#include "rest_handlers.h"
#include "wifi_manager.h"
#include "motors.h"
#include "scale.h"
#include "charge_mode.h"
#include "profile.h"
#include "cleanup_mode.h"
#include "neopixel_led.h"
#include "system_control.h"
#include "autotune.h"
#include "flow_model.h"
#include "ui_screens.h"
#include "lvgl_port.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "REST_Handlers";

// Task to perform delayed reboot (allows HTTP response to be sent first)
static void delayed_reboot_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1000));  // Wait 1 second
    ESP_LOGI(TAG, "Rebooting now...");
    esp_restart();
}

// Helper: Parse boolean from string
bool string_to_boolean(const char *str)
{
    if (strcmp(str, "true") == 0 || strcmp(str, "1") == 0) {
        return true;
    }
    return false;
}

// Helper: Convert boolean to string
const char* boolean_to_string(bool value)
{
    return value ? "true" : "false";
}

// WiFi configuration REST handler
// GET /rest/wireless_config - Returns current config
// POST /rest/wireless_config?w0=ssid&w1=password&w2=auth&w3=timeout&w4=enable&ee=save
char* rest_wireless_config_handler(int num_params, char *params[], char *values[])
{
    static char wireless_config_json_buffer[512];
    wifi_config_data_t config = {0};
    bool save_to_nvs = false;
    bool config_changed = false;

    ESP_LOGI(TAG, "WiFi config request with %d params", num_params);

    // Load current config
    wifi_manager_get_config(&config);

    // If no params, just return current config
    if (num_params == 0) {
        snprintf(wireless_config_json_buffer,
                 sizeof(wireless_config_json_buffer),
                 "{\"w0\":\"%s\",\"w2\":%d,\"w3\":%lu,\"w4\":%s}",
                 config.ssid,
                 config.auth,
                 config.timeout_ms,
                 boolean_to_string(config.enable));
        return wireless_config_json_buffer;
    }

    // Parse parameters and update config
    for (int idx = 0; idx < num_params; idx++) {
        ESP_LOGI(TAG, "  Param[%d]: %s = %s", idx, params[idx], values[idx]);

        if (strcmp(params[idx], "w0") == 0) {
            // SSID
            strncpy(config.ssid, values[idx], sizeof(config.ssid) - 1);
            config.ssid[sizeof(config.ssid) - 1] = '\0';
            config_changed = true;
        }
        else if (strcmp(params[idx], "w1") == 0) {
            // Password
            strncpy(config.password, values[idx], sizeof(config.password) - 1);
            config.password[sizeof(config.password) - 1] = '\0';
            config_changed = true;
        }
        else if (strcmp(params[idx], "w2") == 0) {
            // Auth type
            config.auth = (wifi_auth_type_t)atoi(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "w3") == 0) {
            // Timeout
            config.timeout_ms = (uint32_t)atoi(values[idx]);
            if (config.timeout_ms == 0) {
                config.timeout_ms = 10000; // Default 10 seconds
            }
            config_changed = true;
        }
        else if (strcmp(params[idx], "w4") == 0) {
            // Enable
            config.enable = string_to_boolean(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "ee") == 0) {
            // Save to EEPROM/NVS
            save_to_nvs = string_to_boolean(values[idx]);
        }
    }

    // Save to NVS if requested
    if (save_to_nvs && config_changed) {
        esp_err_t ret = wifi_manager_save_config(&config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save WiFi config: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "WiFi config saved to NVS");
        }
    }

    // Return updated config (but not the password!)
    snprintf(wireless_config_json_buffer,
             sizeof(wireless_config_json_buffer),
             "{\"w0\":\"%s\",\"w2\":%d,\"w3\":%lu,\"w4\":%s,\"saved\":%s}",
             config.ssid,
             config.auth,
             config.timeout_ms,
             boolean_to_string(config.enable),
             boolean_to_string(save_to_nvs));

    return wireless_config_json_buffer;
}

// System control REST handler
// GET /rest/system_control - Returns system info
// GET /rest/system_control?s4=true - Save all NVS settings
// GET /rest/system_control?s5=true - Reboot device
// GET /rest/system_control?s6=true - Erase all NVS settings
char* rest_system_control_handler(int num_params, char *params[], char *values[])
{
    static char system_control_json_buffer[512];
    system_info_t info;
    bool save_to_nvs_flag = false;
    bool software_reset_flag = false;
    bool erase_nvs_flag = false;

    ESP_LOGI(TAG, "System control request with %d params", num_params);

    // Get system info
    system_control_get_info(&info);

    // Parse parameters
    for (int idx = 0; idx < num_params; idx++) {
        ESP_LOGI(TAG, "  Param[%d]: %s = %s", idx, params[idx], values[idx]);

        if (strcmp(params[idx], "s4") == 0) {
            // Save all NVS settings
            save_to_nvs_flag = string_to_boolean(values[idx]);
        }
        else if (strcmp(params[idx], "s5") == 0) {
            // Software reset
            software_reset_flag = string_to_boolean(values[idx]);
        }
        else if (strcmp(params[idx], "s6") == 0) {
            // Erase NVS
            erase_nvs_flag = string_to_boolean(values[idx]);
        }
    }

    // Perform actions
    if (save_to_nvs_flag) {
        ESP_LOGI(TAG, "Saving all NVS settings");
        system_control_save_all_nvs();
    }

    if (erase_nvs_flag) {
        ESP_LOGW(TAG, "Erasing all NVS settings!");
        system_control_erase_nvs(software_reset_flag);
    }

    // Prepare response first (before reboot!)
    snprintf(system_control_json_buffer,
             sizeof(system_control_json_buffer),
             "{\"s0\":\"%s\",\"s1\":\"%s\",\"s2\":\"%s\",\"s3\":\"%s\",\"s4\":%s,\"s5\":%s,\"s6\":%s}",
             info.unique_id,
             info.version_string,
             info.vcs_hash,
             info.build_type,
             boolean_to_string(save_to_nvs_flag),
             boolean_to_string(software_reset_flag),
             boolean_to_string(erase_nvs_flag));

    // Perform reboot if requested (and not already rebooting from erase)
    if (software_reset_flag && !erase_nvs_flag) {
        ESP_LOGI(TAG, "Software reset requested, rebooting in 1 second...");
        // Create a task to reboot after delay (allows HTTP response to be sent)
        xTaskCreate(delayed_reboot_task, "reboot_task", 2048, NULL, 5, NULL);
    }

    return system_control_json_buffer;
}

