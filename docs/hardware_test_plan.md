# Hardware Test Plan — Phase 1+1b Verification

## Prerequisites
- ESP32-S3 flashed with current `feature/pid-charge-mode` branch
- Serial monitor connected (115200 baud) for ESP_LOG output
- Scale powered and connected
- Both motors (coarse + fine) connected and functional
- At least one profile configured with reasonable Kp/Kd values
- Powder and cup available

## Test sequence

Run tests in order — each builds on the previous.

---

### TEST 1: Boot and initialization

**Action**: Power on / reset the ESP32

**Expected in serial log**:
```
flow_model_init: loaded profile 0 (or "no saved data, using defaults")
```

**Check**:
- [ ] No crash on boot
- [ ] Flow model initializes without errors
- [ ] WiFi connects (if configured)
- [ ] REST API accessible (try `http://<ip>/rest/profile_summary` in browser)

---

### TEST 2: Scale stability detection

**Action**: REST call to start charge mode with empty scale
```
GET http://<ip>/rest/charge_mode_state?s0=25.0&s2=1
```
(s2=1 = WAIT_FOR_ZERO state)

**Expected in serial log**:
```
charge_mode: waiting for stable zero
charge_mode: zero stable (mean=X.XXX, sd=X.XXX)
```

**Check**:
- [ ] Detects stable zero within ~5 seconds
- [ ] Mean < 0.02 gn, SD < 0.02 gn reported
- [ ] LED shows "not ready" color, then switches to "ready"

---

### TEST 3: Single charge cycle (PD control)

**Action**: Place cup on scale, charge mode should start automatically after zero detection

**Expected behavior**:
1. Coarse motor starts, speeds up proportionally to error
2. Coarse stops at ~5 gn before target (coarse_stop_threshold)
3. Fine motor starts, slow approach to target
4. Fine stops at ~0.03 gn from target (fine_stop_threshold)
5. LED changes: yellow → green (good) / red (over) / yellow (under)

**Expected in serial log**:
```
charge_mode: coarse motor start
flow_model: record_start motor=0
charge_mode: coarse stop at X.XX gn (error=X.XX)
flow_model: record_stop motor=0
flow_model: analyze coarse: X valid obs, X rejected, quality=X.XX
flow_model: transport_delay=XXXms, inertia=X.XXs
charge_mode: fine motor start
flow_model: record_start motor=1
charge_mode: target reached: X.XX gn (error=X.XXX)
flow_model: analyze fine: X valid obs, X rejected, quality=X.XX
```

**Check**:
- [ ] Coarse motor runs and stops at correct threshold
- [ ] Fine motor takes over and reaches target
- [ ] Final weight within ±0.05 gn of target
- [ ] No overshoot (or minimal)
- [ ] Flow model analysis runs for both motors
- [ ] Quality scores reported (expect 0.3-0.9 for first dispense)
- [ ] Transport delay and inertia values look reasonable:
  - Coarse delay: 200-800ms typical
  - Fine delay: 50-200ms typical
  - Coarse inertia: 0.05-0.5s typical
  - Fine inertia: 0.1-0.6s typical
- [ ] Total dispense time reasonable (5-25s depending on target)

**Record**: write down actual values for comparison later

---

### TEST 4: Over/under detection

**Action**: Run charge cycle, observe LED after completion

**Sub-test A — Normal (within tolerance)**:
- [ ] LED = green after stable settle

**Sub-test B — Force overshoot** (set very aggressive Kp, e.g., 5.0):
- [ ] LED = red after settle
- [ ] Serial log: `charge_mode: OVERCHARGE`
- [ ] REST status shows `event: "over"`

**Sub-test C — Force undershoot** (set very low Kp, e.g., 0.001, or small fine_stop_threshold):
- [ ] LED = yellow after settle
- [ ] Serial log: `charge_mode: UNDERCHARGE`

**Restore normal Kp/Kd after this test.**

---

### TEST 5: Flow model accumulation (5 charge cycles)

**Action**: Run 5 complete charge cycles with same profile and powder

**After each cycle, check serial log for**:
```
flow_model: bin[X] speed=X.XX flow=X.XX count=X
```

**Check after 5 cycles**:
- [ ] Bin sample counts increment (should see count=2, 3, 4, 5)
- [ ] Flow rate values stabilize (EMA convergence)
- [ ] Transport delay and inertia values stabilize
- [ ] Quality scores improve (less noise as model learns)

---

### TEST 6: Flow model confidence (is_trusted)

**Action**: After 5+ cycles, check if model is trusted

**How to verify** (serial log should show at startup or during charge):
```
flow_model: fine model trusted=true (X bins with count>=5)
```
or
```
flow_model: fine model trusted=false (only X bins with count>=5)
```

**Check**:
- [ ] After ~5-10 charges, at least fine model should reach trusted=true
- [ ] Coarse model may need more cycles (sparse data from braking filter)

---

### TEST 7: NVS persistence (reboot test)

**Action**: After 5+ charge cycles, reboot the ESP32

**Expected in serial log after reboot**:
```
flow_model_init: loaded profile 0, coarse bins=X, fine bins=X
```

**Check**:
- [ ] Bin data survives reboot (count values preserved)
- [ ] Transport delay and inertia values preserved
- [ ] Quality score preserved
- [ ] Version check passes (FLOW_MODEL_VERSION=2)

---

### TEST 8: Autotune — coarse stage

