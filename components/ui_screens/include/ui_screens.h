#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the UI screens system (creates menu + charge mode screens).
// Must be called after lvgl_port_init() and with LVGL mutex held.
void ui_screens_init(void);

// Periodic update - refreshes active screen data.
// Call periodically (e.g. every 50ms) with LVGL mutex held.
void ui_screens_update(void);

// Switch display to charge mode screen (e.g. when REST API starts charging).
// Must be called with LVGL mutex held.
void ui_screens_enter_charge(float target_weight);

// Switch display back to main menu (e.g. when REST API exits charging).
// Must be called with LVGL mutex held.
void ui_screens_enter_main_menu(void);

#ifdef __cplusplus
}
#endif
