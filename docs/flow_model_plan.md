# Flow Model — real-time motor-to-weight correlation learning

## Overview
The flow model learns the non-linear relationship between motor speed (RPS) and powder
flow rate (gn/s) from every dispense. It builds a piecewise-linear lookup table per motor,
persisted in NVS per profile. This is the foundation for feedforward control (phase 2)
and analytical PID tuning (phase 3).

## Status: Phase 1 COMPLETE, Phase 2 all REVERTED, model learns passively

Phase 1 (recording + model building) is fully implemented and tested.
- Flow model component created and integrated
- Recording works in both charge_mode and autotune
- NVS persistence verified (survives reboot)
- EMA merge verified across multiple dispenses
- Braking contamination filter applied (rejects deceleration samples)
- Inertia measurement working (coarse ~0.2s, fine ~0.35-0.44s)
- Transport delay measured (coarse ~600ms, fine ~70-80ms)
- Pure PD control active (no feedforward), model learns in background

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

// Debug
esp_err_t flow_model_get(uint8_t profile_idx, flow_model_t *out);
```

## Analysis algorithm (runs after each dispense)

1. **Flow rate from sample pairs**: dt = delta_time, dw = delta_weight -> flow = dw/dt
2. **Filtering**: reject if dt<5ms, dw<0, flow>50 gn/s, |delta_speed|>0.5 (transient)
3. **Braking filter**: reject if speed is decreasing (speed[i] < speed[i-1] - 0.02)
   — prevents braking-phase data from contaminating high-speed bins
4. **Bin assignment**: each observation -> nearest speed bin (within half-width)
5. **Median per bin**: from observations of this dispense (min 3 obs, noise robust)
6. **EMA merge with model**: alpha=0.3, new_point = 0.7*old + 0.3*new
7. **Transport delay**: time from record_start to first delta_weight > 0.02 gn
8. **Inertia**: weight gained after motor stop / pre-stop flow rate
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

### Current state summary
- **Active**: flow model self-learning (records every dispense, updates model in NVS)
- **Reverted**: inertia-aware coarse stop (Phase 2a) — model data too unreliable
- **Rejected**: inertia-aware fine stop (Phase 2b) — too complex
- **Future**: feedforward speed (Phase 2c) — needs careful redesign
- PD control unchanged with fixed thresholds, precision is priority over speed
- User preference: precision > speed. Overshoot (przesyp) = bad, undershoot = acceptable
- Flow model is purely passive — learns in background, no data used in control yet

## Phase 3: Analytical PD tuning (future)
Use the flow model's slope (d_flow/d_speed) at the operating point to analytically
compute optimal Kp/Kd values, replacing or augmenting the evolutionary autotune.
Note: only Kp and Kd — Ki is not used.

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
