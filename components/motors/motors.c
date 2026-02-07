#include "motors.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "Motors";

#define NVS_NAMESPACE "motors"
#define NVS_KEY_COARSE "coarse"
#define NVS_KEY_FINE "fine"
#define CONFIG_VERSION 1

// Default configurations
static motor_config_t coarse_motor_config = {
    .config_version = CONFIG_VERSION,
    .full_steps_per_rotation = 200,
    .current_ma = 800,
    .microsteps = 16,
    .max_speed_rps = 10,
    .r_sense = 110,
    .angular_acceleration = 50.0f,
    .min_speed_rps = 0.1f,
    .gear_ratio = 1.0f,
    .inverted_direction = false,
    .inverted_enable = false
};

static motor_config_t fine_motor_config = {
    .config_version = CONFIG_VERSION,
    .full_steps_per_rotation = 200,
    .current_ma = 600,
    .microsteps = 16,
    .max_speed_rps = 5,
    .r_sense = 110,
    .angular_acceleration = 30.0f,
    .min_speed_rps = 0.05f,
    .gear_ratio = 1.0f,
    .inverted_direction = false,
    .inverted_enable = false
};

esp_err_t motors_init(void)
{
    ESP_LOGI(TAG, "Initializing motors module");

    // Try to load saved configurations
    motor_config_t temp_config;
    if (motors_load_config(MOTOR_COARSE, &temp_config) == ESP_OK) {
        coarse_motor_config = temp_config;
        ESP_LOGI(TAG, "Loaded coarse motor config from NVS");
    } else {
        ESP_LOGI(TAG, "Using default coarse motor config");
    }

    if (motors_load_config(MOTOR_FINE, &temp_config) == ESP_OK) {
        fine_motor_config = temp_config;
        ESP_LOGI(TAG, "Loaded fine motor config from NVS");
    } else {
        ESP_LOGI(TAG, "Using default fine motor config");
    }

    return ESP_OK;
}

esp_err_t motors_save_config(motor_type_t motor, const motor_config_t *config)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    const char *key = (motor == MOTOR_COARSE) ? NVS_KEY_COARSE : NVS_KEY_FINE;

    ESP_LOGI(TAG, "Saving %s motor config to NVS", (motor == MOTOR_COARSE) ? "coarse" : "fine");

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }

    // Save config with version
    motor_config_t config_to_save = *config;
    config_to_save.config_version = CONFIG_VERSION;

    ret = nvs_set_blob(nvs_handle, key, &config_to_save, sizeof(motor_config_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error saving config: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }

    ret = nvs_commit(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Motor config saved successfully");
        // Update local copy
        if (motor == MOTOR_COARSE) {
            coarse_motor_config = config_to_save;
        } else {
            fine_motor_config = config_to_save;
        }
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t motors_load_config(motor_type_t motor, motor_config_t *config)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    const char *key = (motor == MOTOR_COARSE) ? NVS_KEY_COARSE : NVS_KEY_FINE;

    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No saved motor config found");
        return ret;
    }

    size_t required_size = sizeof(motor_config_t);
    ret = nvs_get_blob(nvs_handle, key, config, &required_size);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Loaded %s motor config from NVS", (motor == MOTOR_COARSE) ? "coarse" : "fine");
    } else {
        ESP_LOGW(TAG, "Failed to load config: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t motors_get_config(motor_type_t motor, motor_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    if (motor == MOTOR_COARSE) {
        *config = coarse_motor_config;
    } else {
        *config = fine_motor_config;
    }

    return ESP_OK;
}

// Motor control stubs (hardware implementation later)
esp_err_t motor_set_speed(motor_type_t motor, float speed_rps)
{
    ESP_LOGI(TAG, "%s motor speed set to: %.3f rps (stub)",
             (motor == MOTOR_COARSE) ? "Coarse" : "Fine", speed_rps);
    return ESP_OK;
}

esp_err_t motor_enable(motor_type_t motor, bool enable)
{
    ESP_LOGI(TAG, "%s motor %s (stub)",
             (motor == MOTOR_COARSE) ? "Coarse" : "Fine",
             enable ? "enabled" : "disabled");
    return ESP_OK;
}
