# Motor Control Comparison: Original vs ESP32 Port

Date: 2026-02-28

## Scope

Comparison of coarse/fine motor behavior between original RP2040 project and this ESP32-S3 port.

## Verified differences in current ESP32 code

1. Backend
- ESP32 uses MCPWM timer/comparator based STEP generation.

2. Command path
- `motor_set_speed()` updates motor timing directly; no queue-based motor worker is used.

3. Ramping
- Acceleration ramp uses elapsed time per call (`angular_acceleration * elapsed_s`).
- Behavior depends on caller timing stability.

4. Gear ratio
- `gear_ratio` exists in config structures but is not applied in `motor_set_speed()` conversion path.

5. Charge strategy
- Charge loop is staged (coarse then fine), not parallel-from-start behavior.

## Practical conclusion

- Hardware backend differences are expected.
- Behavior deltas mainly come from high-level strategy and control-loop timing assumptions, not MCPWM itself.

## Recommended checks

1. Measure charge-loop call-period jitter and its impact on per-call ramp behavior.
2. Decide whether `gear_ratio` should become active in the speed conversion path.
3. Keep staged strategy and tune/document around it consistently, instead of partial emulation of original flow.

