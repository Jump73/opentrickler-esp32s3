# TODO — OpenTrickler ESP32-S3

Last updated: 2026-03-02

## Priority 1 — Flow Model / Self-learning — DONE 2026-03-02

### A1. Add freeze/unfreeze API to flow_model ✓
- `flow_model_freeze()`, `flow_model_unfreeze()`, `flow_model_shadow_merge()` implemented.
- `analyze_and_update` redirects writes to shadow when frozen; NVS save skipped during freeze.
- Files: `components/flow_model/flow_model.c`, `components/flow_model/include/flow_model.h`

### A2. Call freeze/unfreeze from autotune ✓
- `flow_model_freeze(profile_idx)` called at autotune task start.
- `flow_model_shadow_merge()` called on successful completion.
- `flow_model_unfreeze()` called on coarse/fine error exit and in `autotune_cancel()`.
- File: `components/autotune/autotune.c`

### A3. Activate predictive fine cutoff in charge_mode (behind trust gate) ✓
- Dynamic `fine_stop` computed from `fine_stop_threshold + inertia_overshoot_gn` when model trusted.
- Capped at `FINE_TRICKLE_THRESHOLD_GN * 0.5` (0.15 gn) to prevent over-anticipation.
- Falls back to configured threshold when model is not trusted or inertia is zero.
- File: `components/charge_mode/charge_mode.c` (fine stop condition)

---

## Priority 2 — Autotune (scoring quality)

### B1. Asymmetric overshoot penalty in scoring
- Current scorer treats overshoot and undershoot symmetrically (`abs_werr`)
- Penalize overshoot stronger than undershoot (e.g. 3× factor) in `is_better_than_current`
- File: `components/autotune/autotune.c` (around lines 826 / 883)

### B2. Improve winner verification after SPEED_PROBE
- After a faster candidate is found in SPEED_PROBE, the confirmation count (2-3 runs) may be insufficient if the candidate has intermittent overshoots
- Consider adding an explicit check that the candidate produced no overshoot above guard threshold across all confirmation runs
- File: `components/autotune/autotune.c` (SPEED_PROBE → CONFIRM transition)

---

## Priority 3 — Infrastructure / Security

### C1. Parse POST body in http_server_ot.c
- `components/http_server/http_server_ot.c` line 112: `// TODO: Parse POST data into params/values`
- POST body is received and logged but never parsed into `param_names` / `param_values`
- Without this, no REST endpoint works via POST (only GET query string works)
- File: `components/http_server/http_server_ot.c` (`universal_handler`)

### C2. Remove WiFi password from logs
- `components/wifi_manager/wifi_manager.c` lines 107 and 177: `ESP_LOGI(TAG, "  Password: %s", password)`
- AP password logged in plain text on every start
- Replace with `"  Password: ***"` or demote to `ESP_LOGD`
- File: `components/wifi_manager/wifi_manager.c`

---

## Priority 4 — Hardware completeness (optional / future)

### D1. Servo Gate driver
- Pins defined: `SERVO0_PWM_PIN = GPIO8`, `SERVO1_PWM_PIN = GPIO9` in `components/board_expansion/include/board_pins.h`
- No component, no MCPWM/LEDC init, no REST endpoint
- Scope: new component `components/servo_gate/`

### D2. Sartorius scale parser
- Enum `SCALE_DRIVER_SARTORIUS = 7` exists in `components/scale/include/scale.h`
- `scale.c` line 483 has a case with no implementation
- Add frame format definition and parser matching the other drivers
- File: `components/scale/scale.c`

---

## Priority 5 — Documentation consistency

### E1. Fix field name mismatch in hardware_test_plan.md
- `docs/hardware_test_plan.md` line 48 references `coarse_best_abs_err` / `fine_best_abs_err` in the REST status response
- Actual `autotune_status_t` struct uses `coarse_best_weight_error` / `fine_best_weight_error`
- Either update the doc to match the struct, or align the REST JSON key names

### E2. Update flow_model_plan.md after A1 is implemented
- Move freeze/unfreeze from "Not implemented yet" to "Implemented" section
- File: `docs/flow_model_plan.md`

---

## Implementation order

A1 → A2 → C1 → C2 → A3 → B1 → B2 → D1 / D2

## Status summary

| Area | Status | Key gaps |
|---|---|---|
| Motor control (MCPWM) | complete | — |
| Charge mode PD | complete | predictive cutoff (A3) |
| Flow model recording | active | freeze API (A1, A2), predictive use (A3) |
| Autotune optimizer | working | freeze integration (A2), asymmetric scoring (B1) |
| REST API | working | POST body parsing (C1) |
| Web UI | complete | — |
| Security | warning | password in logs (C2) |
| Servo Gate | missing | full driver (D1) |
| Sartorius scale | missing | parser (D2) |
