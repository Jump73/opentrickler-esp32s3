# Flow Model — real-time motor-to-weight correlation learning

## Overview
The flow model learns the non-linear relationship between motor speed (RPS) and powder
flow rate (gn/s) from every dispense. It builds a piecewise-linear lookup table per motor,
persisted in NVS per profile. This is the foundation for feedforward control (phase 2)
and analytical PID tuning (phase 3).

## Status: Phase 1+1b COMPLETE, Phase 2a-c REVERTED, Phase 2d-2g TODO

Phase 1 (recording + model building) is fully implemented and tested.
- Flow model component created and integrated
- Recording works in both charge_mode and autotune
- NVS persistence verified (survives reboot)
- EMA merge verified across multiple dispenses
- Braking contamination filter applied (rejects deceleration samples)
- Inertia measurement working (coarse ~0.2s, fine ~0.35-0.44s)
- Transport delay measured (coarse ~600ms, fine ~70-80ms)
- Pure PD control active (no feedforward), model learns in background

Phase 1b (model quality improvements) — COMPLETE:
- Quality-weighted EMA: alpha = BASE_ALPHA * quality * clamp(n_obs/10, 0.1..1.0)
- Spin-up filter: rejects first 100ms after motor start
- Non-steady filter: rejects samples with |delta_speed| > 0.1 RPS
- Quality score (0..1) computed from rejection ratio + settling noise RMS
- Quality-weighted EMA also applied to transport_delay and inertia
- Per-bin confidence: `flow_model_is_trusted()` returns true when ≥3 bins have ≥5 dispenses
- `last_quality` field persisted per motor in NVS
- FLOW_MODEL_VERSION bumped to 2 (old NVS data auto-resets on first boot)
- Huber-like residual clipping on EMA update — prevents single anomalous dispense from
  shifting a bin: `g(r) = r if |r| <= HUBER_DELTA, else HUBER_DELTA * sign(r)`;
  `flow_bin += alpha * g(r)` instead of direct EMA

## Architecture

### Component: `components/flow_model/`

```
components/flow_model/
├── CMakeLists.txt          (REQUIRES nvs_flash profile)
├── flow_model.c
└── include/
    └── flow_model.h
```

### Data structures

**Recording buffer** (static, RAM only, ~24KB):
```c
typedef struct {
    uint32_t timestamp_ms;  // ms since recording start
    float    speed_rps;     // motor speed that was SET
    float    weight_gn;     // scale reading
} flow_sample_t;

#define FLOW_RECORD_MAX  2048
```

**Flow model** (persisted in NVS, ~350B per profile):
```c
#define FLOW_TABLE_POINTS 12

typedef struct {
    float    speed_rps;
    float    flow_rate_gn_s;
    uint16_t sample_count;  // number of dispenses that contributed
} flow_point_t;

typedef struct {
    uint8_t      num_points;
    float        transport_delay_ms;  // delay from motor start to first weight change
    float        inertia_factor_s;    // seconds of "in-flight" powder after motor stop
    float        last_quality;        // quality score of last dispense (0..1)
    flow_point_t points[FLOW_TABLE_POINTS];
} flow_model_single_t;

typedef struct {
    uint8_t             version;
    flow_model_single_t coarse;
    flow_model_single_t fine;
} flow_model_t;
```

### Fixed speed bins (not adaptive — easier merging)
- Coarse: 0.1, 0.2, 0.4, 0.7, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0, 5.0
- Fine:   0.05, 0.1, 0.2, 0.3, 0.5, 0.7, 1.0, 1.3, 1.6, 2.0, 2.3, 3.0

### Public API

```c
esp_err_t flow_model_init(void);

// Recording (called from PID loops, inline — microseconds per call)
void flow_model_record_start(uint8_t motor);
void flow_model_record_sample(float speed_rps, float weight_gn);
void flow_model_record_stop(void);

// Analysis after dispense (merges with existing model, saves to NVS)
esp_err_t flow_model_analyze_and_update(uint8_t profile_idx);

// Query (for feedforward, phase 2)
float flow_model_get_flow_rate(uint8_t profile_idx, uint8_t motor, float speed_rps);
float flow_model_get_speed_for_rate(uint8_t profile_idx, uint8_t motor, float desired_gn_s);
float flow_model_get_transport_delay(uint8_t profile_idx, uint8_t motor);
float flow_model_get_inertia(uint8_t profile_idx, uint8_t motor);

// Persistence
esp_err_t flow_model_save(uint8_t profile_idx);
esp_err_t flow_model_load(uint8_t profile_idx);

// Confidence query — true when ≥3 bins have sample_count ≥ 5
bool flow_model_is_trusted(uint8_t profile_idx, uint8_t motor);

// Debug
esp_err_t flow_model_get(uint8_t profile_idx, flow_model_t *out);
```

