#include "charge_mode.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ChargeMode";

#define NVS_NAMESPACE "charge_mode"
#define NVS_KEY_CONFIG "config"
#define CONFIG_VERSION 1

// Default configuration
static charge_mode_config_t charge_mode_config = {
    .config_version = CONFIG_VERSION,

    // LED colors (default from original)
    .neopixel_normal_charge_colour = RGB_COLOUR_GREEN,
    .neopixel_under_charge_colour = RGB_COLOUR_YELLOW,
    .neopixel_over_charge_colour = RGB_COLOUR_RED,
    .neopixel_not_ready_colour = RGB_COLOUR_BLUE,

    // Thresholds (default from original)
    .coarse_stop_threshold = 0.5f,
    .fine_stop_threshold = 0.05f,
    .set_point_sd_margin = 0.01f,
    .set_point_mean_margin = 0.01f,

    // Display settings
    .decimal_places = DP_3,

    // Precharge settings (default from original)
    .precharge_enable = false,
    .precharge_time_ms = 200,
    .precharge_speed_rps = 2.0f
};

// Runtime state
static charge_mode_state_t_runtime runtime_state = {
    .target_charge_weight = 0.0f,
    .charge_mode_event = 0,
    .charge_mode_state = CHARGE_MODE_EXIT,
    .current_weight = 0.0f,
    .profile_name = "Default",
    .elapsed_time_seconds = 0.0f
};

esp_err_t charge_mode_init(void)
{
    ESP_LOGI(TAG, "Initializing charge mode module");

    // Try to load saved configuration
    charge_mode_config_t temp_config;
    if (charge_mode_load_config(&temp_config) == ESP_OK) {
        charge_mode_config = temp_config;
        ESP_LOGI(TAG, "Loaded charge mode config from NVS");
    } else {
        ESP_LOGI(TAG, "Using default charge mode config");
    }

    ESP_LOGI(TAG, "Coarse threshold: %.3f, Fine threshold: %.3f",
             charge_mode_config.coarse_stop_threshold,
             charge_mode_config.fine_stop_threshold);

    return ESP_OK;
}

esp_err_t charge_mode_save_config(const charge_mode_config_t *config)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ESP_LOGI(TAG, "Saving charge mode config to NVS");

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }

    // Save config with version
    charge_mode_config_t config_to_save = *config;
    config_to_save.config_version = CONFIG_VERSION;

    ret = nvs_set_blob(nvs_handle, NVS_KEY_CONFIG, &config_to_save, sizeof(charge_mode_config_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error saving config: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }

    ret = nvs_commit(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Charge mode config saved successfully");
        // Update local copy
        charge_mode_config = config_to_save;
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t charge_mode_load_config(charge_mode_config_t *config)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No saved charge mode config found");
        return ret;
    }

    size_t required_size = sizeof(charge_mode_config_t);
    ret = nvs_get_blob(nvs_handle, NVS_KEY_CONFIG, config, &required_size);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Loaded charge mode config from NVS");
    } else {
        ESP_LOGW(TAG, "Failed to load config: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t charge_mode_get_config(charge_mode_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    *config = charge_mode_config;
    return ESP_OK;
}

esp_err_t charge_mode_set_target_weight(float weight)
{
    ESP_LOGI(TAG, "Setting target weight to: %.3f (stub)", weight);
    runtime_state.target_charge_weight = weight;
    return ESP_OK;
}

esp_err_t charge_mode_set_state(charge_mode_state_t state)
{
    ESP_LOGI(TAG, "Setting charge mode state to: %d (stub)", state);

    // Handle state transitions (stub)
    if (state == CHARGE_MODE_EXIT && runtime_state.charge_mode_state != CHARGE_MODE_EXIT) {
        ESP_LOGI(TAG, "Exiting charge mode");
        runtime_state.elapsed_time_seconds = 0.0f;
    } else if (state == CHARGE_MODE_WAIT_FOR_ZERO && runtime_state.charge_mode_state == CHARGE_MODE_EXIT) {
        ESP_LOGI(TAG, "Entering charge mode");
    }

    runtime_state.charge_mode_state = state;
    return ESP_OK;
}

esp_err_t charge_mode_get_runtime_state(charge_mode_state_t_runtime *state)
{
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }

    *state = runtime_state;
    return ESP_OK;
}

uint32_t hex_string_to_decimal(const char *string)
{
    uint32_t value = 0;

    if (!string) {
        return 0;
    }

    // Handle URL-encoded # (%23)
    const char *hex_start = string;
    if (string[0] == '%' && string[1] == '2' && string[2] == '3') {
        // URL-encoded # found, skip %23
        hex_start = string + 3;
    } else if (string[0] == '#') {
        // Regular # found, skip it
        hex_start = string + 1;
    } else {
        // No # found, assume it's already just hex digits
        hex_start = string;
    }

    // Parse hex string
    value = strtol(hex_start, NULL, 16);

    return value;
}
