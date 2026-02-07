#include "neopixel_led.h"
#include "charge_mode.h"  // For hex_string_to_decimal
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "NeoPixelLED";

#define NVS_NAMESPACE "neopixel_led"
#define NVS_KEY_CONFIG "config"
#define NEOPIXEL_DATA_REV 1

// Default configuration
static neopixel_led_config_t neopixel_config = {
    .neopixel_data_rev = NEOPIXEL_DATA_REV,
    .default_led_colours = {
        .led1_colour = RGB_COLOUR_WHITE,
        .led2_colour = RGB_COLOUR_WHITE,
        .mini12864_backlight_colour = RGB_COLOUR_DULL_WHITE,
    },
    .pwm_out_led_chain_count = NEOPIXEL_LED_CHAIN_COUNT_1,
    .pwm_out_led_colour_order = NEOPIXEL_COLOUR_ORDER_GRB,
    .pwm_out_led_is_rgbw = false,
};

esp_err_t neopixel_led_init(void)
{
    ESP_LOGI(TAG, "Initializing NeoPixel LED module");

    // Try to load saved configuration
    neopixel_led_config_t temp_config;
    if (neopixel_led_load_config(&temp_config) == ESP_OK) {
        neopixel_config = temp_config;
        ESP_LOGI(TAG, "Loaded NeoPixel config from NVS");
    } else {
        ESP_LOGI(TAG, "Using default NeoPixel config");
    }

    ESP_LOGI(TAG, "LED1: #%06lx, LED2: #%06lx, Backlight: #%06lx",
             neopixel_config.default_led_colours.led1_colour,
             neopixel_config.default_led_colours.led2_colour,
             neopixel_config.default_led_colours.mini12864_backlight_colour);

    return ESP_OK;
}

esp_err_t neopixel_led_save_config(const neopixel_led_config_t *config)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ESP_LOGI(TAG, "Saving NeoPixel config to NVS");

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }

    // Save config with version
    neopixel_led_config_t config_to_save = *config;
    config_to_save.neopixel_data_rev = NEOPIXEL_DATA_REV;

    ret = nvs_set_blob(nvs_handle, NVS_KEY_CONFIG, &config_to_save, sizeof(neopixel_led_config_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error saving config: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }

    ret = nvs_commit(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "NeoPixel config saved successfully");
        // Update local copy
        neopixel_config = config_to_save;
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t neopixel_led_load_config(neopixel_led_config_t *config)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No saved NeoPixel config found");
        return ret;
    }

    size_t required_size = sizeof(neopixel_led_config_t);
    ret = nvs_get_blob(nvs_handle, NVS_KEY_CONFIG, config, &required_size);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Loaded NeoPixel config from NVS");
    } else {
        ESP_LOGW(TAG, "Failed to load config: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t neopixel_led_get_config(neopixel_led_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    *config = neopixel_config;
    return ESP_OK;
}

esp_err_t neopixel_led_set_colour(uint32_t mini12864_backlight, uint32_t led1, uint32_t led2)
{
    ESP_LOGI(TAG, "Setting LED colors (stub): BL=#%06lx, L1=#%06lx, L2=#%06lx",
             mini12864_backlight, led1, led2);

    // TODO: Implement with led_strip library
    // For now, just log the color change

    return ESP_OK;
}