## Analysis algorithm (runs after each dispense)

### Filtering (steps 1-3)

1. **Flow rate from sample pairs**: dt = delta_time, dw = delta_weight -> flow = dw/dt
2. **Basic filtering**: reject if dt<5ms, dw<0, flow>50 gn/s
3. **Extended filtering** (Phase 1b):
   - **Braking filter**: reject if speed is decreasing (speed[i] < speed[i-1] - 0.02)
     — prevents braking-phase data from contaminating high-speed bins
   - **Spin-up filter**: reject first ~100ms after motor start (transport delay window)
     — motor and powder flow not yet established, readings are noise
   - **Non-steady filter**: reject if |delta_speed| > 0.1 RPS between samples
     — keeps only steady-state observations where speed ≈ constant
     — subsumes the old |delta_speed|>0.5 transient check (tighter threshold)

### Model building (steps 4-6)

4. **Bin assignment**: each observation -> nearest speed bin (within half-width)
5. **Median per bin**: from observations of this dispense (min 3 obs, noise robust)
6. **Quality-weighted EMA merge** (Phase 1b):
   - Compute quality score (0..1) for this dispense:
     ```
     quality = 1.0
     quality -= 0.3 * (rejected_count / total_candidates)  // filter rejection ratio
     quality -= 0.3 * clamp(settling_noise_rms / 0.04, 0, 1)  // scale noise
     quality = clamp(quality, 0.05, 1.0)
     ```
   - Compute adaptive alpha per bin:
     ```
     alpha = BASE_ALPHA * quality * clamp(n_obs / 10.0, 0.1, 1.0)
     ```
     where BASE_ALPHA = 0.3, n_obs = number of valid observations in this bin
   - Merge: `flow_new = (1-alpha)*flow_old + alpha*flow_dispense`
   - Effect: noisy dispenses (high rejection, shaky scale) barely affect the model;
     clean dispenses with many observations update it more aggressively

### Per-bin confidence (Phase 1b)

- `sample_count` already tracked per bin (incremented each merge)
- **Trusted threshold**: a bin is considered trusted when `sample_count >= 5`
- **Model trusted**: `model_trusted = true` when ≥3 bins around the typical operating
  speed are trusted (enough coverage to make predictions)
- Used as gate for Phase 2d adaptations and future Phase 3

### Dynamic parameters (steps 7-8)

7. **Transport delay**: time from record_start to first delta_weight > 0.02 gn
   — quality-weighted EMA: `alpha = BASE_ALPHA * quality`
8. **Inertia**: weight gained after motor stop / pre-stop flow rate
   — quality-weighted EMA: `alpha = BASE_ALPHA * quality`
   — requires post-stop samples (10 readings with speed=0 after motor off)
   — `record_stop()` only marks stop_tick, keeps recording active
   — `analyze_and_update()` ends recording

## Integration points

**`charge_mode.c`** — `do_wait_for_complete()`:
- `flow_model_record_start(MOTOR_COARSE)` after motor enable
- `flow_model_record_sample(speed, weight)` after each PD calculation
- At coarse-to-fine transition:
  1. `record_stop()` — marks stop time, keeps recording
  2. 10x `record_sample(0.0f, weight)` — post-stop settling for inertia
  3. `analyze_and_update()` — ends recording, merges, saves
  4. `record_start(MOTOR_FINE)` — start fine motor recording
- After completion: same pattern (stop, 10 settling samples, analyze)
- FF model status logged at charge start (informational only)

**`autotune.c`** — `run_single_motor_dispense()`:
- `record_start(motor)` after motor enable
- `record_sample(speed, weight)` after each PD calculation
- At motor stop (normal, timeout, or cancel):
  1. `record_stop()` — marks stop time
  2. 10x `record_sample(0.0f, weight)` — post-stop settling
  3. `analyze_and_update()` — ends recording, merges, saves

