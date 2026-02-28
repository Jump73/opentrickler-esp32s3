# Autotune Comparison: RP2040 vs ESP32-S3

Last verified against code: 2026-02-28

## Scope

Comparison of tuning and control behavior between:
- RP2040 original project
- ESP32-S3 port in this repository

## Key Differences

1. Tuning algorithm:
- RP2040: step-style/manual-like tuning flow.
- ESP32-S3: staged search with adaptive candidate updates and acceptance checks.

2. Stage flow:
- RP2040: coarse/fine tuning flow in original implementation style.
- ESP32-S3: `COARSE -> FINE`, each with `SEARCH -> CONFIRM -> SPEED_PROBE`.

3. Stop thresholds during autotune:
- ESP32-S3 currently uses fixed stop thresholds from request/defaults.
- Parameters: `fine_stop_threshold` (`a10`), `coarse_stop_threshold` (`a11`).

4. Trickle behavior:
- ESP32-S3 fine stage uses fixed min-speed trickle near target.
- Not proportional trickle.

5. Flow model usage:
- ESP32-S3 records and updates flow model during charge mode and autotune.
- Live-model freeze/shadow merge is still planned, not implemented.

6. REST/API shape (ESP32-S3):
- `GET/POST /rest/autotune_coarse` for start/status/cancel/finish-now.
- `GET /rest/autotune_trials` and `GET /rest/autotune_telemetry` for run data.

## Practical Conclusion

- ESP32-S3 autotune stack is already richer in telemetry and stage orchestration.
- Remaining quality gains are mainly in model isolation during autotune and predictive cutoff integration, not in endpoint structure.

