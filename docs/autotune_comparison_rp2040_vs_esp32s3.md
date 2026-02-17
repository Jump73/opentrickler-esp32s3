# Autotune Comparison: RP2040 vs ESP32-S3

## Overview

| Feature | RP2040 (`git_rp2040`) | ESP32-S3 (`opentrickler-esp32s3`) |
|---------|----------------------|----------------------------------|
| **Algorithm** | Adaptive binary step search | (1+1)-ES evolutionary strategy |
| **Parameter space** | Linear (Kp, Kd directly) | Log-space (multiplicative changes) |
| **Step/mutation** | Step halving: `step /= 2`, min 0.02/0.1 | Adaptive σ: ×1.2 success, ×0.8 failure (1/5 rule) |
| **Fitness** | Binary accept/reject within window | Lexicographic: weight accuracy first, then time |
| **Overshoot handling** | Track % overthrow, max 6.67% | Track + 2× penalty in fitness function |
| **Tuning phases** | 4 sub-phases: coarse Kp → Kd → fine Kp → Kd | 2 stages: coarse (Kp+Kd jointly) → fine (Kp+Kd jointly) |
| **Max trials** | 30 drops per session | Configurable `max_runs` per stage (default ~15) |
| **Convergence** | Fixed min step size → done | Adaptive σ + weight/time tolerance |
| **Dispense control** | PD: `Kp*error + Kd*derivative` | PD: `Kp*error + Kd*derivative` (identical) |
| **Learning** | ML history (10 drops) → Kp/Kd suggestions | Flow model: 12 speed bins, quality-weighted EMA |
| **Data filtering** | None | Spin-up, non-steady, braking filters + quality score |
| **Persistence** | EEPROM | NVS flash (8 profiles) |
| **REST API** | 7 endpoints | Similar + trials endpoint |
| **Thread safety** | FreeRTOS recursive mutex | FreeRTOS dedicated task |

---

## 1. Tuning Algorithm

### RP2040: Adaptive Binary Step Search

- Deterministic bisection algorithm
- Tunes **Kp and Kd sequentially** (one at a time, 4 sub-phases total)
- Each iteration halves the step size until convergence
- Acceptance is binary: weight falls within a fixed window or it doesn't
- Convergence guaranteed but slow (many drops needed for tight windows)

```
Coarse Kp → Coarse Kd → Fine Kp → Fine Kd
Each: step = (max - min) / 2
      if weight in window → next phase
      if overshoot → decrease param
      if undershoot → increase param
      step /= 2
```

**Parameter ranges:**
- Coarse Kp: 0.01–1.0, Kd: 0.01–2.0
- Fine Kp: 0.01–5.0, Kd: 0.01–20.0

**Acceptance criteria:**
- Coarse Kp: within 1.5% of upper target
- Coarse Kd: drop ≤ target − (threshold × 0.02)
- Fine Kp: within 0.3% of target
- Fine Kd: ±0.05 grains (noise margin)

### ESP32-S3: (1+1)-ES Evolutionary Strategy

- Stochastic optimizer with adaptive mutation
- Tunes **Kp and Kd jointly** within each stage (2 stages total)
- Works in log-space — multiplicative perturbations natural for control gains
- Lexicographic fitness: weight accuracy dominates, time breaks ties
- Overshoot penalized 2× in fitness (asymmetric cost)

```
Coarse (Kp+Kd together) → Fine (Kp+Kd together)
Each: candidate = parent ± σ (in log-space)
      if fitness(candidate) > fitness(parent) → accept
      σ *= 1.2 on success, σ *= 0.8 on failure
```

**Initial σ:** ~30% change per step (log(1.3) ≈ 0.26)
**σ bounds:** min ~5% change (log(1.05) ≈ 0.05), no upper limit

### Key Difference

RP2040 tunes parameters **independently** — it cannot discover Kp/Kd interaction effects. ESP32-S3 tunes them **jointly**, which better handles coupled dynamics (e.g., high Kp needing higher Kd for stability).

---

## 2. Fitness vs Acceptance

### RP2040: Binary Window

```
if (final_weight >= lower_bound && final_weight <= upper_bound):
    ACCEPT → move to next phase
else:
    REJECT → adjust parameter, halve step
```

Finds the **first acceptable** set of parameters, not the **best**.

### ESP32-S3: Continuous Lexicographic Fitness

```
weight_error = |target - settled_weight|
if overshoot: weight_error *= 2.0  // penalize overshoot
time_error = |target_time - elapsed|
fitness = -(weight_error * 1000 + time_error)  // weight dominates
```

Selects the **best** parameters across all trials, optimizing for both accuracy and speed.

---

## 3. Flow Model vs ML History

### RP2040: Simple ML History

- Stores last 10 drops in EEPROM
- After ≥3 drops, calculates average Kp/Kd
- If avg overthrow > noise_margin → increase Kd
- If avg overthrow < -noise_margin → increase Kp
- Suggests starting values for next tuning session (`ai_tuning_get_suggestions()`)

**Strength:** Provides a warm-start for repeat tuning sessions.

### ESP32-S3: Full Flow Model

- 12 fixed speed bins per motor (coarse + fine)
- Builds piecewise-linear lookup: speed (RPS) → flow rate (gn/s)
- Quality-weighted EMA merging (noisy dispenses barely affect model)
- Extended filters: spin-up rejection (100ms), non-steady, braking
- Tracks transport delay and inertia factor per motor
- Per-bin confidence gating (≥3 bins with ≥5 samples = trusted)
- Persisted in NVS per profile

**Strength:** Deep understanding of motor dynamics, enabling future feedforward control and analytical PID tuning.

### Key Difference

RP2040 learns **what PID values worked** (parameter-level).
ESP32-S3 learns **how the motor-powder system behaves** (physics-level).

