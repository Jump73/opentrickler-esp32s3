#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "wifi_manager.h"
#include "http_server_ot.h"
#include "rest_handlers.h"
#include "motors.h"
#include "scale.h"
#include "charge_mode.h"
#include "profile.h"
#include "cleanup_mode.h"
#include "neopixel_led.h"
#include "system_control.h"

// Display and LVGL
#include "ot_pins.h"
#include "display_st7567.h"
#include "lvgl_port.h"
#include "ui_screens.h"

// Include generated HTML headers
#include "generated/web_portal.html.h"
#include "generated/wizard.html.h"
#include "generated/display_mirror.html.h"

static const char *TAG = "OpenTrickler";

// ST7567 display instance
static st7567_t s_lcd;

// UI update task - periodically refreshes active screen data
static void ui_update_task(void *arg)
{
    ESP_LOGI(TAG, "UI update task started");
    while (1) {
        if (lvgl_port_lock(100)) {
            ui_screens_update();
            lvgl_port_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(10));  // 100 Hz update rate
    }
}

// Smart root handler - returns wizard in AP mode, portal in STA mode
static char* root_page_handler(int num_params, char *params[], char *values[])
{
    // AP mode → wizard, STA mode → portal
    if (wifi_manager_is_ap_mode()) {
        ESP_LOGI(TAG, "Root request - returning wizard (AP mode)");
        return (char*)html_wizard_html;
    } else {
        ESP_LOGI(TAG, "Root request - returning portal (STA mode)");
        return (char*)html_web_portal_html;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "=== OpenTrickler ESP32-S3 Controller ===");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    ESP_LOGI(TAG, "Step 1: Initializing WiFi...");
    esp_err_t ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Step 2: Initializing motors...");
    ret = motors_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Motors init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Step 3: Initializing scale...");
    ret = scale_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scale init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Step 4: Initializing charge mode...");
    ret = charge_mode_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Charge mode init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Step 5: Initializing profile system...");
    ret = profile_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Profile init failed: %s", esp_err_to_name(ret));
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

    // Initialize display, NeoPixel backlight, and LVGL
    ESP_LOGI(TAG, "Step 8: Initializing display (UC1701 via SPI)...");
    {
        st7567_bus_t bus = {
            .host = SPI3_HOST,
            .gpio_sck = LCD_SCK,     // GPIO1
            .gpio_mosi = LCD_MOSI,   // GPIO2
            .gpio_cs = LCD_CS,       // GPIO41
            .gpio_a0 = LCD_A0,       // GPIO4
            .gpio_rst = LCD_RST,     // GPIO5
            .clk_hz = 4000000,       // 4 MHz
        };
        ret = st7567_init(&s_lcd, &bus);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ST7567 init failed: %s", esp_err_to_name(ret));
            // Continue without display
        }
    }

    if (ret == ESP_OK) {
        // Wake display from power-save (init ends with display OFF per u8g2 convention)
        st7567_power_save(&s_lcd, false);

    }

    // Step 9: NeoPixel backlight (GPIO38, separate from LCD_RST=GPIO5)
    ESP_LOGI(TAG, "Step 9: Initializing NeoPixel backlight on GPIO%d...", NEOPIXEL);
    ret = neopixel_led_init(NEOPIXEL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NeoPixel init failed: %s (backlight will be off)", esp_err_to_name(ret));
        // Don't return - display can work without backlight
    }

    // Step 9b: External PWM3 LED (GPIO9) - mirrors LED1 colour
    ESP_LOGI(TAG, "Step 9b: Initializing PWM3 external LED on GPIO%d...", NEOPIXEL_PWM3);
    ret = neopixel_pwm3_init(NEOPIXEL_PWM3);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PWM3 LED init failed: %s (external LED unavailable)", esp_err_to_name(ret));
    }

    // Step 10: LVGL port + UI
    ESP_LOGI(TAG, "Step 10: Initializing LVGL...");
    {
        encoder_cfg_t enc_cfg = {
            .gpio_a = BTN_EN1,    // GPIO40
            .gpio_b = BTN_EN2,    // GPIO39
            .gpio_btn = ENC_BTN,  // GPIO6
            .pullups = true,
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
            ESP_LOGI(TAG, "Display + LVGL initialized OK");
        } else {
            ESP_LOGE(TAG, "LVGL port init failed: %s", esp_err_to_name(ret));
        }
    }

    ESP_LOGI(TAG, "Step 11: Auto-starting WiFi (STA or AP)...");
    ret = wifi_manager_auto_start("OpenTrickler-ESP32", "opentrickler");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
        return;
    }

    // Wait a bit for WiFi to stabilize
    ESP_LOGI(TAG, "Step 12: Waiting for WiFi to stabilize...");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Initialize HTTP server
    ESP_LOGI(TAG, "Step 13: Starting HTTP server...");
    ret = http_server_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server init failed: %s", esp_err_to_name(ret));
        return;
    }

    // Register HTML pages
    ESP_LOGI(TAG, "Step 14: Registering web pages...");
    // Smart root handler - shows wizard in AP mode, portal in STA mode
    http_server_register_rest_handler("/", root_page_handler);
    http_server_register_page_handler("/wizard", html_wizard_html);
    http_server_register_page_handler("/portal", html_web_portal_html);
    http_server_register_page_handler("/mobile", html_web_portal_html);
    http_server_register_page_handler("/display_mirror", html_display_mirror_html);

    // Register REST endpoints
    ESP_LOGI(TAG, "Step 15: Registering REST endpoints...");
    http_server_register_rest_handler("/rest/wireless_config", rest_wireless_config_handler);
    http_server_register_rest_handler("/rest/system_control", rest_system_control_handler);
    http_server_register_rest_handler("/rest/coarse_motor_config", rest_coarse_motor_config_handler);
    http_server_register_rest_handler("/rest/fine_motor_config", rest_fine_motor_config_handler);
    http_server_register_rest_handler("/rest/scale_config", rest_scale_config_handler);
    http_server_register_rest_handler("/rest/scale_action", rest_scale_action_handler);
    http_server_register_rest_handler("/rest/charge_mode_config", rest_charge_mode_config_handler);
    http_server_register_rest_handler("/rest/charge_mode_state", rest_charge_mode_state_handler);
    http_server_register_rest_handler("/rest/profile_config", rest_profile_config_handler);
    http_server_register_rest_handler("/rest/profile_summary", rest_profile_summary_handler);
    http_server_register_rest_handler("/rest/cleanup_mode_state", rest_cleanup_mode_state_handler);
    http_server_register_rest_handler("/rest/neopixel_led_config", rest_neopixel_led_config_handler);

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "OpenTrickler ESP32-S3 ready!");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Connect to WiFi:");
    ESP_LOGI(TAG, "  SSID: OpenTrickler-ESP32");
    ESP_LOGI(TAG, "  Password: opentrickler");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Then open browser:");
    ESP_LOGI(TAG, "  http://%s/", wifi_manager_get_ip());
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "");

    // Start UI update task (periodically refreshes charge mode display)
    xTaskCreate(ui_update_task, "ui_upd", 4096, NULL, 3, NULL);

    // Main loop - keep running
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "System running, WiFi connected: %d", wifi_manager_is_connected());
    }
}
