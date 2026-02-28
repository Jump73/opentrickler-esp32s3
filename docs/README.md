# Documentation Index

Last verified against code: 2026-02-28

## Decision: one document or separate documents?

Do not merge all `docs/` files into one document.

Reason:
- The repository needs three different documentation types: current status, implementation plan, and test procedure.
- A single file would mix stable reference content with fast-changing work notes and become harder to maintain.

What was merged:
- Overlapping autotune/flow-model notes were reduced and aligned to one source of truth in `flow_model_plan.md`.

What stays separate:
- `flow_model_plan.md`: current implementation status + roadmap.
- `hardware_test_plan.md`: executable hardware test checklist.
- `autotune_comparison_rp2040_vs_esp32s3.md`: architecture comparison with original project.
- `motor_control_comparison_original_vs_esp32_port.md`: low-level motor control delta review.
- `flow_model_autotune_control_recommendations.md`: prioritized next improvements.
- `autotune_logic_review.md`: short status snapshot for UI/REST/control mapping.

## Notes

- `local_ui_lvgl_tree_and_screens.pdf` is intentionally separate (UI tree/screens asset).
- Historical claims that no longer match code were removed or updated.

