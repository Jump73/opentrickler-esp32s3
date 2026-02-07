#include "motors.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include <string.h>

// TMC2209 driver includes
#include "tmc2209.h"
#include "tmc_uart_hal_esp32.h"

static const char *TAG = "Motors";

#define NVS_NAMESPACE "motors"
#define NVS_KEY_COARSE "coarse"
#define NVS_KEY_FINE "fine"
#define CONFIG_VERSION 1

// GPIO pin definitions (matching original Pico W pinout)
#define COARSE_MOTOR_ADDR       0
#define COARSE_MOTOR_EN_PIN     GPIO_NUM_6
#define COARSE_MOTOR_STEP_PIN   GPIO_NUM_3
#define COARSE_MOTOR_DIR_PIN    GPIO_NUM_2

#define FINE_MOTOR_ADDR         1
#define FINE_MOTOR_EN_PIN       GPIO_NUM_9
#define FINE_MOTOR_STEP_PIN     GPIO_NUM_8
#define FINE_MOTOR_DIR_PIN      GPIO_NUM_7

// Motor driver instances
static TMC2209_t coarse_tmc_driver;
static TMC2209_t fine_tmc_driver;

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

/**
 * Initialize GPIO pins for motor control
 */
static esp_err_t motor_gpio_init(motor_type_t motor)
{
    gpio_num_t en_pin, dir_pin;
    bool inverted_enable;

    if (motor == MOTOR_COARSE) {
        en_pin = COARSE_MOTOR_EN_PIN;
        dir_pin = COARSE_MOTOR_DIR_PIN;
        inverted_enable = coarse_motor_config.inverted_enable;
    } else {
        en_pin = FINE_MOTOR_EN_PIN;
        dir_pin = FINE_MOTOR_DIR_PIN;
        inverted_enable = fine_motor_config.inverted_enable;
    }

    // Configure ENABLE pin
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << en_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Set initial state (disabled)
    // If inverted_enable, LOW = enabled, so we want HIGH for disabled
    gpio_set_level(en_pin, inverted_enable ? 1 : 0);

    // Configure DIRECTION pin
    io_conf.pin_bit_mask = (1ULL << dir_pin);
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(dir_pin, 0);

    // Note: STEP pin will be controlled by MCPWM later
    // For now, we'll leave it unconfigured

    ESP_LOGI(TAG, "%s motor GPIO initialized (EN=%d, DIR=%d)",
             (motor == MOTOR_COARSE) ? "Coarse" : "Fine", en_pin, dir_pin);

    return ESP_OK;
}

/**
 * Initialize TMC2209 driver with configuration
 */
static esp_err_t tmc2209_driver_init(motor_type_t motor, TMC2209_t *driver, const motor_config_t *config)
{
    // Set defaults first
    TMC2209_SetDefaults(driver);

    // Apply user configuration
    driver->config.motor.address = (motor == MOTOR_COARSE) ? COARSE_MOTOR_ADDR : FINE_MOTOR_ADDR;
    driver->config.current = config->current_ma;
    driver->config.r_sense = config->r_sense;
    driver->config.hold_current_pct = 50;
    driver->config.microsteps = config->microsteps;

    // Initialize driver communication
    if (!TMC2209_Init(driver)) {
        ESP_LOGE(TAG, "Failed to initialize %s TMC2209 driver!",
                 (motor == MOTOR_COARSE) ? "coarse" : "fine");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "%s TMC2209 initialized (addr=%d, current=%dmA, microsteps=%d)",
             (motor == MOTOR_COARSE) ? "Coarse" : "Fine",
             driver->config.motor.address,
             driver->config.current,
             driver->config.microsteps);

    return ESP_OK;
}

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

    // Initialize TMC UART
    esp_err_t ret = tmc_uart_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize TMC UART");
        return ret;
    }

    // Initialize GPIO pins
    ret = motor_gpio_init(MOTOR_COARSE);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = motor_gpio_init(MOTOR_FINE);
    if (ret != ESP_OK) {
        return ret;
    }

    // Initialize TMC2209 drivers
    ret = tmc2209_driver_init(MOTOR_COARSE, &coarse_tmc_driver, &coarse_motor_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Coarse motor TMC initialization failed - motor may not be connected");
        // Don't return error - allow system to continue without motors
    }

    ret = tmc2209_driver_init(MOTOR_FINE, &fine_tmc_driver, &fine_motor_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Fine motor TMC initialization failed - motor may not be connected");
        // Don't return error - allow system to continue without motors
    }

    ESP_LOGI(TAG, "Motors module initialized successfully");
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

        // Reinitialize driver with new configuration
        if (motor == MOTOR_COARSE) {
            tmc2209_driver_init(MOTOR_COARSE, &coarse_tmc_driver, &coarse_motor_config);
        } else {
            tmc2209_driver_init(MOTOR_FINE, &fine_tmc_driver, &fine_motor_config);
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

// Motor control implementation
esp_err_t motor_set_speed(motor_type_t motor, float speed_rps)
{
    // TODO: Implement MCPWM-based STEP signal generation
    // For now, just log the request
    ESP_LOGI(TAG, "%s motor speed set to: %.3f rps (MCPWM not yet implemented)",
             (motor == MOTOR_COARSE) ? "Coarse" : "Fine", speed_rps);

    // Set direction pin based on speed sign
    gpio_num_t dir_pin = (motor == MOTOR_COARSE) ? COARSE_MOTOR_DIR_PIN : FINE_MOTOR_DIR_PIN;
    bool inverted = (motor == MOTOR_COARSE) ? coarse_motor_config.inverted_direction : fine_motor_config.inverted_direction;

    bool direction = (speed_rps >= 0);
    if (inverted) {
        direction = !direction;
    }

    gpio_set_level(dir_pin, direction ? 1 : 0);

    return ESP_OK;
}

esp_err_t motor_enable(motor_type_t motor, bool enable)
{
    gpio_num_t en_pin;
    bool inverted_enable;

    if (motor == MOTOR_COARSE) {
        en_pin = COARSE_MOTOR_EN_PIN;
        inverted_enable = coarse_motor_config.inverted_enable;
    } else {
        en_pin = FINE_MOTOR_EN_PIN;
        inverted_enable = fine_motor_config.inverted_enable;
    }

    // If inverted_enable: LOW = enabled, HIGH = disabled
    // If normal: HIGH = enabled, LOW = disabled
    bool pin_level = inverted_enable ? !enable : enable;
    gpio_set_level(en_pin, pin_level ? 1 : 0);

    ESP_LOGI(TAG, "%s motor %s",
             (motor == MOTOR_COARSE) ? "Coarse" : "Fine",
             enable ? "enabled" : "disabled");

    return ESP_OK;
}
