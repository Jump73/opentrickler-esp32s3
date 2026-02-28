# Flow Model Plan

Last verified against code: 2026-02-28

## Overview

The flow model learns motor speed to powder flow correlation from real dispenses and stores per-profile data in NVS.

Current use:
- Learning/recording is active.
- Control still relies on PD + configured/requested thresholds.
- Feedforward/predictive use is partial/planned.

## Implemented

1. Data model and persistence
- `FLOW_MODEL_VERSION = 3`
- Per-profile model (coarse + fine) in NVS namespace `flow_model`, keys `fm_0..fm_7`.

2. Recording and analysis
- Recording API used in both `charge_mode` and `autotune`.
- `FLOW_RECORD_MAX = 512` samples per run.
- Fixed speed bins (`FLOW_TABLE_POINTS = 12`) for coarse and fine.
- Quality-weighted EMA update.
- Huber-clipped residual update.
- Delay/inertia/inertia-overshoot tracking.
- Trust gate (`flow_model_is_trusted`) based on populated bins.

3. Integration
- Charge mode: records and analyzes both motor phases.
- Autotune: records and analyzes trial runs.

## Not implemented yet

1. Live-model freeze during autotune
- `flow_model_freeze/unfreeze/shadow-merge` API is not present yet.

2. Predictive stop/cutoff in charge mode
- Current charge-mode stop checks use configured thresholds.
- Dynamic stop-threshold from flow model is not active.

3. Full feedforward replacement strategy
- Model query helpers exist, but production control remains PD-dominant.

## Current control notes

- Fine trickle in charge/autotune is fixed min-speed near target.
- Autotune receives `a10` (fine stop threshold) and `a11` (coarse stop threshold).
- Autotune supports `finish now` (`fn`) and cancel (`ca`).

## Roadmap

1. Phase A (safety and isolation)
- Add model freeze + optional shadow merge for autotune sessions.

2. Phase B (precision improvement)
- Add trust-gated predictive fine cutoff with conservative fallback.

3. Phase C (objective quality)
- Refine autotune scoring/acceptance and winner verification.

4. Phase D (optional)
- Diagnostic ring buffer endpoint for per-dispense quality analysis.

## Files

Core:
- `components/flow_model/flow_model.c`
- `components/flow_model/include/flow_model.h`

Integrated with:
- `components/charge_mode/charge_mode.c`
- `components/autotune/autotune.c`
- `components/rest_handlers/rest_handlers.c`

