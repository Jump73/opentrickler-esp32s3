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

#ifdef __cplusplus
}
#endif