**`main.c`**: `flow_model_init()` called after `profile_init()` (Step 5b)

### NVS layout
- Namespace: `"flow_model"`
- Keys: `"fm_0"` through `"fm_7"` (one per profile)
- Does NOT modify `profile_t` — backward compatible

## Memory usage

| Element | Size |
|---------|------|
| Recording buffer | 24 KB |
| Models for 8 profiles | 2.8 KB |
| Per-bin accumulators (static) | 3.1 KB |
| **Total** | **~30 KB** |

## Known issues — FIXED

1. **Braking phase contamination** — FIXED. Added filter in flow_model.c analysis:
   rejects observations when speed is decreasing (speed[i] < speed[i-1] - 0.02).
   This prevents braking-phase data from polluting high-speed bins.

2. **Inertia always zero** — FIXED. `record_stop()` no longer ends recording.
   Post-stop samples (speed=0, weight=settling) are collected for 10 readings
   before `analyze_and_update()` is called. Inertia = overshoot / pre-stop flow.

## Known limitations

1. **Coarse model sparse data** — PD control ramps speed through bins quickly,
   so the braking filter leaves only ~4 valid observations per dispense, all
   concentrated in one bin (typically 2.50 RPS). The model needs many dispenses
   to populate multiple bins. This is acceptable — the model learns slowly but
   cleanly. It will improve once feedforward is re-enabled (steady-state speeds).

2. **ki not used** — The system uses pure PD control (kp + kd). Ki is always 0
   and will not be used. All integral-related code paths are effectively dead.
   Transport delay integral suppression (Phase 2 item 3) is therefore unnecessary.

## Phase 2: Using flow model data in charge control

### Phase 2a: Inertia-aware coarse stop — REVERTED

Implemented and tested on hardware. **Reverted** because:
- Coarse should always stop at fixed `coarse_stop_threshold` — that's what the user tuned
- If model underestimates flow rate → dynamic threshold drops below config → coarse runs
  longer → overshoot. The opposite of what we want.
- Coarse stop threshold is a handoff point to fine motor, not a precision target.
  In-flight powder from coarse is handled by fine motor, not by adjusting the threshold.

### Phase 2b: Inertia-aware fine stop — REJECTED

Attempted but rejected. Fine motor inertia stop is too complex:
- Requires settle-wait-retry logic (stop motor, wait, check if undershoot, restart)
- PD already handles fine motor well at low speeds
- Fine stop stays as simple `error < fine_stop_threshold`

### Phase 2c: Feedforward speed — REVERTED, future work

Feedforward was implemented and fully reverted due to multiple issues:
1. **FF + PD double counting**: both map error→speed, adding them doubles the gain
   - FF must **replace** P-term, not add to it
2. **rate_gain too aggressive**: desired_rate exceeded model's range → max speed returned
3. **Positive feedback loop**: FF→max speed→only max bin learns→model stuck→FF returns max
4. **Model had garbage data**: braking contamination + inertia=0 (both now fixed)

**Lessons for re-implementation:**
- FF should replace kp*error, not add to it. kd*derivative stays as correction
- rate_gain must be conservative and stay within model's known range
- Need minimum `sample_count >= 5` before trusting a bin
- Consider gradual ramp-up of FF contribution
- Model data quality is now good (braking filter + inertia working)

### Phase 2d: Auto max_speed limit — TODO (safe adaptation)

First **active** use of flow model data in control. Designed to be one-directional:
only reduces speed, never increases aggressiveness. Safe by construction.

**Concept**: after each dispense, check if overshoot occurred. If overshoot pattern
persists, reduce the maximum allowed motor speed for subsequent dispenses.

**Logic**:
```
// After dispense analysis, in charge_mode or autotune:
if (overshoot_post_stop > OVERSHOOT_THRESHOLD) {
    overshoot_streak++;
} else {
    overshoot_streak = 0;
}

if (overshoot_streak >= N_STREAK) {  // e.g. 3 consecutive overshoots
    max_speed_rps -= SPEED_STEP;     // e.g. -0.2 RPS
    max_speed_rps = max(max_speed_rps, MIN_SAFE_SPEED);
    overshoot_streak = 0;
}
```