---

## 4. Motor Isolation During Tuning

Both projects isolate motors during tuning:

| | RP2040 | ESP32-S3 |
|--|--------|----------|
| Phase 1 | Coarse only, fine OFF | Coarse only, fine OFF |
| Phase 2 | Fine only, coarse OFF | Coarse as prefill (best params), then fine only |

ESP32-S3 uses the **tuned coarse parameters** as a prefill step before fine tuning, which is more realistic since the fine motor always operates after coarse in production.

---

## 5. Settlement and Stability Detection

### RP2040
- Basic wait after motor stop
- No explicit noise quantification

### ESP32-S3
- Collects 10 post-stop samples
- Requires SD < 0.015g over settling window
- Minimum 2000ms settle time
- Tracks peak weight for overshoot measurement
- Quality score (0–1) based on rejection ratio and settling noise RMS

---

## 6. State Machine

### RP2040
```
AI_TUNING_IDLE
  → AI_TUNING_PHASE_1_COARSE (Kp → Kd)
  → AI_TUNING_PHASE_2_FINE (Kp → Kd)
  → AI_TUNING_COMPLETE | AI_TUNING_ERROR
```

### ESP32-S3
```
AUTOTUNE_STATE_IDLE
  → AUTOTUNE_STATE_RUNNING
      stage: COARSE → FINE
      substatus: IDLE → DISPENSING → STABILIZING → REMOVE_CUP → RETURN_CUP
  → AUTOTUNE_STATE_DONE | AUTOTUNE_STATE_ERROR
```

ESP32-S3 has a richer substatus model, enabling better UI feedback.

---

## 7. REST API

### RP2040
```
POST /rest/ai_tuning_start       - Start tuning for a profile
GET  /rest/ai_tuning_status      - Current state and progress
POST /rest/ai_tuning_apply       - Apply recommended parameters
POST /rest/ai_tuning_cancel      - Cancel in progress
GET  /rest/ai_tuning_config      - Get configuration
POST /rest/ai_tuning_config_set  - Update configuration
GET  /rest/ai_tuning_history     - ML history
```

### ESP32-S3
```
GET/POST /rest/autotune_coarse   - Start/cancel/poll status (a0=1 start, a0=0 status, ca=1 cancel)
GET      /rest/autotune_trials   - Full trial history with per-trial telemetry
```

RP2040 has more granular endpoints; ESP32-S3 uses a single multiplexed endpoint with query parameters.

---

## 8. Configuration

### RP2040 (EEPROM)
```c
coarse_kp_min/max: 0.01 / 1.0
coarse_kd_min/max: 0.01 / 2.0
fine_kp_min/max:   0.01 / 5.0
fine_kd_min/max:   0.01 / 20.0
noise_margin:      0.05 grains
max_overthrow_pct: 6.67%
coarse_time_limit: 10s
total_time_limit:  15s
```

### ESP32-S3 (via REST request)
```c
coarse_target_weight / fine_target_weight
coarse_target_time   / fine_target_time
max_runs             (per stage)
weight_tolerance     (grains)
time_tolerance       (seconds)
auto_apply           (write to profile)
save_to_nvs          (persist)
```

ESP32-S3 is more flexible — tolerances and targets are per-session, not global constants.

---

## 9. Strengths and Gaps

### RP2040 Strengths (things ESP32-S3 could adopt)
1. **ML warm-start suggestions** — `ai_tuning_get_suggestions()` provides a better starting point for repeat tuning based on historical performance. ESP32-S3 always starts from current profile values.
2. **Configurable parameter ranges** — RP2040 allows min/max Kp/Kd to be set per config. ESP32-S3 uses ±10× base values implicitly.
3. **Separate Kp/Kd phases** — while less optimal for coupled parameters, it provides clearer diagnostics about which parameter is problematic.

### ESP32-S3 Strengths
1. **Evolutionary optimizer** — handles parameter coupling, finds global optimum not just first acceptable.
2. **Log-space search** — natural for gain parameters spanning orders of magnitude.
3. **Continuous fitness with overshoot penalty** — optimizes for best result, not just acceptable.
4. **Flow model** — physics-based learning enables future feedforward and analytical tuning.
5. **Quality-weighted learning** — noisy data doesn't corrupt the model.
6. **Rich telemetry** — full trial history with per-trial metrics.

### Potential Improvements for ESP32-S3
1. **Warm-start from flow model** — use flow model slope to compute initial Kp analytically (Phase 3 in plan).
2. **ML suggestions** — adapt RP2040's history-based starting point idea, but use flow model data instead of raw drop averages.
3. **Auto max_speed limit** — Phase 2d (planned, not yet implemented): reduce max_speed on repeated overshoots.
4. **Configurable search bounds** — expose Kp/Kd min/max ranges via REST API for advanced users.

---

## 10. Source Files

### RP2040
| File | Lines | Purpose |
|------|-------|---------|
| `src/ai_tuning.h` | — | Data structures, API |
| `src/ai_tuning.c` | ~863 | Core binary search algorithm |
| `src/rest_ai_tuning.c` | — | REST API handlers |
| `src/charge_mode.cpp` | — | Integration with charge loop |

### ESP32-S3
| File | Lines | Purpose |
|------|-------|---------|
| `components/autotune/autotune.c` | ~978 | (1+1)-ES algorithm |
| `components/autotune/include/autotune.h` | — | Data structures, API |
| `components/charge_mode/charge_mode.c` | ~705 | PD control loop |
| `components/flow_model/flow_model.c` | ~552 | Self-learning flow model |
| `components/rest_handlers/rest_handlers.c` | — | REST endpoints |
| `docs/flow_model_plan.md` | — | Design doc and phase plan |