// Motor configuration REST handler
// Parameters: m0-m9, ee
char* rest_coarse_motor_config_handler(int num_params, char *params[], char *values[])
{
    static char motor_config_json_buffer[512];
    motor_config_t config = {0};
    bool save_to_nvs = false;
    bool config_changed = false;

    ESP_LOGI(TAG, "Coarse motor config request with %d params", num_params);

    // Load current config
    motors_get_config(MOTOR_COARSE, &config);

    // If no params, just return current config
    if (num_params == 0) {
        snprintf(motor_config_json_buffer,
                 sizeof(motor_config_json_buffer),
                 "{\"m0\":%.3f,\"m1\":%lu,\"m2\":%d,\"m3\":%d,\"m4\":%d,\"m5\":%d,\"m6\":%.3f,\"m7\":%.7f,\"m8\":%s,\"m9\":%s}",
                 config.angular_acceleration,
                 config.full_steps_per_rotation,
                 config.current_ma,
                 config.microsteps,
                 config.max_speed_rps,
                 config.r_sense,
                 config.min_speed_rps,
                 config.gear_ratio,
                 boolean_to_string(config.inverted_enable),
                 boolean_to_string(config.inverted_direction));
        return motor_config_json_buffer;
    }

    // Parse parameters and update config
    for (int idx = 0; idx < num_params; idx++) {
        ESP_LOGI(TAG, "  Param[%d]: %s = %s", idx, params[idx], values[idx]);

        if (strcmp(params[idx], "m0") == 0) {
            config.angular_acceleration = strtof(values[idx], NULL);
            config_changed = true;
        }
        else if (strcmp(params[idx], "m1") == 0) {
            config.full_steps_per_rotation = strtoul(values[idx], NULL, 10);
            config_changed = true;
        }
        else if (strcmp(params[idx], "m2") == 0) {
            config.current_ma = (uint16_t)atoi(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "m3") == 0) {
            config.microsteps = (uint16_t)atoi(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "m4") == 0) {
            config.max_speed_rps = (uint16_t)atoi(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "m5") == 0) {
            config.r_sense = (uint16_t)atoi(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "m6") == 0) {
            config.min_speed_rps = strtof(values[idx], NULL);
            config_changed = true;
        }
        else if (strcmp(params[idx], "m7") == 0) {
            config.gear_ratio = strtof(values[idx], NULL);
            config_changed = true;
        }
        else if (strcmp(params[idx], "m8") == 0) {
            config.inverted_enable = string_to_boolean(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "m9") == 0) {
            config.inverted_direction = string_to_boolean(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "ee") == 0) {
            save_to_nvs = string_to_boolean(values[idx]);
        }
    }

    // Save to NVS if requested
    if (save_to_nvs && config_changed) {
        esp_err_t ret = motors_save_config(MOTOR_COARSE, &config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save coarse motor config: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Coarse motor config saved to NVS");
        }
    }

    // Return updated config
    snprintf(motor_config_json_buffer,
             sizeof(motor_config_json_buffer),
             "{\"m0\":%.3f,\"m1\":%lu,\"m2\":%d,\"m3\":%d,\"m4\":%d,\"m5\":%d,\"m6\":%.3f,\"m7\":%.7f,\"m8\":%s,\"m9\":%s,\"saved\":%s}",
             config.angular_acceleration,
             config.full_steps_per_rotation,
             config.current_ma,
             config.microsteps,
             config.max_speed_rps,
             config.r_sense,
             config.min_speed_rps,
             config.gear_ratio,
             boolean_to_string(config.inverted_enable),
             boolean_to_string(config.inverted_direction),
             boolean_to_string(save_to_nvs));

    return motor_config_json_buffer;
}

char* rest_fine_motor_config_handler(int num_params, char *params[], char *values[])
{
    static char motor_config_json_buffer[512];
    motor_config_t config = {0};
    bool save_to_nvs = false;
    bool config_changed = false;

    ESP_LOGI(TAG, "Fine motor config request with %d params", num_params);

    // Load current config
    motors_get_config(MOTOR_FINE, &config);

    // If no params, just return current config
    if (num_params == 0) {
        snprintf(motor_config_json_buffer,
                 sizeof(motor_config_json_buffer),
                 "{\"m0\":%.3f,\"m1\":%lu,\"m2\":%d,\"m3\":%d,\"m4\":%d,\"m5\":%d,\"m6\":%.3f,\"m7\":%.7f,\"m8\":%s,\"m9\":%s}",
                 config.angular_acceleration,
                 config.full_steps_per_rotation,
                 config.current_ma,
                 config.microsteps,
                 config.max_speed_rps,
                 config.r_sense,
                 config.min_speed_rps,
                 config.gear_ratio,
                 boolean_to_string(config.inverted_enable),
                 boolean_to_string(config.inverted_direction));
        return motor_config_json_buffer;
    }

    // Parse parameters and update config
    for (int idx = 0; idx < num_params; idx++) {
        ESP_LOGI(TAG, "  Param[%d]: %s = %s", idx, params[idx], values[idx]);

        if (strcmp(params[idx], "m0") == 0) {
            config.angular_acceleration = strtof(values[idx], NULL);
            config_changed = true;
        }
        else if (strcmp(params[idx], "m1") == 0) {
            config.full_steps_per_rotation = strtoul(values[idx], NULL, 10);
            config_changed = true;
        }
        else if (strcmp(params[idx], "m2") == 0) {
            config.current_ma = (uint16_t)atoi(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "m3") == 0) {
            config.microsteps = (uint16_t)atoi(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "m4") == 0) {
            config.max_speed_rps = (uint16_t)atoi(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "m5") == 0) {
            config.r_sense = (uint16_t)atoi(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "m6") == 0) {
            config.min_speed_rps = strtof(values[idx], NULL);
            config_changed = true;
        }
        else if (strcmp(params[idx], "m7") == 0) {
            config.gear_ratio = strtof(values[idx], NULL);
            config_changed = true;
        }
        else if (strcmp(params[idx], "m8") == 0) {
            config.inverted_enable = string_to_boolean(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "m9") == 0) {
            config.inverted_direction = string_to_boolean(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "ee") == 0) {
            save_to_nvs = string_to_boolean(values[idx]);
        }
    }

    // Save to NVS if requested
    if (save_to_nvs && config_changed) {
        esp_err_t ret = motors_save_config(MOTOR_FINE, &config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save fine motor config: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Fine motor config saved to NVS");
        }
    }

    // Return updated config
    snprintf(motor_config_json_buffer,
             sizeof(motor_config_json_buffer),
             "{\"m0\":%.3f,\"m1\":%lu,\"m2\":%d,\"m3\":%d,\"m4\":%d,\"m5\":%d,\"m6\":%.3f,\"m7\":%.7f,\"m8\":%s,\"m9\":%s,\"saved\":%s}",
             config.angular_acceleration,
             config.full_steps_per_rotation,
             config.current_ma,
             config.microsteps,
             config.max_speed_rps,
             config.r_sense,
             config.min_speed_rps,
             config.gear_ratio,
             boolean_to_string(config.inverted_enable),
             boolean_to_string(config.inverted_direction),
             boolean_to_string(save_to_nvs));

    return motor_config_json_buffer;
}