**Properties**:
- Only decreases max_speed, never increases → cannot cause overshoot
- Requires N consecutive overshoots before acting → no knee-jerk reaction
- Has a floor (MIN_SAFE_SPEED) → motor always runs fast enough to work
- Does NOT touch coarse_stop_threshold or fine_stop_threshold (lesson from Phase 2a)
- Does NOT touch Kp/Kd (that's Phase 3)

**Optional: soft speed cap near target**:
```
// In PD loop, after computing speed:
if (error < E_CAP_THRESHOLD) {  // e.g. last 2.0 gn
    speed = min(speed, cap_speed);  // limit approach speed
}
```
- `cap_speed` learned from post-stop overshoot history
- If overshoot grows → cap decreases → gentler approach
- Redundant with good Kp tuning, but provides extra safety layer
- Only apply when model_trusted = true

**Requires**: Phase 1b (quality/confidence) to gate activation.
Only activate when `model_trusted = true` for the active motor.

**Persistence**: max_speed_limit stored in flow_model NVS (per profile, per motor).
Reset to default when user runs autotune (fresh start).

### Phase 2e: Predictive fine cutoff — TODO (high-value, low-risk)

First use of flow model data **inside the PD loop** for real-time control.
Fine motor only — coarse thresholds are never modified (lesson from Phase 2a).

**Concept**: instead of waiting for `error < fine_stop_threshold` (reactive),
predict the final weight including in-flight powder and stop earlier.

**Algorithm** (runs every PD iteration, ~200ms):
```c
// In charge_mode PD loop, fine motor phase:
if (flow_model_is_trusted(profile_idx, MOTOR_FINE)) {
    float flow_rate = flow_model_get_flow_rate(profile_idx, MOTOR_FINE, current_speed);
    float inertia_s = flow_model_get_inertia(profile_idx, MOTOR_FINE);
    float inflight_gn = flow_rate * inertia_s;

    // Account for one extra loop iteration (200ms at current flow rate)
    float loop_compensation_gn = flow_rate * 0.2f;

    float predicted_final = current_weight + inflight_gn + loop_compensation_gn;

    if (predicted_final >= target_weight - fine_stop_threshold) {
        motor_set_speed(MOTOR_FINE, 0);
        // ... normal post-stop settling
    }
}
// Fallback: if model not trusted, use existing threshold-only logic (unchanged)
```

**Key design decisions**:
- **Fine motor only**: coarse stop threshold is a handoff point, not a precision target.
  User tunes it manually and it must not change (Phase 2a lesson).
- **Gated by `flow_model_is_trusted()`**: if model has insufficient data, falls back
  to current PD-only behavior. Zero risk to untrained profiles.
- **Uses measured values only**: `inertia_factor_s` and `flow_rate` are learned from
  real dispenses via quality-weighted EMA. No synthetic parameters to tune.
- **Loop compensation**: at 5 Hz (200ms), one extra iteration of flow must be accounted
  for. This is `flow_rate * 0.2s` — simple and conservative.
- **Does NOT replace PD**: PD still controls motor speed. Predictive cutoff only
  decides *when* to stop. PD naturally reduces speed as error shrinks, so the
  prediction becomes more accurate near target (low speed = low flow = small inflight).

**Why not tune inflight_gain, delay_ms, brake_gain as parameters?**
Evaluated as part of SPSA-based autotune proposal (see `docs/autotune_comparison_rp2040_vs_esp32s3.md`).
Rejected because:
- `inertia_factor_s` is already measured empirically by flow model — adding a tunable
  `inflight_gain` multiplier would fight the measured value
- `transport_delay_ms` is already measured — making it a tunable parameter is regression
- `brake_gain` has no physical actuator (stepper motor: speed=0 is the only "brake")
- `switch_margin_gn` ≡ `coarse_stop_threshold` which was already reverted (Phase 2a)
- SPSA with 6 parameters needs ~40-60 trials (20-30 min), too expensive in powder

**Expected improvement**: reduced fine motor overshoot by 30-60% on trusted profiles.
Primary benefit is for fast powders with high inertia (fine inertia > 0.3s).

**Requires**: Phase 1b (confidence gating). Recommended after Phase 2d (max_speed limit).

### Phase 2f: Improved autotune cost function and early stop — TODO

Upgrade the (1+1)-ES autotune fitness function and add convergence shortcuts.
Keeps the existing ES algorithm (no switch to SPSA — see rationale below).

**Improved cost function**:
```c
float autotune_cost(trial_result_t tr) {
    // Hard constraints — trial is unusable
    if (tr.t_total_s > 30.0f) return 1e9f;

    float e  = fabsf(tr.target_gn - tr.settled_gn);
    float o  = fmaxf(0.0f, tr.peak_gn - tr.target_gn);
    float sd = tr.settle_sd_gn;
    float dt = fmaxf(0.0f, tr.t_total_s - t_target_s);

    // Weight accuracy dominates, overshoot penalized 4× vs undershoot
    // Time is secondary, settling noise is minor tiebreaker
    float J = 1200.0f * e + 4800.0f * o + 800.0f * sd + 25.0f * dt;

    // Optional: extra penalty for very slow dispenses
    if (tr.t_total_s > 22.0f) J += 200.0f;

    return J;
}
```

**Why these weights**:
- 0.05 gn error → 60 pts (baseline)
- 0.05 gn overshoot → 240 pts (4× worse than undershoot — overshoot is irreversible)
- 0.02 gn settle SD → 16 pts (minor factor)
- 10s time delta → 250 pts (matters, but never overrides accuracy)

Compared to current fitness (`abs_error + 2*overshoot`):
- Adds time optimization (currently ignored)
- Adds settling noise (currently ignored)
- Stronger overshoot penalty (4× vs 2×)
- Absolute scale enables comparison across different targets

**Early stop**:
```c
// After each trial in ES loop:
if (fabsf(weight_err) <= weight_tol
    && overshoot <= 0.02f
    && t_total_s <= t_target_s + time_tol
    && settle_sd <= 0.02f) {
    consecutive_good++;
} else {
    consecutive_good = 0;
}
if (consecutive_good >= 3) {
    // Converged — skip remaining trials
    break;
}
```

**Winner verification** (run after ES completes):
```c
// Take best θ* from ES, run 2 verification trials
// Accept only if median cost confirms quality
// This prevents lucky single-trial flukes from being saved
trial_t v1 = run_trial(best_theta);
trial_t v2 = run_trial(best_theta);
float median_cost = median3(best_cost, cost(v1), cost(v2));
if (median_cost < ACCEPTABLE_THRESHOLD) {
    apply_to_profile(best_theta);
}
```

**Why keep (1+1)-ES instead of switching to SPSA**:
- ES needs 1 trial/iteration; SPSA needs 2 (gradient estimation requires θ+cΔ and θ−cΔ)
- Our parameter space is small (2 params per stage: Kp, Kd)
- For 2D, ES converges in ~8-12 trials; SPSA would need similar count but with 2× cost
- ES already works in log-space with adaptive σ — well-suited for gain parameters
- SPSA shines in high dimensions (6+ params) — but we rejected the extra parameters
- Powder is expensive: fewer trials = better

**Requires**: nothing (can be implemented independently of Phase 2d/2e).

**Flow model freeze during autotune** (new addition to Phase 2f):

During autotune the live flow model must be **read-only**. Autotune data is atypical
(aggressive excitation, different speed trajectories than normal dispenses) and if
ingested into the live model, it biases the `speed→flow` map and corrupts delay/inertia
estimates. Additionally, two adaptive loops reacting to each other (autotune adjusts
gains ↔ model adjusts plant estimate) can cause oscillatory meta-dynamics and slow
convergence.

**Policy**:
- `model_live`: read-only during autotune (used by any predictive logic, not updated)
- `model_shadow`: separate buffer, updated from autotune samples with very low alpha or
  full-batch fit; never used for control decisions
- After autotune completes, if new gains were accepted **and** shadow quality metrics
  pass (sufficient bin coverage, low residual variance): bounded merge
  `model_live ← (1 − beta)*model_live + beta*model_shadow` with `beta ≤ 0.15`
- Otherwise discard shadow — live model stays exactly as it was before autotune

**New API** (to be added to `flow_model.h`):
```c
void flow_model_freeze(void);    // autotune start: block analyze_and_update()
void flow_model_unfreeze(void);  // autotune end: restore normal updates
bool flow_model_is_frozen(void);
```

`autotune.c` calls `flow_model_freeze()` before the ES loop and
`flow_model_unfreeze()` (+ conditional shadow merge) after winner verification.

**Post-autotune reduced alpha** (new addition to Phase 2f):

After unfreezing, the first N normal dispenses (recommended N = 5) use
`effective_alpha = 0.5 * alpha` to prevent the model from overreacting to the
behavioural difference between the old and new gains. Implemented as a countdown
counter `post_autotune_cooldown_n` in `flow_model.c` that is set by
`flow_model_unfreeze()` and decremented per `analyze_and_update()` call.

**Requires**: nothing (can be added alongside other Phase 2f changes).

### Phase 2g: Diagnostics ring buffer + fine correction burst — TODO (optional)

#### 2g.1 Per-dispense diagnostics ring buffer

A compact record written to a static RAM ring buffer after every dispense.
Enables objective pass/fail analysis and debugging without heavy on-device computation.

**Record struct** (stored in a fixed-size ring buffer, e.g. 32 entries):
```c
typedef struct {
    uint32_t timestamp_ms;
    uint8_t  profile_idx;
    uint16_t coarse_time_ms;
    uint16_t fine_time_ms;
    float    overshoot_gn;       // max(0, peak_weight - target)
    float    e_final_gn;         // target - settled_weight
    float    model_confidence;   // flow_model_is_trusted() → 0.0 or 1.0
    float    delay_ms;           // transport delay at time of dispense
    float    inflight_s;         // inertia factor at time of dispense
    float    kp_fine;
    float    kd_fine;
    bool     autotune_flag;      // true if this trial was part of autotune
} dispense_record_t;
```

**Implementation**:
- Static ring buffer `dispense_record_t disp_log[DISP_LOG_SIZE]` in `charge_mode.c` or
  shared module
- Written from `do_wait_for_complete()` (normal) and `run_single_motor_dispense()` (autotune)
- Exposed via REST endpoint `/rest/dispense_log` for WebUI display (future)
- Does not require NVS — RAM only, lost on reboot, sufficient for session analysis

**Requires**: nothing. Purely additive, zero impact on control.

#### 2g.2 Fine correction burst after settle (opt-in)

After the fine motor stops and weight settles, if the final error exceeds a deadband
(undershoot), restart the fine motor for a short correction burst.

**Algorithm**:
```c
// SETTLE_VERIFY: after fine motor stop, wait settle_window_ms
vTaskDelay(pdMS_TO_TICKS(charge_mode_config.settle_verify_ms));  // e.g. 800ms
float w_final;
scale_block_wait_for_measurement(200, &w_final);
float e_final = target - w_final;

if (e_final > charge_mode_config.correction_deadband_gn) {  // e.g. 0.05 gn
    // Small correction burst — fine motor only
    motor_enable(MOTOR_FINE, true);
    // Re-enter PD fine loop with updated weight; stop at fine_stop_threshold as normal
}
```

**Risk**: a second burst can overshoot if fine inertia is underestimated. Therefore:
- Opt-in only: enabled via `charge_mode_config.correction_burst_enable` (default false)
- At most **one** correction burst per dispense (no recursive retry)
- Only triggers if `e_final > correction_deadband_gn` (dead zone prevents dithering)
- Gate on `flow_model_is_trusted()` if predictive cutoff (Phase 2e) is active, to avoid
  compounding two unreliable predictions
- `correction_deadband_gn` should be ≥ scale noise RMS (typically 0.04–0.06 gn)

**Note**: this is the "settle-wait-retry" logic that caused Phase 2b to be rejected.
It is included here as an opt-in because the risk is manageable with the deadband gate
and single-retry limit, and because Phase 2e (predictive cutoff) reduces the likelihood
of large undershots that would trigger it.

**Requires**: Phase 2e recommended (reduces undershoot cases that trigger correction).

### Current state summary
- **Active**: flow model self-learning with quality-weighted EMA + Huber clipping + extended filters
- **Complete**: Phase 1 + 1b — recording, model building, quality scoring, confidence tracking, Huber EMA
- **Reverted**: inertia-aware coarse stop (Phase 2a) — overwrites user-tuned thresholds
- **Rejected**: inertia-aware fine stop (Phase 2b) — too complex for marginal gain
- **TODO**: auto max_speed limit (Phase 2d) — first safe active adaptation
- **TODO**: predictive fine cutoff (Phase 2e) — first real-time use of flow model in PD loop
- **TODO**: improved autotune cost + early stop + model freeze + post-autotune alpha (Phase 2f)
- **TODO (optional)**: diagnostics ring buffer + fine correction burst (Phase 2g)
- **Future**: feedforward speed (Phase 2c) — needs careful redesign (requires 2e stable)
- PD control unchanged with fixed thresholds, precision is priority over speed
- User preference: precision > speed. Overshoot = bad, undershoot = acceptable
- Next steps: 2d → 2e → 2f → 2g (optional) → 2c → 3 → 4 (BO offloaded to WebUI)

## Phase 3: Analytical PD tuning (future)

Use the flow model's slope (d_flow/d_speed) at the operating point to analytically
compute optimal Kp/Kd values, replacing or augmenting the evolutionary autotune.
Note: only Kp and Kd — Ki is not used.

**CMA-ES as upgrade path**: if Phase 3 requires tuning more than 2 parameters
simultaneously (e.g., coarse Kp/Kd + fine Kp/Kd = 4D, or adding feedforward gains),
consider CMA-ES (Covariance Matrix Adaptation Evolution Strategy) instead of (1+1)-ES.
CMA-ES captures parameter correlations (e.g., Kp↔Kd interaction) and is more robust
in noisy environments with populations of λ=6-10 candidates per generation.
Not worth it for 2D — (1+1)-ES is more sample-efficient there. CMA-ES starts to
outperform at 4-6+ dimensions where parameter interactions matter.

## Phase 4: Offloaded Bayesian Optimization via WebUI (future)

**Concept**: move the optimization intelligence to PC/WebUI while ESP32 remains
a trial executor. This minimizes the number of physical dispenses needed.

**Architecture**:
```
WebUI (PC)                          ESP32
  │                                   │
  ├─ GP surrogate model               │
  ├─ Acquisition function (EI/UCB)    │
  ├─ Suggest next θ ──────────────────▶ Execute dispense
  │                                   ├─ Record telemetry
  ◀────────────────────────────────── ├─ Return trial result
  ├─ Update GP model                  │
  ├─ Suggest next θ ...               │
  └─ ...                              └─ ...
```

**Why this is attractive**:
- Bayesian Optimization is the gold standard for expensive black-box optimization
- Gaussian Process surrogate models the cost landscape from few samples
- Acquisition function (Expected Improvement) balances exploration vs exploitation
- Typically converges in 8-15 trials for 2-4D — similar to ES but with better
  uncertainty quantification and less wasted powder
- ESP32 firmware needs zero changes — `/rest/autotune_trials` already returns
  full per-trial telemetry (stage, kp, kd, weight_error, time_error, overshoot,
  settled_weight, elapsed_s)

**Why not now**:
- Requires active PC connection during autotune (no standalone operation)
- GP library needed in WebUI (Python backend or JS implementation)
- Round-trip latency: PC → REST → ESP → dispense → REST → PC → compute
- Current (1+1)-ES on ESP32 works well enough for 2D optimization
- Phase 2e (predictive cutoff) and 2f (better cost) give bigger gains first

**When to consider**: after Phase 2e+2f are stable and if users want to optimize
more parameters simultaneously (e.g., Kp/Kd + feedforward gains + speed profiles).
The existing REST telemetry endpoint makes this a WebUI-only addition.

## Evaluated and rejected approaches

### SPSA-based autotune with 6-parameter vector (rejected)

A full SPSA (Simultaneous Perturbation Stochastic Approximation) approach was evaluated
that would tune: delay_ms, inflight_gain, brake_gain, switch_margin_gn, fine_kp, fine_kd.

**Rejected because**:
1. **SPSA needs 2 trials per iteration** for gradient estimation (θ+cΔ and θ−cΔ).
   With 6 parameters and realistic 5 iterations = 10 trials minimum. Gradient estimates
   in 6D from 10 trials are extremely noisy. Reliable convergence needs ~40-60 trials
   (20-30 minutes, significant powder waste).
2. **4 of 6 parameters are unnecessary**:
   - `delay_ms`: already measured empirically by flow model with quality-weighted EMA
   - `inflight_gain`: flow model already measures `inertia_factor_s` empirically
   - `brake_gain`: no physical actuator (stepper motor has no variable braking)
   - `switch_margin_gn`: equivalent to `coarse_stop_threshold`, already reverted (Phase 2a)
3. **Remaining 2 params (fine_kp, fine_kd)** are already tuned by (1+1)-ES which needs
   only 1 trial/iteration and converges well in 2D.
4. **Flow model refresh phase (6 warmup trials)** is unnecessary — model learns from
   every normal dispense. If user has done a few charges, model is already current.

**What was adopted from the proposal**:
- Improved cost function with time + settle SD terms (→ Phase 2f)
- 4× overshoot penalty instead of 2× (→ Phase 2f)
- Early stop on 3 consecutive good trials (→ Phase 2f)
- Winner verification with 2 confirmation trials (→ Phase 2f)
- Predictive cutoff concept using flow model data (→ Phase 2e, simplified)

See `docs/autotune_comparison_rp2040_vs_esp32s3.md` for full RP2040 vs ESP32-S3 comparison.

### Relay feedback autotune / Astrom-Hagglund (rejected)

Classic industrial PID autotuning method: force relay oscillations, measure ultimate
gain (Ku) and period (Tu), compute PID parameters via Ziegler-Nichols or similar rules.

**Rejected because**:
1. **Not suited for weight control loop**: the plant (powder trickler → scale) is not
   a continuous process — it has discrete granular flow, transport delay, and settling
   dynamics that don't produce clean oscillations.
2. **Motor speed loop calibration**: relay could work for characterizing motor dynamics
   (speed command → actual flow), but flow model already does this passively from every
   dispense with quality-weighted EMA. Relay would require a dedicated calibration mode
   that wastes powder without improving accuracy.
3. **Already covered by flow model**: the relay method's goal is to characterize the
   plant — our flow model does exactly that, but continuously and without dedicated
   test runs.

**From RP2040 comparison**: the RP2040 project also does not use relay method. Both
projects use iterative optimization (binary search on RP2040, ES on ESP32-S3).

### (µ/µ,λ)-ES and population-based ES variants (deferred)

Population-based ES generates λ=6-10 candidates per generation and selects best µ.
More robust against measurement noise than single-offspring (1+1)-ES.

**Deferred because**:
1. **6 trials per generation** vs 1 trial in (1+1)-ES — at ~15-30s per trial, one
   generation takes 90-180s. With budget of ~16 trials, only 2-3 generations possible.
2. **Noise is manageable**: scale noise is ±0.02gn, small relative to typical weight
   errors during tuning (0.05-0.5gn). Winner verification (Phase 2f) addresses the
   same concern more cheaply — 2 extra trials vs 5× trials per generation.
3. **Upgrade path exists**: if measurement noise proves problematic after Phase 2f
   winner verification is implemented, population ES or CMA-ES can be adopted.
   The cost function and parameter space are algorithm-agnostic.

### Tunable prediction parameters in autotune (rejected)

Instead of tuning prediction parameters (inflight_gain, brake_gain, etc.), the system
uses **measured physical quantities** from the flow model:
- `inertia_factor_s` — empirical seconds of in-flight powder (quality-weighted EMA)
- `transport_delay_ms` — empirical delay from motor start to scale response
- `flow_rate(rps)` — empirical speed-to-flow mapping per bin

Rationale: tuning a multiplier on top of a measured value introduces a second source
of adaptation that can fight the first. If the model measures inertia = 0.35s and
autotune learns inflight_gain = 0.8, the effective inertia is 0.28s — but the model
will then re-learn inertia based on the changed behavior, creating an unstable loop.
Using measured values directly is simpler, more transparent, and self-correcting.

## Files

Created:
- `components/flow_model/CMakeLists.txt`
- `components/flow_model/flow_model.c`
- `components/flow_model/include/flow_model.h`

Modified:
- `components/charge_mode/charge_mode.c` — recording + post-stop settling, FF log (no FF active)
- `components/charge_mode/CMakeLists.txt` — added flow_model to REQUIRES
- `components/autotune/autotune.c` — recording + post-stop settling in run_single_motor_dispense
- `components/autotune/CMakeLists.txt` — added flow_model to REQUIRES
- `components/rest_handlers/rest_handlers.c` — charge mode state log level LOGI→LOGD
- `main/main.c` — flow_model_init(), "System running" log level LOGI→LOGD
- `main/CMakeLists.txt` — added flow_model to REQUIRES