**Action**: Start autotune via REST
```
GET http://<ip>/rest/autotune_coarse?a0=1&a1=25.0&a2=10.0&a3=25.0&a4=10.0&a5=8&a6=0.05&a9=0.03&a7=3.0&a8=1&ee=1
```
Parameters:
- a1=25.0 (coarse target weight)
- a2=10.0 (coarse target time)
- a3=25.0 (fine target weight)
- a4=10.0 (fine target time)
- a5=8 (max runs per stage)
- a6=0.05 (coarse weight tolerance)
- a9=0.03 (fine weight tolerance)
- a7=3.0 (time tolerance)
- a8=1 (auto apply)
- ee=1 (save to NVS)

**Expected behavior**:
1. First run: waits for stable zero
2. Subsequent runs: cup removal/return cycle
3. Each run: single coarse motor dispense + settle
4. ES adjusts Kp/Kd between runs

**Poll status**:
```
GET http://<ip>/rest/autotune_coarse?a0=0
```

**Expected response fields**:
```json
{
  "state": 1,
  "stage": 1,
  "progress_pct": XX.X,
  "runs_done": X,
  "active_kp": X.XXX,
  "active_kd": X.XXX,
  "coarse_best_kp": X.XXX,
  "coarse_best_kd": X.XXX,
  "coarse_best_weight_error": X.XXX
}
```

**Check**:
- [ ] State transitions: IDLE → RUNNING → DONE
- [ ] Substatus cycles: STABILIZING → DISPENSING → REMOVE_CUP → RETURN_CUP
- [ ] active_kp/kd change between runs (ES perturbation working)
- [ ] best_weight_error decreases over runs (convergence)
- [ ] Cup removal/return detection works reliably
- [ ] No crashes or hangs during 8 runs
- [ ] Flow model records data during each autotune dispense

---

### TEST 9: Autotune — fine stage

**Action**: Autotune automatically transitions to fine stage after coarse

**Expected behavior**:
1. Uses best coarse Kp/Kd for prefill
2. Fine motor PD control for each trial
3. ES adjusts fine Kp/Kd

**Check**:
- [ ] Fine stage starts automatically after coarse completes
- [ ] Coarse prefill uses tuned parameters (not defaults)
- [ ] Fine Kp/Kd converge to reasonable values
- [ ] Final state = DONE (not ERROR)
- [ ] If auto_apply=true: profile updated with new Kp/Kd values

---

### TEST 10: Autotune trial history

**Action**: After autotune completes, fetch trial data
```
GET http://<ip>/rest/autotune_trials
```

**Expected response**:
```json
[
  {"trial":0,"stage":1,"kp":0.15,"kd":0.002,"weight_error":0.08,...},
  {"trial":1,"stage":1,"kp":0.18,"kd":0.003,"weight_error":0.05,...},
  ...
]
```

**Check**:
- [ ] All trials present (up to 16 for coarse+fine)
- [ ] Stage field correct (1=coarse, 2=fine)
- [ ] Kp/Kd values differ between trials (ES exploration)
- [ ] Weight error and overshoot values are plausible
- [ ] Elapsed time values are reasonable (5-30s)

---

### TEST 11: Charge mode with tuned parameters

**Action**: After autotune, run 3 normal charge cycles

**Check**:
- [ ] Accuracy improved compared to pre-autotune (TEST 3)
- [ ] Overshoot reduced or eliminated
- [ ] Dispense time reasonable
- [ ] Flow model continues learning (bin counts increment)

---

### TEST 12: Autotune cancel

**Action**: Start autotune, then cancel mid-run
```
GET http://<ip>/rest/autotune_coarse?ca=1
```

**Check**:
- [ ] Autotune stops within 1 cycle
- [ ] Motors stop safely
- [ ] State returns to IDLE (not ERROR)
- [ ] Original profile Kp/Kd preserved (not overwritten)
- [ ] Flow model data from partial runs still saved

---

### TEST 13: Precharge

**Action**: Enable precharge in charge mode config
```
GET http://<ip>/rest/charge_mode_config?c10=1&c11=1000&c12=2.0&ee=1
```
(c10=enable, c11=1000ms, c12=2.0 RPS)

**Action**: Run a charge cycle, after completion observe motor behavior

**Check**:
- [ ] After target reached, motor runs briefly (1s at 2.0 RPS)
- [ ] Precharge only happens AFTER charge is complete
- [ ] No effect on charge accuracy

---

## Results template

| Test | Pass/Fail | Notes |
|------|-----------|-------|
| 1. Boot | | |
| 2. Zero stability | | |
| 3. Single charge | | |
| 4a. Over detection | | |
| 4b. Under detection | | |
| 5. Model accumulation | | |
| 6. Model confidence | | |
| 7. NVS reboot | | |
| 8. Autotune coarse | | |
| 9. Autotune fine | | |
| 10. Trial history | | |
| 11. Tuned charge | | |
| 12. Autotune cancel | | |
| 13. Precharge | | |

## Measured values (fill in during testing)

| Parameter | Value | Expected range |
|-----------|-------|----------------|
| Coarse transport delay | ms | 200-800 ms |
| Fine transport delay | ms | 50-200 ms |
| Coarse inertia | s | 0.05-0.5 s |
| Fine inertia | s | 0.1-0.6 s |
| Quality score (first charge) | | 0.3-0.9 |
| Quality score (5th charge) | | 0.5-1.0 |
| Autotune coarse best Kp | | |
| Autotune coarse best Kd | | |
| Autotune fine best Kp | | |
| Autotune fine best Kd | | |
| Pre-autotune accuracy (error gn) | | |
| Post-autotune accuracy (error gn) | | |
| Typical dispense time | s | 5-25 s |
