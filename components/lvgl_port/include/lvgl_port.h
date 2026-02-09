#pragma once

#include "esp_err.h"
#include "display_st7567.h"
#include "input_encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

// Configuration for LVGL port initialization
typedef struct {
    st7567_t *lcd;              // Initialized ST7567 display handle
    const encoder_cfg_t *enc;   // Encoder GPIO configuration (NULL to skip encoder init)
} lvgl_port_cfg_t;

// Initialize LVGL, display driver, encoder input, and start the LVGL task.
// Call after st7567_init() has been called on the lcd handle.
esp_err_t lvgl_port_init(const lvgl_port_cfg_t *cfg);

// Lock LVGL mutex before accessing LVGL API from non-LVGL task.
// Returns true if lock acquired within timeout_ms.
bool lvgl_port_lock(uint32_t timeout_ms);

// Unlock LVGL mutex after accessing LVGL API.
void lvgl_port_unlock(void);

#ifdef __cplusplus
}
#endif
