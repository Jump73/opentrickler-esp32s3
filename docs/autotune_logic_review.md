# Autotune Logic Review (Current Snapshot)

Last verified against code: 2026-02-28

## Current behavior in firmware

1. Autotune stages:
- `COARSE` then `FINE`.
- Per-stage phases: `SEARCH`, `CONFIRM`, `SPEED_PROBE`.

2. Fine approach near target:
- Fine motor enters fixed min-speed trickle mode close to target.
- This is fixed-speed trickle, not proportional trickle.

3. Stop thresholds:
- Charge mode: uses config stop thresholds.
- Autotune: uses request stop thresholds (`a10`, `a11`) with defaults.
- No dynamic stop-threshold logic is active in either path at this time.

4. Flow model learning:
- Active in charge mode and autotune.
- Model freeze/shadow merge around autotune is still TODO.

## Web UI to REST mapping (autotune start)

`/rest/autotune_coarse` query params:
- `a0`: start
- `a1`: coarse target weight
- `a2`: total target time
- `a3`: fine target weight
- `a5`: max runs per stage
- `a6`: coarse weight tolerance
- `a7`: time tolerance
- `a9`: fine weight tolerance
- `a10`: fine stop threshold
- `a11`: coarse stop threshold
- `a8`: auto-apply compatibility flag
- `ee`: save-to-NVS compatibility flag

Control params:
- `ca`: cancel
- `fn`: finish now

## Open technical gaps (confirmed)

- HTTP POST body parsing in `http_server_ot.c` is still TODO (payload logged only).
- WiFi manager still logs AP password in plain text.