// Scale configuration REST handler
// Parameters: s0=driver, s1=baudrate, ee=save
char* rest_scale_config_handler(int num_params, char *params[], char *values[])
{
    static char scale_config_json_buffer[256];
    scale_config_t config = {0};
    bool save_to_nvs = false;
    bool config_changed = false;

    ESP_LOGI(TAG, "Scale config request with %d params", num_params);

    // Load current config
    scale_get_config(&config);

    // If no params, just return current config
    if (num_params == 0) {
        snprintf(scale_config_json_buffer,
                 sizeof(scale_config_json_buffer),
                 "{\"s0\":%d,\"s1\":%d}",
                 config.scale_driver,
                 config.scale_baudrate);
        return scale_config_json_buffer;
    }

    // Parse parameters and update config
    for (int idx = 0; idx < num_params; idx++) {
        ESP_LOGI(TAG, "  Param[%d]: %s = %s", idx, params[idx], values[idx]);

        if (strcmp(params[idx], "s0") == 0) {
            config.scale_driver = (scale_driver_t)atoi(values[idx]);
            scale_set_driver(config.scale_driver);
            // Read back config - scale_set_driver may have auto-changed baudrate
            scale_get_config(&config);
            config_changed = true;
        }
        else if (strcmp(params[idx], "s1") == 0) {
            config.scale_baudrate = (scale_baudrate_t)atoi(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "ee") == 0) {
            save_to_nvs = string_to_boolean(values[idx]);
        }
    }

    // Save to NVS if requested
    if (save_to_nvs && config_changed) {
        esp_err_t ret = scale_save_config(&config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save scale config: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Scale config saved to NVS");
        }
    }

    // Return updated config
    snprintf(scale_config_json_buffer,
             sizeof(scale_config_json_buffer),
             "{\"s0\":%d,\"s1\":%d,\"saved\":%s}",
             config.scale_driver,
             config.scale_baudrate,
             boolean_to_string(save_to_nvs));

    return scale_config_json_buffer;
}

// Scale action REST handler
// Parameters: a0=action_type
char* rest_scale_action_handler(int num_params, char *params[], char *values[])
{
    static char scale_action_json_buffer[128];
    scale_action_t action = SCALE_ACTION_NO_ACTION;

    ESP_LOGI(TAG, "Scale action request with %d params", num_params);

    // Parse parameters
    for (int idx = 0; idx < num_params; idx++) {
        ESP_LOGI(TAG, "  Param[%d]: %s = %s", idx, params[idx], values[idx]);

        if (strcmp(params[idx], "a0") == 0) {
            action = (scale_action_t)atoi(values[idx]);
            scale_perform_action(action);
        }
    }

    // Return action response
    snprintf(scale_action_json_buffer,
             sizeof(scale_action_json_buffer),
             "{\"a0\":%d}",
             (int)action);

    return scale_action_json_buffer;
}

