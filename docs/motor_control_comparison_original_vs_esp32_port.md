# Motor Control Comparison: Original vs ESP32 Port

Date: 2026-02-21

## Scope
Comparison of coarse/fine motor control between:
- Original: `C:\Users\kdzia\OpenTrickler_org-EAMARS`
- Port: `C:\Users\kdzia\ESPRESS\opentrickler-esp32s3`

Assumption: ESP32 port uses MCPWM for STEP generation (different low-level driver than RP2040/PIO).

## Key Differences

### 1. Motor-control architecture
1. Original (RP2040): `motor_set_speed()` pushes command to queue; a dedicated motor task executes ramp and direction handling.
2. Port (ESP32): `motor_set_speed()` updates MCPWM directly (no queue).
3. Effect: Port can react faster, but behavior depends more on charge-loop timing jitter.

### 2. STEP generation backend
1. Original: PIO (`speed_to_period`, FIFO push).
2. Port: MCPWM (`mcpwm_timer_set_period`, comparator at 50% duty).
3. Effect: Different low-level implementation is expected, but high-level control must remain behaviorally equivalent.

### 3. Gear ratio handling
1. Original: commanded velocity is divided by `gear_ratio` in motor task.
2. Port: `gear_ratio` is present in config but not applied in `motor_set_speed()` path.
3. Effect: Real mechanical speed can diverge from controller assumptions.

### 4. Speed ramping model
1. Original: continuous time-based ramp (`speed_ramp`) in dedicated task.
2. Port: per-call ramp using elapsed time since last call (`angular_acceleration * elapsed_s`).
3. Effect: If charge loop period varies, ramp behavior varies too.

### 5. Coarse/fine strategy in charge loop
1. Original: fine runs from start; coarse is disabled after threshold.
2. Port: coarse runs first, fine starts only after coarse stop.
3. Effect: Different dynamic response and different optimal gains.

### 6. Coarse stop criterion
1. Original: stop coarse when `error < coarse_stop_threshold` (error to target).
2. Port: sub-target logic (`target - coarse_stop_threshold`) and stop at `coarse_error <= 0.03`.
3. Effect: Coarse handoff point differs from original semantics.

### 7. Controller form
1. Original: PID with active integral term for both motors.
2. Port: effectively PD in charge loop plus fine trickle mode.
3. Effect: Different tuning surface and transient response.

### 8. Post-stop result handling
1. Original: simpler final classification path.
2. Port: extended settle/post-settle window and latching logic.
3. Effect: more timing sensitivity, more states that can affect reported final result.

## Practical Conclusion
1. Hardware-layer differences (PIO vs MCPWM) are natural and acceptable.
2. Main instability risk comes from non-equivalent high-level control logic (coarse/fine strategy, coarse stop semantics, PID vs PD, gear-ratio usage), not from MCPWM itself.

## Suggested next diagnostic focus (no code changes)
1. Verify whether charge-loop timing is sufficiently stable for direct (non-queued) motor control.
2. Verify whether `gear_ratio` should be applied in ESP32 motor command path.
3. Decide whether charge-loop strategy should match original (parallel fine+coarse) or keep staged approach and retune consistently.
