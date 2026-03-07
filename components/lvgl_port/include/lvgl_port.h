#pragma once

#include "esp_err.h"
#include "display_st7567.h"
#include "input_encoder.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Configuration for LVGL port initialization
typedef struct {
    st7567_t *lcd;              // Initialized ST7567 display handle
    const encoder_cfg_t *enc;   // Encoder GPIO configuration (NULL to skip encoder init)
} lvgl_port_cfg_t;

// Mini 12864 module runtime configuration (persisted in NVS)
typedef struct {
    bool inverted_encoder;    // Invert rotary encoder direction
    uint8_t display_rotation; // 0 = 0°, 2 = 180° (matches lv_display_rotation_t)
} mini_12864_config_t;

// Initialize LVGL, display driver, encoder input, and start the LVGL task.
// Call after st7567_init() has been called on the lcd handle.
esp_err_t lvgl_port_init(const lvgl_port_cfg_t *cfg);

// Lock LVGL mutex before accessing LVGL API from non-LVGL task.
// Returns true if lock acquired within timeout_ms.
bool lvgl_port_lock(uint32_t timeout_ms);

// Unlock LVGL mutex after accessing LVGL API.
void lvgl_port_unlock(void);

// Get current mini 12864 config.
void lvgl_port_get_mini12864_config(mini_12864_config_t *config);

// Apply and optionally save mini 12864 config (display rotation + encoder inversion).
// If save=true the config is persisted to NVS.
esp_err_t lvgl_port_set_mini12864_config(const mini_12864_config_t *config, bool save);

#ifdef __cplusplus
}
#endif