// Charge mode configuration handler
char* rest_charge_mode_config_handler(int num_params, char *params[], char *values[])
{
    static char charge_mode_config_json_buffer[512];
    charge_mode_config_t config = {0};
    bool save_to_nvs = false;
    bool config_changed = false;

    charge_mode_get_config(&config);

    // If no parameters, return current config
    if (num_params == 0) {
        snprintf(charge_mode_config_json_buffer, sizeof(charge_mode_config_json_buffer),
                 "{\"c1\":\"#%06lx\",\"c2\":\"#%06lx\",\"c3\":\"#%06lx\",\"c4\":\"#%06lx\","
                 "\"c5\":%.3f,\"c6\":%.3f,\"c7\":%.3f,\"c8\":%.3f,\"c9\":%d,\"c10\":%s,\"c11\":%lu,\"c12\":%.3f}",
                 config.neopixel_normal_charge_colour,
                 config.neopixel_under_charge_colour,
                 config.neopixel_over_charge_colour,
                 config.neopixel_not_ready_colour,
                 config.coarse_stop_threshold,
                 config.fine_stop_threshold,
                 config.set_point_sd_margin,
                 config.set_point_mean_margin,
                 (int)config.decimal_places,
                 boolean_to_string(config.precharge_enable),
                 config.precharge_time_ms,
                 config.precharge_speed_rps);
        return charge_mode_config_json_buffer;
    }

    ESP_LOGI(TAG, "Charge mode config request with %d params", num_params);

    // Parse parameters
    for (int idx = 0; idx < num_params; idx++) {
        ESP_LOGI(TAG, "  Param[%d]: %s = %s", idx, params[idx], values[idx]);

        // LED colors (hex strings)
        if (strcmp(params[idx], "c1") == 0) {
            config.neopixel_normal_charge_colour = hex_string_to_decimal(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "c2") == 0) {
            config.neopixel_under_charge_colour = hex_string_to_decimal(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "c3") == 0) {
            config.neopixel_over_charge_colour = hex_string_to_decimal(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "c4") == 0) {
            config.neopixel_not_ready_colour = hex_string_to_decimal(values[idx]);
            config_changed = true;
        }
        // Thresholds and margins
        else if (strcmp(params[idx], "c5") == 0) {
            config.coarse_stop_threshold = strtof(values[idx], NULL);
            config_changed = true;
        }
        else if (strcmp(params[idx], "c6") == 0) {
            config.fine_stop_threshold = strtof(values[idx], NULL);
            config_changed = true;
        }
        else if (strcmp(params[idx], "c7") == 0) {
            config.set_point_sd_margin = strtof(values[idx], NULL);
            config_changed = true;
        }
        else if (strcmp(params[idx], "c8") == 0) {
            config.set_point_mean_margin = strtof(values[idx], NULL);
            config_changed = true;
        }
        // Decimal places
        else if (strcmp(params[idx], "c9") == 0) {
            config.decimal_places = (decimal_places_t)atoi(values[idx]);
            config_changed = true;
        }
        // Precharge settings
        else if (strcmp(params[idx], "c10") == 0) {
            config.precharge_enable = string_to_boolean(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "c11") == 0) {
            config.precharge_time_ms = strtoul(values[idx], NULL, 10);
            config_changed = true;
        }
        else if (strcmp(params[idx], "c12") == 0) {
            config.precharge_speed_rps = strtof(values[idx], NULL);
            config_changed = true;
        }
        // Save to NVS
        else if (strcmp(params[idx], "ee") == 0) {
            save_to_nvs = string_to_boolean(values[idx]);
        }
    }

    // Save to NVS if requested and config changed
    if (save_to_nvs && config_changed) {
        charge_mode_save_config(&config);
    }

    // Return updated config
    snprintf(charge_mode_config_json_buffer, sizeof(charge_mode_config_json_buffer),
             "{\"c1\":\"#%06lx\",\"c2\":\"#%06lx\",\"c3\":\"#%06lx\",\"c4\":\"#%06lx\","
             "\"c5\":%.3f,\"c6\":%.3f,\"c7\":%.3f,\"c8\":%.3f,\"c9\":%d,\"c10\":%s,\"c11\":%lu,\"c12\":%.3f}",
             config.neopixel_normal_charge_colour,
             config.neopixel_under_charge_colour,
             config.neopixel_over_charge_colour,
             config.neopixel_not_ready_colour,
             config.coarse_stop_threshold,
             config.fine_stop_threshold,
             config.set_point_sd_margin,
             config.set_point_mean_margin,
             (int)config.decimal_places,
             boolean_to_string(config.precharge_enable),
             config.precharge_time_ms,
             config.precharge_speed_rps);

    return charge_mode_config_json_buffer;
}

// Charge mode state handler
char* rest_charge_mode_state_handler(int num_params, char *params[], char *values[])
{
    static char charge_mode_state_json_buffer[320];
    charge_mode_state_t_runtime runtime_state = {0};

    charge_mode_get_runtime_state(&runtime_state);

    ESP_LOGD(TAG, "Charge mode state request with %d params", num_params);

    // Parse control parameters
    for (int idx = 0; idx < num_params; idx++) {
        ESP_LOGI(TAG, "  Param[%d]: %s = %s", idx, params[idx], values[idx]);

        if (strcmp(params[idx], "s0") == 0) {
            // Set target charge weight
            float target = strtof(values[idx], NULL);
            charge_mode_set_target_weight(target);
            runtime_state.target_charge_weight = target;
        }
        else if (strcmp(params[idx], "s2") == 0) {
            // Set charge mode state
            charge_mode_state_t new_state = (charge_mode_state_t)atoi(values[idx]);
            charge_mode_set_state(new_state);
            runtime_state.charge_mode_state = new_state;

            // Switch display to match charge mode state
            if (lvgl_port_lock(100)) {
                if (new_state == CHARGE_MODE_WAIT_FOR_ZERO) {
                    ui_screens_enter_charge(runtime_state.target_charge_weight);
                } else if (new_state == CHARGE_MODE_EXIT) {
                    ui_screens_enter_main_menu();
                }
                lvgl_port_unlock();
            }
        }
    }

    // Get updated state for response
    charge_mode_get_runtime_state(&runtime_state);

    // Format current weight: primary source is live physical scale reading.
    // Runtime snapshot is fallback only (e.g., temporary invalid live frame).
    char weight_string[16];
    float live_weight = scale_get_measurement();
    bool live_valid = scale_is_measurement_valid();
    if (live_valid && !isnanf(live_weight)) {
        snprintf(weight_string, sizeof(weight_string), "%.3f", live_weight);
    } else if (!isnanf(runtime_state.current_weight)) {
        snprintf(weight_string, sizeof(weight_string), "%.3f", runtime_state.current_weight);
    } else {
        snprintf(weight_string, sizeof(weight_string), "\"---\"");
    }

    // Format elapsed time
    char elapsed_time_buffer[16];
    snprintf(elapsed_time_buffer, sizeof(elapsed_time_buffer), "%.2f", runtime_state.elapsed_time_seconds);

    // Settled (post-settle) values for the just-completed charge cycle.
    char settled_weight_buffer[16];
    char settled_time_buffer[16];
    snprintf(settled_weight_buffer, sizeof(settled_weight_buffer), "%.3f", runtime_state.settled_weight);
    snprintf(settled_time_buffer, sizeof(settled_time_buffer), "%.2f", runtime_state.settled_time_seconds);

    // Return state
    snprintf(charge_mode_state_json_buffer, sizeof(charge_mode_state_json_buffer),
             "{\"s0\":%.3f,\"s1\":%s,\"s2\":%d,\"s3\":%lu,\"s4\":\"%s\",\"s5\":\"%s\",\"s6\":%s,\"s7\":\"%s\"}",
             runtime_state.target_charge_weight,
             weight_string,
             (int)runtime_state.charge_mode_state,
             runtime_state.charge_mode_event,
             runtime_state.profile_name,
             elapsed_time_buffer,
             settled_weight_buffer,
             settled_time_buffer);

    // Events persist until next charge cycle (cleared in do_wait_for_zero).
    // Web UI now calculates over/under inline from weight data, so events
    // don't need to be cleared after each REST read.

    return charge_mode_state_json_buffer;
}

