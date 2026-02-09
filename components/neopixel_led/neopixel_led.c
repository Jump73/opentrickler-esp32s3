#include "neopixel_led.h"
#include "charge_mode.h"  // For hex_string_to_decimal
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "led_strip.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "NeoPixelLED";

#define NVS_NAMESPACE "neopixel_led"
#define NVS_KEY_CONFIG "config"
#define NEOPIXEL_DATA_REV 1

// LED strip handle
static led_strip_handle_t s_led_strip = NULL;

// Default configuration
static neopixel_led_config_t neopixel_config = {
    .neopixel_data_rev = NEOPIXEL_DATA_REV,
    .default_led_colours = {
        .led1_colour = RGB_COLOUR_WHITE,
        .led2_colour = RGB_COLOUR_WHITE,
        .mini12864_backlight_colour = RGB_COLOUR_WHITE,
    },
    .pwm_out_led_chain_count = NEOPIXEL_LED_CHAIN_COUNT_1,
    .pwm_out_led_colour_order = NEOPIXEL_COLOUR_ORDER_GRB,
    .pwm_out_led_is_rgbw = false,
};

// Helper: extract R, G, B from uint32_t 0xRRGGBB
static void colour_to_rgb(uint32_t colour, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = (colour >> 16) & 0xFF;
    *g = (colour >> 8)  & 0xFF;
    *b = colour & 0xFF;
}

esp_err_t neopixel_led_init(int gpio_num)
{
    ESP_LOGI(TAG, "Initializing NeoPixel LED on GPIO%d", gpio_num);

    // Load saved configuration from NVS
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

    // Configure LED strip (WS2812, GRB color order)
    led_strip_config_t strip_config = {
        .strip_gpio_num = gpio_num,
        .max_leds = NEOPIXEL_BACKLIGHT_COUNT,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,  // 10 MHz RMT resolution
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "LED strip initialized on GPIO%d (%d LEDs)", gpio_num, NEOPIXEL_BACKLIGHT_COUNT);

    // Set initial backlight color from config
    ret = neopixel_led_set_colour(
        neopixel_config.default_led_colours.mini12864_backlight_colour,
        neopixel_config.default_led_colours.led1_colour,
        neopixel_config.default_led_colours.led2_colour
    );

    return ret;
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
    if (!s_led_strip) {
        ESP_LOGW(TAG, "LED strip not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t r, g, b;

    // Set all 3 backlight LEDs to the same color
    colour_to_rgb(mini12864_backlight, &r, &g, &b);
    for (int i = 0; i < NEOPIXEL_BACKLIGHT_COUNT; i++) {
        led_strip_set_pixel(s_led_strip, i, r, g, b);
    }

    // Refresh to push data to the strip
    esp_err_t ret = led_strip_refresh(s_led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LED strip refresh failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Backlight set to #%06lx (R=%d G=%d B=%d)", mini12864_backlight, r, g, b);
    }

    return ret;
}
