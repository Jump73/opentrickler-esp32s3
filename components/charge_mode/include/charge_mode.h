#ifndef CHARGE_MODE_H_
#define CHARGE_MODE_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Decimal places enum for weight display
typedef enum {
    DP_2 = 0,  // 2 decimal places
    DP_3 = 1,  // 3 decimal places
} decimal_places_t;

// Charge mode states
typedef enum {
    CHARGE_MODE_EXIT = 0,
    CHARGE_MODE_WAIT_FOR_ZERO = 1,
    CHARGE_MODE_WAIT_FOR_COMPLETE = 2,
    CHARGE_MODE_WAIT_FOR_CUP_REMOVAL = 3,
    CHARGE_MODE_WAIT_FOR_CUP_RETURN = 4,
} charge_mode_state_t;

// RGB color macros (RGBW format as uint32_t)
#define RGB_COLOUR_GREEN      0x00FF00ul
#define RGB_COLOUR_YELLOW     0xFFFF00ul
#define RGB_COLOUR_RED        0xFF0000ul
#define RGB_COLOUR_BLUE       0x0000FFul
#define RGB_COLOUR_WHITE      0xFFFFFFul
#define RGB_COLOUR_DULL_WHITE 0x0F0F0Ful

// Charge mode configuration structure (stored in NVS)
typedef struct {
    uint16_t config_version;        // Config version for compatibility

    // LED colors (RGBW as uint32_t)
    uint32_t neopixel_normal_charge_colour;
    uint32_t neopixel_under_charge_colour;
    uint32_t neopixel_over_charge_colour;
    uint32_t neopixel_not_ready_colour;

    // Thresholds and margins
    float coarse_stop_threshold;
    float fine_stop_threshold;
    float set_point_sd_margin;
    float set_point_mean_margin;

    // Display settings
    decimal_places_t decimal_places;

    // Precharge settings
    bool precharge_enable;
    uint32_t precharge_time_ms;
    float precharge_speed_rps;
} charge_mode_config_t;

// Runtime state (not saved to NVS)
typedef struct {
    float target_charge_weight;
    uint32_t charge_mode_event;
    charge_mode_state_t charge_mode_state;
    float current_weight;
    char profile_name[32];
    float elapsed_time_seconds;
    float settled_weight;
    float settled_time_seconds;
} charge_mode_state_t_runtime;

// Initialize charge mode module
esp_err_t charge_mode_init(void);

// Configuration management (NVS)
esp_err_t charge_mode_save_config(const charge_mode_config_t *config);
esp_err_t charge_mode_load_config(charge_mode_config_t *config);
esp_err_t charge_mode_get_config(charge_mode_config_t *config);

// State management (stubs for now)
esp_err_t charge_mode_set_target_weight(float weight);
esp_err_t charge_mode_set_state(charge_mode_state_t state);
esp_err_t charge_mode_get_runtime_state(charge_mode_state_t_runtime *state);

// Clear charge mode events (call after REST read, like original)
void charge_mode_clear_events(void);

// Utility function to convert hex string to uint32_t color
uint32_t hex_string_to_decimal(const char *string);

#ifdef __cplusplus
}
#endif

#endif // CHARGE_MODE_H_
