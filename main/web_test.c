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

// Include generated HTML headers
#include "generated/web_portal.html.h"
#include "generated/wizard.html.h"
#include "generated/display_mirror.html.h"

static const char *TAG = "WebTest";

// Example REST handler - returns JSON (without HTTP headers)
static char* rest_test_handler(int num_params, char *params[], char *values[])
{
    ESP_LOGI(TAG, "REST handler called with %d params", num_params);
    for (int i = 0; i < num_params; i++) {
        ESP_LOGI(TAG, "  Param %d: %s = %s", i, params[i], values[i]);
    }

    // Return JSON only (http_server_ot adds HTTP headers automatically)
    static char response[] = "{\"status\":\"ok\",\"device\":\"OpenTrickler-ESP32S3\"}";
    return response;
}

// Smart root handler - returns wizard in AP mode, portal in STA mode
static char* root_page_handler(int num_params, char *params[], char *values[])
{
    // Check if we are actually connected to WiFi
    if (wifi_manager_is_connected()) {
        // Connected to WiFi (STA mode) - show portal
        ESP_LOGI(TAG, "Root request - returning portal (connected to WiFi)");
        return (char*)html_web_portal_html;
    } else {
        // Not connected (AP mode) - show wizard
        ESP_LOGI(TAG, "Root request - returning wizard (AP mode)");
        return (char*)html_wizard_html;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "=== OpenTrickler ESP32-S3 Web UI Test ===");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    // Small delay to let serial monitor connect
    vTaskDelay(pdMS_TO_TICKS(1000));

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

    ESP_LOGI(TAG, "Step 7: Initializing NeoPixel LED...");
    ret = neopixel_led_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NeoPixel LED init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Step 8: Initializing system control...");
    ret = system_control_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "System control init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Step 9: Auto-starting WiFi (STA or AP)...");
    ret = wifi_manager_auto_start("OpenTrickler-ESP32", "opentrickler");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
        return;
    }

    // Wait a bit for WiFi to stabilize
    ESP_LOGI(TAG, "Step 10: Waiting for WiFi to stabilize...");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Initialize HTTP server
    ESP_LOGI(TAG, "Step 11: Starting HTTP server...");
    ret = http_server_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server init failed: %s", esp_err_to_name(ret));
        return;
    }

    // Register HTML pages
    ESP_LOGI(TAG, "Step 12: Registering web pages...");
    // Smart root handler - shows wizard in AP mode, portal in STA mode
    http_server_register_rest_handler("/", root_page_handler);
    http_server_register_page_handler("/wizard", html_wizard_html);
    http_server_register_page_handler("/portal", html_web_portal_html);
    http_server_register_page_handler("/mobile", html_web_portal_html);
    http_server_register_page_handler("/display_mirror", html_display_mirror_html);

    // Register REST endpoints
    ESP_LOGI(TAG, "Step 13: Registering REST endpoints...");
    http_server_register_rest_handler("/rest/test", rest_test_handler);
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
    ESP_LOGI(TAG, "Web UI ready in AP MODE!");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Connect to WiFi:");
    ESP_LOGI(TAG, "  SSID: OpenTrickler-ESP32");
    ESP_LOGI(TAG, "  Password: opentrickler");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Then open browser and go to:");
    ESP_LOGI(TAG, "  http://%s/        <- Smart: Wizard (AP) or Portal (STA)", wifi_manager_get_ip());
    ESP_LOGI(TAG, "  http://%s/wizard  <- Setup Wizard (works offline)", wifi_manager_get_ip());
    ESP_LOGI(TAG, "  http://%s/portal  <- Full Web Portal (needs internet for CSS)", wifi_manager_get_ip());
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "REST API test:");
    ESP_LOGI(TAG, "  http://%s/rest/test", wifi_manager_get_ip());
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "");

    // Main loop - keep running
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "WiFi AP running, connected: %d", wifi_manager_is_connected());
    }
}
