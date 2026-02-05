#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Safe initialization of the expansion board:
// - motors disabled (EN = HIGH)
// - GPIO3 configured as input without pull (strap-safe)
void board_expansion_safe_init(void);

// Enable or disable motors via EN pins
// Typical TMC logic: LOW = enable, HIGH = disable
void board_expansion_set_motors_enabled(int enabled);

#ifdef __cplusplus
}
#endif
