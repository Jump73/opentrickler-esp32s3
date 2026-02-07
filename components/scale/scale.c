#include "scale.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "Scale";

#define NVS_NAMESPACE "scale"
#define NVS_KEY_CONFIG "config"
#define CONFIG_VERSION 1

// Default configuration
static scale_config_t scale_config = {
    .config_version = CONFIG_VERSION,
    .scale_driver = SCALE_DRIVER_AND_FXI,
    .scale_baudrate = BAUDRATE_9600
};

// Current measurement (stub)
static float current_measurement = 0.0f;

esp_err_t scale_init(void)
{
    ESP_LOGI(TAG, "Initializing scale module");

    // Try to load saved configuration
    scale_config_t temp_config;
    if (scale_load_config(&temp_config) == ESP_OK) {
        scale_config = temp_config;
        ESP_LOGI(TAG, "Loaded scale config from NVS");
    } else {
        ESP_LOGI(TAG, "Using default scale config");
    }

    ESP_LOGI(TAG, "Scale driver: %d, baudrate: %d",
             scale_config.scale_driver, scale_config.scale_baudrate);

    return ESP_OK;
}

esp_err_t scale_save_config(const scale_config_t *config)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ESP_LOGI(TAG, "Saving scale config to NVS");
    ESP_LOGI(TAG, "  Driver: %d", config->scale_driver);
    ESP_LOGI(TAG, "  Baudrate: %d", config->scale_baudrate);

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }

    // Save config with version
    scale_config_t config_to_save = *config;
    config_to_save.config_version = CONFIG_VERSION;

    ret = nvs_set_blob(nvs_handle, NVS_KEY_CONFIG, &config_to_save, sizeof(scale_config_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error saving config: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }

    ret = nvs_commit(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Scale config saved successfully");
        // Update local copy
        scale_config = config_to_save;
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t scale_load_config(scale_config_t *config)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No saved scale config found");
        return ret;
    }

    size_t required_size = sizeof(scale_config_t);
    ret = nvs_get_blob(nvs_handle, NVS_KEY_CONFIG, config, &required_size);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Loaded scale config from NVS");
    } else {
        ESP_LOGW(TAG, "Failed to load config: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t scale_get_config(scale_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    *config = scale_config;
    return ESP_OK;
}

esp_err_t scale_set_driver(scale_driver_t driver)
{
    ESP_LOGI(TAG, "Setting scale driver to: %d (stub)", driver);
    scale_config.scale_driver = driver;
    return ESP_OK;
}

esp_err_t scale_perform_action(scale_action_t action)
{
    switch (action) {
        case SCALE_ACTION_FORCE_ZERO:
            ESP_LOGI(TAG, "Performing force zero action (stub)");
            current_measurement = 0.0f;
            break;
        case SCALE_ACTION_NO_ACTION:
        default:
            ESP_LOGD(TAG, "No action requested");
            break;
    }
    return ESP_OK;
}

float scale_get_measurement(void)
{
    // Stub: return simulated measurement
    return current_measurement;
}
