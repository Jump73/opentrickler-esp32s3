# Hardware Test Plan

Last verified against code: 2026-02-28

## Prerequisites

- ESP32-S3 flashed with current firmware
- Serial monitor available
- Scale connected and stable
- Coarse and fine motors connected
- Powder and cup available

## Test sequence

### 1. Boot and service startup

Check:
- `flow_model_init` runs without errors
- REST is reachable (`/rest/profile_summary`)

### 2. Charge mode baseline

Run charge cycle and verify:
- Coarse stops near configured coarse threshold
- Fine reaches target within configured fine threshold
- Post-stop settle classification is applied

### 3. Flow model recording

After several cycles verify in logs:
- start/stop/analysis called for both motors
- bins and sample counts increase
- quality and inertia values are produced

### 4. Autotune start/status

Start request example:

`GET /rest/autotune_coarse?a0=1&a1=37.0&a2=20.0&a3=40.0&a5=8&a6=1.0&a9=0.02&a7=2.0&a10=0.02&a11=0.03&a8=0&ee=0`

Poll status:

`GET /rest/autotune_coarse?a0=0`

Verify response fields:
- `state`, `stage`, `substatus`, `progress`
- `runs_done`, `runs_total`, `stage_run`, `stage_max_runs`
- `active_kp`, `active_kd`
- `coarse_best_abs_err`, `fine_best_abs_err`

### 5. Autotune control actions

- Cancel: `GET /rest/autotune_coarse?ca=1`
- Finish now: `GET /rest/autotune_coarse?fn=1`

Verify:
- cancel stops safely and returns to idle
- finish-now advances stage/ends according to current phase

### 6. Trial and telemetry endpoints

- `GET /rest/autotune_trials`
- `GET /rest/autotune_telemetry`

Verify:
- entries exist and fields are plausible
- stage/phase values match observed run progression

### 7. Post-autotune charge validation

Run 3 normal charge cycles and compare with baseline:
- final error repeatability
- overshoot frequency
- total cycle time

### 8. Optional precharge behavior

If precharge enabled in config, verify it runs only after charge completion and does not affect final classification logic.

