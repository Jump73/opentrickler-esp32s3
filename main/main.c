#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "motors.h"
#include "scale.h"
#include "charge_mode.h"
#include "profile.h"
#include "cleanup_mode.h"
#include "neopixel_led.h"
#include "system_control.h"
#include "autotune.h"
#include "flow_model.h"
#include "ble_uart.h"

// Display and LVGL
#include "ot_pins.h"
#include "display_st7567.h"
#include "lvgl_port.h"
#include "ui_screens.h"

static const char *TAG = "OpenTrickler";

// ST7567 display instance
static st7567_t s_lcd;

// ---------------------------------------------------------------------------
// BLE command handler
// Receives newline-terminated JSON from the phone, dispatches commands.
// Runs in the NimBLE host task context.
// ---------------------------------------------------------------------------
static void ble_rx_handler(const uint8_t *data, size_t len)
{
    // Echo back for now — protocol to be implemented
    ESP_LOGI(TAG, "BLE RX (%d bytes): %.*s", (int)len, (int)len, (const char *)data);
    ble_uart_send(data, len);
}

// ---------------------------------------------------------------------------
// UI update task
// ---------------------------------------------------------------------------
static void ui_update_task(void *arg)
{
    ESP_LOGI(TAG, "UI update task started");
    while (1) {
        if (lvgl_port_lock(100)) {
            ui_screens_update();
            lvgl_port_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(10));  // 100 Hz
    }
}

// ---------------------------------------------------------------------------
// app_main
// ---------------------------------------------------------------------------
void app_main(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "=== OpenTrickler ESP32-S3 (BLE only) ===");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    // NVS must be initialised before BLE (and any component that uses NVS)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Step 1: Initializing motors...");
    ret = motors_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Motors init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Step 2: Initializing scale...");
    ret = scale_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scale init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Step 3: Initializing charge mode...");
    ret = charge_mode_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Charge mode init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Step 4: Initializing profile system...");
    ret = profile_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Profile init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Step 5: Initializing flow model...");
    ret = flow_model_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Flow model init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Step 6: Initializing cleanup mode...");
    ret = cleanup_mode_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Cleanup mode init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Step 7: Initializing system control...");
    ret = system_control_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "System control init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Step 8: Initializing autotune...");
    ret = autotune_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Autotune init failed: %s", esp_err_to_name(ret));
        return;
    }

    // Display
    ESP_LOGI(TAG, "Step 9: Initializing display...");
    {
        st7567_bus_t bus = {
            .host     = SPI3_HOST,
            .gpio_sck  = LCD_SCK,   // GPIO1
            .gpio_mosi = LCD_MOSI,  // GPIO2
            .gpio_cs   = LCD_CS,    // GPIO41
            .gpio_a0   = LCD_A0,    // GPIO4
            .gpio_rst  = LCD_RST,   // GPIO5
            .clk_hz    = 4000000,
        };
        ret = st7567_init(&s_lcd, &bus);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ST7567 init failed: %s (continuing)", esp_err_to_name(ret));
        } else {
            st7567_power_save(&s_lcd, false);
        }
    }

    ESP_LOGI(TAG, "Step 10: Initializing NeoPixel backlight on GPIO%d...", NEOPIXEL);
    ret = neopixel_led_init(NEOPIXEL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NeoPixel init failed: %s (backlight off)", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Step 10b: Initializing PWM3 external LED on GPIO%d...", NEOPIXEL_PWM3);
    ret = neopixel_pwm3_init(NEOPIXEL_PWM3);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PWM3 LED init failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Step 11: Initializing LVGL...");
    {
        encoder_cfg_t enc_cfg = {
            .gpio_a   = BTN_EN1,   // GPIO40
            .gpio_b   = BTN_EN2,   // GPIO39
            .gpio_btn = ENC_BTN,   // GPIO6
            .pullups  = true,
            .position = 0,
        };
        lvgl_port_cfg_t lvgl_cfg = {
            .lcd = &s_lcd,
            .enc = &enc_cfg,
        };
        ret = lvgl_port_init(&lvgl_cfg);
        if (ret == ESP_OK) {
            if (lvgl_port_lock(1000)) {
                ui_screens_init();
                lvgl_port_unlock();
            }
            ESP_LOGI(TAG, "Display + LVGL OK");
        } else {
            ESP_LOGE(TAG, "LVGL port init failed: %s", esp_err_to_name(ret));
        }
    }

    ESP_LOGI(TAG, "Step 12: Initializing BLE (NimBLE NUS)...");
    ret = ble_uart_init("OpenTrickler", ble_rx_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE init failed: %s", esp_err_to_name(ret));
        // Continue without BLE — device still functional via display
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "OpenTrickler (BLE only) ready!");
    ESP_LOGI(TAG, "Connect via Bluetooth: \"OpenTrickler\"");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    xTaskCreate(ui_update_task, "ui_upd", 4096, NULL, 3, NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