// Profile configuration handler
char* rest_profile_config_handler(int num_params, char *params[], char *values[])
{
    static char profile_config_json_buffer[512];
    uint8_t profile_idx = profile_get_selected_idx();
    bool save_to_nvs = false;
    bool config_changed = false;

    // Check if profile index is specified
    for (int idx = 0; idx < num_params; idx++) {
        if (strcmp(params[idx], "pf") == 0) {
            profile_idx = (uint8_t)atoi(values[idx]);
            if (profile_idx >= MAX_PROFILE_CNT) {
                snprintf(profile_config_json_buffer, sizeof(profile_config_json_buffer),
                         "{\"error\":\"InvalidProfileIndex\"}");
                return profile_config_json_buffer;
            }
            profile_select(profile_idx);
            esp_err_t fm_ret = flow_model_load(profile_idx);
            if (fm_ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to load flow model for profile %u: %s",
                         profile_idx, esp_err_to_name(fm_ret));
            }
            config_changed = true;
            break;
        }
    }

    profile_t *current_profile = profile_get_by_idx(profile_idx);
    if (!current_profile) {
        snprintf(profile_config_json_buffer, sizeof(profile_config_json_buffer),
                 "{\"error\":\"ProfileNotFound\"}");
        return profile_config_json_buffer;
    }

    // If no parameters, return current profile config
    if (num_params == 0) {
        snprintf(profile_config_json_buffer, sizeof(profile_config_json_buffer),
                 "{\"pf\":%d,\"p0\":%lu,\"p1\":%lu,\"p2\":\"%s\","
                 "\"p3\":%.3f,\"p4\":%.3f,\"p5\":%.3f,\"p6\":%.3f,\"p7\":%.3f,"
                 "\"p8\":%.3f,\"p9\":%.3f,\"p10\":%.3f,\"p11\":%.3f,\"p12\":%.3f}",
                 profile_idx,
                 current_profile->rev,
                 current_profile->compatibility,
                 current_profile->name,
                 current_profile->coarse_kp,
                 current_profile->coarse_ki,
                 current_profile->coarse_kd,
                 current_profile->coarse_min_flow_speed_rps,
                 current_profile->coarse_max_flow_speed_rps,
                 current_profile->fine_kp,
                 current_profile->fine_ki,
                 current_profile->fine_kd,
                 current_profile->fine_min_flow_speed_rps,
                 current_profile->fine_max_flow_speed_rps);
        return profile_config_json_buffer;
    }

    ESP_LOGI(TAG, "Profile config request with %d params", num_params);

    // Parse parameters
    for (int idx = 0; idx < num_params; idx++) {
        ESP_LOGI(TAG, "  Param[%d]: %s = %s", idx, params[idx], values[idx]);

        if (strcmp(params[idx], "pf") == 0) {
            // Already handled above
            continue;
        }
        else if (strcmp(params[idx], "p0") == 0) {
            current_profile->rev = strtoul(values[idx], NULL, 10);
            config_changed = true;
        }
        else if (strcmp(params[idx], "p1") == 0) {
            current_profile->compatibility = strtoul(values[idx], NULL, 10);
            config_changed = true;
        }
        else if (strcmp(params[idx], "p2") == 0) {
            strncpy(current_profile->name, values[idx], PROFILE_NAME_MAX_LEN - 1);
            current_profile->name[PROFILE_NAME_MAX_LEN - 1] = '\0';
            config_changed = true;
        }
        else if (strcmp(params[idx], "p3") == 0) {
            current_profile->coarse_kp = strtof(values[idx], NULL);
            config_changed = true;
        }
        else if (strcmp(params[idx], "p4") == 0) {
            current_profile->coarse_ki = strtof(values[idx], NULL);
            config_changed = true;
        }
        else if (strcmp(params[idx], "p5") == 0) {
            current_profile->coarse_kd = strtof(values[idx], NULL);
            config_changed = true;
        }
        else if (strcmp(params[idx], "p6") == 0) {
            current_profile->coarse_min_flow_speed_rps = strtof(values[idx], NULL);
            config_changed = true;
        }
        else if (strcmp(params[idx], "p7") == 0) {
            current_profile->coarse_max_flow_speed_rps = strtof(values[idx], NULL);
            config_changed = true;
        }
        else if (strcmp(params[idx], "p8") == 0) {
            current_profile->fine_kp = strtof(values[idx], NULL);
            config_changed = true;
        }
        else if (strcmp(params[idx], "p9") == 0) {
            current_profile->fine_ki = strtof(values[idx], NULL);
            config_changed = true;
        }
        else if (strcmp(params[idx], "p10") == 0) {
            current_profile->fine_kd = strtof(values[idx], NULL);
            config_changed = true;
        }
        else if (strcmp(params[idx], "p11") == 0) {
            current_profile->fine_min_flow_speed_rps = strtof(values[idx], NULL);
            config_changed = true;
        }
        else if (strcmp(params[idx], "p12") == 0) {
            current_profile->fine_max_flow_speed_rps = strtof(values[idx], NULL);
            config_changed = true;
        }
        else if (strcmp(params[idx], "ee") == 0) {
            save_to_nvs = string_to_boolean(values[idx]);
        }
    }

    // Save to NVS if requested and config changed
    if (save_to_nvs && config_changed) {
        profile_save();
    }

    // Return updated config
    snprintf(profile_config_json_buffer, sizeof(profile_config_json_buffer),
             "{\"pf\":%d,\"p0\":%lu,\"p1\":%lu,\"p2\":\"%s\","
             "\"p3\":%.3f,\"p4\":%.3f,\"p5\":%.3f,\"p6\":%.3f,\"p7\":%.3f,"
             "\"p8\":%.3f,\"p9\":%.3f,\"p10\":%.3f,\"p11\":%.3f,\"p12\":%.3f}",
             profile_idx,
             current_profile->rev,
             current_profile->compatibility,
             current_profile->name,
             current_profile->coarse_kp,
             current_profile->coarse_ki,
             current_profile->coarse_kd,
             current_profile->coarse_min_flow_speed_rps,
             current_profile->coarse_max_flow_speed_rps,
             current_profile->fine_kp,
             current_profile->fine_ki,
             current_profile->fine_kd,
             current_profile->fine_min_flow_speed_rps,
             current_profile->fine_max_flow_speed_rps);

    return profile_config_json_buffer;
}

