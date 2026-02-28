# Flow Model + Autotune Control Recommendations

Last verified against code: 2026-02-28

## Baseline (what is true now)

- Flow model is updated from normal charge mode and autotune dispenses.
- Autotune is staged (`SEARCH -> CONFIRM -> SPEED_PROBE`) for both coarse and fine.
- Fine trickle is fixed min-speed near target.
- Stop thresholds are fixed parameters (config in charge mode, request in autotune).

## Recommended priorities

1. Freeze live model during autotune
- Keep live model read-only while searching gains.
- Optionally build a shadow model and merge only after acceptance gate.

2. Implement predictive fine cutoff behind trust gate
- Use model-derived flow/inertia only when `flow_model_is_trusted(...)` is true.
- Keep existing threshold path as fallback.

3. Improve autotune objective/acceptance (without changing endpoint contract)
- Keep weight accuracy dominant.
- Penalize overshoot stronger than undershoot.
- Keep time and settle noise as secondary factors.

4. Harden service/security gaps
- Parse POST body in REST wrapper (not logging only).
- Mask or remove WiFi password logging.

## Keep/avoid

Keep:
- PD structure and current staged autotune control flow.
- Manual operator control over tolerance/threshold inputs from UI.

Avoid:
- Simultaneous fast adaptation of gains and live plant model.
- Hidden automatic threshold rewrites that bypass explicit user config.