// Profile summary handler
char* rest_profile_summary_handler(int num_params, char *params[], char *values[])
{
    static char profile_summary_json_buffer[512];
    char names[MAX_PROFILE_CNT][PROFILE_NAME_MAX_LEN];
    uint8_t count = 0;

    ESP_LOGI(TAG, "Profile summary request");

    profile_get_all_names(names, &count);
    uint16_t current_idx = profile_get_selected_idx();

    // Build JSON: {"s0":{"0":"AR2208,gr","1":"AR2209,gr",...},"s1":0}
    int offset = snprintf(profile_summary_json_buffer, sizeof(profile_summary_json_buffer),
                          "{\"s0\":{");

    for (uint8_t i = 0; i < count; i++) {
        offset += snprintf(profile_summary_json_buffer + offset,
                          sizeof(profile_summary_json_buffer) - offset,
                          "\"%d\":\"%s\"%s",
                          i, names[i], (i < count - 1) ? "," : "");
    }

    snprintf(profile_summary_json_buffer + offset,
             sizeof(profile_summary_json_buffer) - offset,
             "},\"s1\":%d}",
             current_idx);

    return profile_summary_json_buffer;
}


char* rest_autotune_coarse_handler(int num_params, char *params[], char *values[])
{
    static char autotune_json_buffer[768];
    autotune_status_t status = {0};

    bool start = false;
    bool cancel = false;
    bool finish_now = false;

    autotune_request_t request = {
        .coarse_target_weight = 37.0f,
        .fine_target_weight = 40.0f,
        .total_target_time_s = 20.0f,
        .max_runs_per_stage = 15,
        .coarse_weight_tolerance = 1.0f,
        .fine_weight_tolerance = 0.02f,
        .time_tolerance_s = 2.0f,
        .fine_stop_threshold = 0.02f,
        .coarse_stop_threshold = 0.03f,
        .auto_apply = true,
        .save_to_nvs = false,
    };

    for (int idx = 0; idx < num_params; idx++) {
        if (strcmp(params[idx], "a0") == 0) {
            start = string_to_boolean(values[idx]);
        }
        else if (strcmp(params[idx], "a1") == 0) {
            request.coarse_target_weight = strtof(values[idx], NULL);
        }
        else if (strcmp(params[idx], "a2") == 0) {
            request.total_target_time_s = strtof(values[idx], NULL);
        }
        else if (strcmp(params[idx], "a3") == 0) {
            request.fine_target_weight = strtof(values[idx], NULL);
        }
        else if (strcmp(params[idx], "a5") == 0) {
            request.max_runs_per_stage = atoi(values[idx]);
        }
        else if (strcmp(params[idx], "a6") == 0) {
            request.coarse_weight_tolerance = strtof(values[idx], NULL);
        }
        else if (strcmp(params[idx], "a9") == 0) {
            request.fine_weight_tolerance = strtof(values[idx], NULL);
        }
        else if (strcmp(params[idx], "a7") == 0) {
            request.time_tolerance_s = strtof(values[idx], NULL);
        }
        else if (strcmp(params[idx], "a10") == 0) {
            request.fine_stop_threshold = strtof(values[idx], NULL);
        }
        else if (strcmp(params[idx], "a11") == 0) {
            request.coarse_stop_threshold = strtof(values[idx], NULL);
        }
        else if (strcmp(params[idx], "a8") == 0) {
            request.auto_apply = string_to_boolean(values[idx]);
        }
        else if (strcmp(params[idx], "ee") == 0) {
            request.save_to_nvs = string_to_boolean(values[idx]);
        }
        else if (strcmp(params[idx], "ca") == 0) {
            cancel = string_to_boolean(values[idx]);
        }
        else if (strcmp(params[idx], "fn") == 0) {
            finish_now = string_to_boolean(values[idx]);
        }
    }

    if (cancel) {
        autotune_cancel();
    }
    if (finish_now) {
        autotune_finish_now();
    }

    if (start) {
        esp_err_t start_ret = autotune_start(&request);
        if (start_ret != ESP_OK) {
            autotune_get_status(&status);
            snprintf(autotune_json_buffer, sizeof(autotune_json_buffer),
                     "{\"ok\":false,\"err\":\"%s\",\"state\":%d,\"stage\":%d,\"msg\":\"%s\"}",
                     esp_err_to_name(start_ret), (int)status.state, (int)status.stage, status.message);
            return autotune_json_buffer;
        }
    }

    autotune_get_status(&status);

    snprintf(autotune_json_buffer, sizeof(autotune_json_buffer),
             "{\"ok\":true,\"state\":%d,\"stage\":%d,\"substatus\":%d,\"progress\":%.1f,"
             "\"runs_done\":%d,\"runs_total\":%d,\"stage_run\":%d,\"stage_max_runs\":%d,"
             "\"active_kp\":%.5f,\"active_kd\":%.5f,"
             "\"current_weight\":%.4f,\"current_time\":%.2f,"
             "\"last_weight\":%.4f,\"last_time\":%.2f,"
             "\"coarse_best_kp\":%.5f,\"coarse_best_kd\":%.5f,\"coarse_best_abs_err\":%.5f,\"coarse_best_time_err\":%.5f,"
             "\"fine_best_kp\":%.5f,\"fine_best_kd\":%.5f,\"fine_best_abs_err\":%.5f,\"fine_best_time_err\":%.5f,"
             "\"msg\":\"%s\"}",
             (int)status.state,
             (int)status.stage,
             (int)status.substatus,
             status.progress_pct,
             status.runs_done,
             status.runs_total,
             status.stage_run,
             status.stage_max_runs,
             status.active_kp,
             status.active_kd,
             status.current_weight,
             status.current_elapsed_s,
             status.last_weight,
             status.last_elapsed_s,
             status.coarse_best_kp,
             status.coarse_best_kd,
             status.coarse_best_weight_error,
             status.coarse_best_time_error,
             status.fine_best_kp,
             status.fine_best_kd,
             status.fine_best_weight_error,
             status.fine_best_time_error,
             status.message);

    return autotune_json_buffer;
}

// Autotune trial history handler - returns JSON array of trial results
char* rest_autotune_trials_handler(int num_params, char *params[], char *values[])
{
    // Buffer for up to 64 trials, ~110 chars each
    static char trials_json_buffer[8192];
    static autotune_trial_result_t trials[64];

    int count = autotune_get_trials(trials, 64);
    ESP_LOGI("REST", "autotune_trials: count=%d", count);

    int offset = snprintf(trials_json_buffer, sizeof(trials_json_buffer), "{\"trials\":[");

    for (int i = 0; i < count; i++) {
        offset += snprintf(trials_json_buffer + offset,
                          sizeof(trials_json_buffer) - offset,
                          "%s{\"n\":%d,\"s\":%d,\"kp\":%.5f,\"kd\":%.5f,\"we\":%.4f,\"te\":%.3f,\"os\":%.4f,\"sw\":%.3f,\"t\":%.2f}",
                          (i > 0) ? "," : "",
                          i + 1,
                          trials[i].stage,
                          trials[i].kp,
                          trials[i].kd,
                          trials[i].weight_error,
                          trials[i].time_error,
                          trials[i].overshoot,
                          trials[i].settled_weight,
                          trials[i].elapsed_s);
        if (offset >= (int)sizeof(trials_json_buffer) - 10) break;
    }

    snprintf(trials_json_buffer + offset,
             sizeof(trials_json_buffer) - offset,
             "],\"count\":%d}", count);

    return trials_json_buffer;
}

// Autotune telemetry endpoint - returns compact telemetry entries
// Optional param: n=max entries (default 64, max 128)
char* rest_autotune_telemetry_handler(int num_params, char *params[], char *values[])
{
    static char telemetry_json_buffer[12288];
    static autotune_telemetry_entry_t entries[128];

    int max_entries = 64;
    for (int idx = 0; idx < num_params; idx++) {
        if (strcmp(params[idx], "n") == 0) {
            max_entries = atoi(values[idx]);
        }
    }
    if (max_entries < 1) max_entries = 1;
    if (max_entries > 128) max_entries = 128;

    int count = autotune_get_telemetry(entries, max_entries);

    int offset = snprintf(telemetry_json_buffer, sizeof(telemetry_json_buffer), "{\"telemetry\":[");
    for (int i = 0; i < count; i++) {
        offset += snprintf(telemetry_json_buffer + offset,
                           sizeof(telemetry_json_buffer) - offset,
                           "%s{\"t\":%lu,\"s\":%d,\"p\":%d,\"kp\":%.5f,\"kd\":%.5f,"
                           "\"w\":%.4f,\"dt\":%.3f,\"os\":%.4f,\"we\":%.4f,\"q\":%.3f,\"ok\":%s}",
                           (i > 0) ? "," : "",
                           (unsigned long)entries[i].timestamp_ms,
                           (int)entries[i].stage,
                           (int)entries[i].phase,
                           entries[i].kp,
                           entries[i].kd,
                           entries[i].settled_weight,
                           entries[i].elapsed_s,
                           entries[i].overshoot,
                           entries[i].abs_weight_error,
                           entries[i].quality,
                           boolean_to_string(entries[i].accepted));
        if (offset >= (int)sizeof(telemetry_json_buffer) - 16) break;
    }

    snprintf(telemetry_json_buffer + offset,
             sizeof(telemetry_json_buffer) - offset,
             "],\"count\":%d}", count);
    return telemetry_json_buffer;
}

// Cleanup mode state handler
char* rest_cleanup_mode_state_handler(int num_params, char *params[], char *values[])
{
    static char cleanup_mode_state_json_buffer[128];
    cleanup_mode_runtime_state_t runtime_state = {0};

    cleanup_mode_get_state(&runtime_state);

    ESP_LOGI(TAG, "Cleanup mode state request with %d params", num_params);

    // Parse control parameters
    for (int idx = 0; idx < num_params; idx++) {
        ESP_LOGI(TAG, "  Param[%d]: %s = %s", idx, params[idx], values[idx]);

        if (strcmp(params[idx], "s0") == 0) {
            // Set cleanup mode state
            cleanup_mode_state_t new_state = (cleanup_mode_state_t)atoi(values[idx]);
            cleanup_mode_set_state(new_state);
            runtime_state.cleanup_mode_state = new_state;
        }
        else if (strcmp(params[idx], "s1") == 0) {
            // Set trickler speed
            float speed = strtof(values[idx], NULL);
            cleanup_mode_set_speed(speed);
            runtime_state.trickler_speed = speed;
        }
    }

    // Get updated state for response
    cleanup_mode_get_state(&runtime_state);

    // Return state
    snprintf(cleanup_mode_state_json_buffer, sizeof(cleanup_mode_state_json_buffer),
             "{\"s0\":%d,\"s1\":%.3f}",
             (int)runtime_state.cleanup_mode_state,
             runtime_state.trickler_speed);

    return cleanup_mode_state_json_buffer;
}

// NeoPixel LED configuration handler
char* rest_neopixel_led_config_handler(int num_params, char *params[], char *values[])
{
    static char neopixel_config_json_buffer[256];
    neopixel_led_config_t config = {0};
    bool save_to_nvs = false;
    bool config_changed = false;

    neopixel_led_get_config(&config);

    // If no parameters, return current config
    if (num_params == 0) {
        snprintf(neopixel_config_json_buffer, sizeof(neopixel_config_json_buffer),
                 "{\"bl\":\"#%06lx\",\"l1\":\"#%06lx\",\"l2\":\"#%06lx\",\"l3\":%d,\"l4\":%s,\"l5\":%d}",
                 config.default_led_colours.mini12864_backlight_colour,
                 config.default_led_colours.led1_colour,
                 config.default_led_colours.led2_colour,
                 (int)config.pwm_out_led_chain_count,
                 boolean_to_string(config.pwm_out_led_is_rgbw),
                 (int)config.pwm_out_led_colour_order);
        return neopixel_config_json_buffer;
    }

    ESP_LOGI(TAG, "NeoPixel LED config request with %d params", num_params);

    // Parse parameters
    for (int idx = 0; idx < num_params; idx++) {
        ESP_LOGI(TAG, "  Param[%d]: %s = %s", idx, params[idx], values[idx]);

        if (strcmp(params[idx], "bl") == 0) {
            config.default_led_colours.mini12864_backlight_colour = hex_string_to_decimal(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "l1") == 0) {
            config.default_led_colours.led1_colour = hex_string_to_decimal(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "l2") == 0) {
            config.default_led_colours.led2_colour = hex_string_to_decimal(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "l3") == 0) {
            config.pwm_out_led_chain_count = (neopixel_led_chain_count_t)atoi(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "l4") == 0) {
            config.pwm_out_led_is_rgbw = string_to_boolean(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "l5") == 0) {
            config.pwm_out_led_colour_order = (neopixel_colour_order_t)atoi(values[idx]);
            config_changed = true;
        }
        else if (strcmp(params[idx], "ee") == 0) {
            save_to_nvs = string_to_boolean(values[idx]);
        }
    }

    // Save to NVS if requested and config changed
    if (save_to_nvs && config_changed) {
        neopixel_led_save_config(&config);
    }

    // Update LED colors
    if (config_changed) {
        neopixel_led_set_colour(
            config.default_led_colours.mini12864_backlight_colour,
            config.default_led_colours.led1_colour,
            config.default_led_colours.led2_colour);
    }

    // Return updated config
    snprintf(neopixel_config_json_buffer, sizeof(neopixel_config_json_buffer),
             "{\"bl\":\"#%06lx\",\"l1\":\"#%06lx\",\"l2\":\"#%06lx\",\"l3\":%d,\"l4\":%s,\"l5\":%d}",
             config.default_led_colours.mini12864_backlight_colour,
             config.default_led_colours.led1_colour,
             config.default_led_colours.led2_colour,
             (int)config.pwm_out_led_chain_count,
             boolean_to_string(config.pwm_out_led_is_rgbw),
             (int)config.pwm_out_led_colour_order);

    return neopixel_config_json_buffer;
}

// Mini 12864 module configuration handler
// GET  /rest/mini_12864_config            -> {"b0":bool,"b1":int}
// POST /rest/mini_12864_config?b0=&b1=&ee=save
char* rest_mini_12864_config_handler(int num_params, char *params[], char *values[])
{
    static char mini12864_json_buffer[64];
    mini_12864_config_t config;
    bool save_to_nvs = false;
    bool config_changed = false;

    ESP_LOGI(TAG, "Mini12864 config request with %d params", num_params);

    lvgl_port_get_mini12864_config(&config);

    if (num_params == 0) {
        snprintf(mini12864_json_buffer, sizeof(mini12864_json_buffer),
                 "{\"b0\":%s,\"b1\":%d}",
                 boolean_to_string(config.inverted_encoder),
                 (int)config.display_rotation);
        return mini12864_json_buffer;
    }

    for (int idx = 0; idx < num_params; idx++) {
        ESP_LOGI(TAG, "  Param[%d]: %s = %s", idx, params[idx], values[idx]);

        if (strcmp(params[idx], "b0") == 0) {
            config.inverted_encoder = string_to_boolean(values[idx]);
            config_changed = true;
        } else if (strcmp(params[idx], "b1") == 0) {
            config.display_rotation = (uint8_t)atoi(values[idx]);
            config_changed = true;
        } else if (strcmp(params[idx], "ee") == 0) {
            save_to_nvs = true;
        }
    }

    if (config_changed) {
        lvgl_port_set_mini12864_config(&config, save_to_nvs);
    }

    snprintf(mini12864_json_buffer, sizeof(mini12864_json_buffer),
             "{\"b0\":%s,\"b1\":%d}",
             boolean_to_string(config.inverted_encoder),
             (int)config.display_rotation);
    return mini12864_json_buffer;
}

// Flow model endpoint
// GET /rest/flow_model[?pf=<idx>]        — returns model summary
// GET /rest/flow_model?pf=<idx>&reset=1  — resets model for profile
char* rest_flow_model_handler(int num_params, char *params[], char *values[])
{
    static char flow_model_json_buffer[256];
    uint8_t profile_idx = profile_get_selected_idx();
    bool do_reset = false;

    for (int idx = 0; idx < num_params; idx++) {
        if (strcmp(params[idx], "pf") == 0) {
            profile_idx = (uint8_t)atoi(values[idx]);
        } else if (strcmp(params[idx], "reset") == 0 && string_to_boolean(values[idx])) {
            do_reset = true;
        }
    }

    if (do_reset) {
        esp_err_t ret = flow_model_reset(profile_idx);
        if (ret != ESP_OK) {
            snprintf(flow_model_json_buffer, sizeof(flow_model_json_buffer),
                     "{\"error\":\"ResetFailed\",\"pf\":%d}", profile_idx);
            return flow_model_json_buffer;
        }
        ESP_LOGI(TAG, "Flow model reset for profile %d via REST", profile_idx);
    }

    flow_model_t fm;
    if (flow_model_get(profile_idx, &fm) != ESP_OK) {
        snprintf(flow_model_json_buffer, sizeof(flow_model_json_buffer),
                 "{\"error\":\"ProfileNotFound\",\"pf\":%d}", profile_idx);
        return flow_model_json_buffer;
    }

    snprintf(flow_model_json_buffer, sizeof(flow_model_json_buffer),
             "{\"pf\":%d,\"coarse_trusted\":%s,\"fine_trusted\":%s,"
             "\"coarse_overshoot\":%.4f,\"fine_overshoot\":%.4f,"
             "\"coarse_delay\":%.1f,\"fine_delay\":%.1f}",
             profile_idx,
             boolean_to_string(flow_model_is_trusted(profile_idx, 0)),
             boolean_to_string(flow_model_is_trusted(profile_idx, 1)),
             fm.coarse.inertia_overshoot_gn,
             fm.fine.inertia_overshoot_gn,
             fm.coarse.transport_delay_ms,
             fm.fine.transport_delay_ms);
    return flow_model_json_buffer;
}
