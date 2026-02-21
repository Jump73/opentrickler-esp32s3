#include "motors.h"
#include "board_pins.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "driver/mcpwm_prelude.h"
#include <string.h>
#include <math.h>

// TMC2209 driver includes
#include "tmc2209.h"
#include "tmc_uart_hal_esp32.h"

static const char *TAG = "Motors";

#define NVS_NAMESPACE "motors"
#define NVS_KEY_COARSE "coarse"
#define NVS_KEY_FINE "fine"
#define CONFIG_VERSION 3

// Motor driver instances
static TMC2209_t coarse_tmc_driver;
static TMC2209_t fine_tmc_driver;

// MCPWM instances for STEP signal generation
static mcpwm_timer_handle_t coarse_mcpwm_timer = NULL;
static mcpwm_oper_handle_t coarse_mcpwm_oper = NULL;
static mcpwm_cmpr_handle_t coarse_mcpwm_cmpr = NULL;
static mcpwm_gen_handle_t coarse_mcpwm_gen = NULL;
static bool coarse_mcpwm_running = false;

static mcpwm_timer_handle_t fine_mcpwm_timer = NULL;
static mcpwm_oper_handle_t fine_mcpwm_oper = NULL;
static mcpwm_cmpr_handle_t fine_mcpwm_cmpr = NULL;
static mcpwm_gen_handle_t fine_mcpwm_gen = NULL;
static bool fine_mcpwm_running = false;

// Default configurations
static motor_config_t coarse_motor_config = {
    .config_version = CONFIG_VERSION,
    .full_steps_per_rotation = 200,
    .current_ma = 800,
    .microsteps = 16,   // Matches hardware MS1/MS2 pin state (works without UART)
    .max_speed_rps = 10,
    .r_sense = 110,
    .angular_acceleration = 10.0f,   // rev/s² - smooth ramp (0→3 rps in 0.3s)
    .min_speed_rps = 0.1f,
    .gear_ratio = 1.0f,
    .inverted_direction = false,
    .inverted_enable = true   // TMC2209: EN is active-low (LOW = enabled)
};

static motor_config_t fine_motor_config = {
    .config_version = CONFIG_VERSION,
    .full_steps_per_rotation = 200,
    .current_ma = 600,
    .microsteps = 16,   // Matches hardware MS1/MS2 pin state (works without UART)
    .max_speed_rps = 5,
    .r_sense = 110,
    .angular_acceleration = 5.0f,    // rev/s² - smooth ramp (0→3 rps in 0.6s)
    .min_speed_rps = 0.05f,
    .gear_ratio = 1.0f,
    .inverted_direction = false,
    .inverted_enable = true   // TMC2209: EN is active-low (LOW = enabled)
};

/**
 * Initialize MCPWM for STEP signal generation
 * Creates PWM signal at configurable frequency (initially 1 Hz, updated dynamically)
 */
static esp_err_t motor_mcpwm_init(motor_type_t motor)
{
    gpio_num_t step_pin;
    mcpwm_timer_handle_t *timer;
    mcpwm_oper_handle_t *oper;
    mcpwm_cmpr_handle_t *cmpr;
    mcpwm_gen_handle_t *gen;

    if (motor == MOTOR_COARSE) {
        step_pin = COARSE_MOTOR_STEP_PIN;
        timer = &coarse_mcpwm_timer;
        oper = &coarse_mcpwm_oper;
        cmpr = &coarse_mcpwm_cmpr;
        gen = &coarse_mcpwm_gen;
    } else {
        step_pin = FINE_MOTOR_STEP_PIN;
        timer = &fine_mcpwm_timer;
        oper = &fine_mcpwm_oper;
        cmpr = &fine_mcpwm_cmpr;
        gen = &fine_mcpwm_gen;
    }

    // Create MCPWM timer (initially 1 kHz, will be updated dynamically)
    // Use separate groups: coarse=group1, fine=group0 (avoids resource conflicts)
    int group_id = (motor == MOTOR_COARSE) ? 1 : 0;
    mcpwm_timer_config_t timer_config = {
        .group_id = group_id,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = 10000000,  // 10 MHz resolution for precise timing
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
        .period_ticks = 10000,  // Initial period (1 kHz = 10MHz / 10000)
        .flags.update_period_on_empty = true,  // Update period at end of cycle (glitch-free)
    };
    ESP_ERROR_CHECK(mcpwm_new_timer(&timer_config, timer));

    // Create MCPWM operator
    mcpwm_operator_config_t oper_config = {
        .group_id = group_id,
    };
    ESP_ERROR_CHECK(mcpwm_new_operator(&oper_config, oper));

    // Connect operator to timer
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(*oper, *timer));

    // Create MCPWM comparator (for 50% duty cycle)
    mcpwm_comparator_config_t cmpr_config = {
        .flags.update_cmp_on_tez = true,
    };
    ESP_ERROR_CHECK(mcpwm_new_comparator(*oper, &cmpr_config, cmpr));

    // Set initial compare value (50% duty cycle)
    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(*cmpr, 5000));  // 50% of 10000

    // Create MCPWM generator
    mcpwm_generator_config_t gen_config = {
        .gen_gpio_num = step_pin,
    };
    ESP_ERROR_CHECK(mcpwm_new_generator(*oper, &gen_config, gen));

    // Set generator actions (creates square wave)
    // On timer empty (start of period): set high
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(*gen,
                    MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)));
    // On compare match: set low (creates 50% duty cycle)
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(*gen,
                    MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, *cmpr, MCPWM_GEN_ACTION_LOW)));

    // Enable and start timer (but with very low frequency initially)
    ESP_ERROR_CHECK(mcpwm_timer_enable(*timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(*timer, MCPWM_TIMER_STOP_EMPTY));  // Start stopped

    ESP_LOGI(TAG, "%s motor MCPWM initialized (STEP pin=%d)",
             (motor == MOTOR_COARSE) ? "Coarse" : "Fine", step_pin);

    return ESP_OK;
}

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

    // Initialize driver communication (UART may not be available - non-fatal)
    bool uart_ok = TMC2209_Init(driver);
    ESP_LOGI(TAG, "%s TMC2209 UART init: %s",
        (motor == MOTOR_COARSE) ? "Coarse" : "Fine",
        uart_ok ? "SUCCESS" : "FAILED");
    if (!uart_ok) {
        ESP_LOGW(TAG, "%s TMC2209 UART init failed - running without driver config",
                 (motor == MOTOR_COARSE) ? "Coarse" : "Fine");
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

    // Try to load saved configurations (only if version matches)
    motor_config_t temp_config;
    if (motors_load_config(MOTOR_COARSE, &temp_config) == ESP_OK
            && temp_config.config_version == CONFIG_VERSION) {
        coarse_motor_config = temp_config;
        ESP_LOGI(TAG, "Loaded coarse motor config from NVS");
    } else {
        ESP_LOGI(TAG, "Using default coarse motor config (version mismatch or no saved config)");
    }

    if (motors_load_config(MOTOR_FINE, &temp_config) == ESP_OK
            && temp_config.config_version == CONFIG_VERSION) {
        fine_motor_config = temp_config;
        ESP_LOGI(TAG, "Loaded fine motor config from NVS");
    } else {
        ESP_LOGI(TAG, "Using default fine motor config (version mismatch or no saved config)");
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

    // Initialize MCPWM for STEP signal generation
    ret = motor_mcpwm_init(MOTOR_COARSE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize coarse motor MCPWM");
        return ret;
    }

    ret = motor_mcpwm_init(MOTOR_FINE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize fine motor MCPWM");
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

// Speed ramping state
static float coarse_current_speed = 0.0f;
static float fine_current_speed = 0.0f;
static TickType_t coarse_last_speed_tick = 0;
static TickType_t fine_last_speed_tick = 0;

// Motor control implementation with acceleration ramping
esp_err_t motor_set_speed(motor_type_t motor, float speed_rps)
{
    const motor_config_t *config;
    mcpwm_timer_handle_t timer;
    mcpwm_cmpr_handle_t cmpr;
    gpio_num_t dir_pin;
    bool inverted;
    float *current_speed;
    TickType_t *last_tick;

    // Get motor configuration and MCPWM handles
    if (motor == MOTOR_COARSE) {
        config = &coarse_motor_config;
        timer = coarse_mcpwm_timer;
        cmpr = coarse_mcpwm_cmpr;
        dir_pin = COARSE_MOTOR_DIR_PIN;
        inverted = coarse_motor_config.inverted_direction;
        current_speed = &coarse_current_speed;
        last_tick = &coarse_last_speed_tick;
    } else {
        config = &fine_motor_config;
        timer = fine_mcpwm_timer;
        cmpr = fine_mcpwm_cmpr;
        dir_pin = FINE_MOTOR_DIR_PIN;
        inverted = fine_motor_config.inverted_direction;
        current_speed = &fine_current_speed;
        last_tick = &fine_last_speed_tick;
    }

    if (!isfinite(speed_rps)) {
        ESP_LOGE(TAG, "%s motor: invalid speed input (NaN/Inf)",
                 (motor == MOTOR_COARSE) ? "Coarse" : "Fine");
        return ESP_ERR_INVALID_ARG;
    }

    // Set direction pin based on speed sign
    bool direction = (speed_rps >= 0);
    if (inverted) {
        direction = !direction;
    }
    gpio_set_level(dir_pin, direction ? 1 : 0);

    // Get absolute speed
    float target_speed = fabsf(speed_rps);

    bool *running = (motor == MOTOR_COARSE) ? &coarse_mcpwm_running : &fine_mcpwm_running;

    // Stop motor if speed is too low or zero
    if (target_speed < 0.001f) {
        if (*running) {
            mcpwm_timer_start_stop(timer, MCPWM_TIMER_STOP_EMPTY);
            *running = false;
        }
        *current_speed = 0.0f;
        ESP_LOGD(TAG, "%s motor stopped (speed=0)",
                 (motor == MOTOR_COARSE) ? "Coarse" : "Fine");
        return ESP_OK;
    }

    // Apply acceleration ramp: limit speed change based on angular_acceleration
    TickType_t now = xTaskGetTickCount();
    float elapsed_s = (float)((now - *last_tick) * portTICK_PERIOD_MS) / 1000.0f;
    if (elapsed_s > 1.0f) elapsed_s = 1.0f;  // Cap for first call or long gaps
    *last_tick = now;

    float max_change = config->angular_acceleration * elapsed_s;
    float speed_diff = target_speed - *current_speed;

    if (speed_diff > max_change) {
        *current_speed += max_change;  // Accelerate
    } else if (speed_diff < -max_change) {
        *current_speed -= max_change;  // Decelerate
    } else {
        *current_speed = target_speed;  // Close enough, snap to target
    }

    float abs_speed_rps = *current_speed;
    if (abs_speed_rps < 0.001f) abs_speed_rps = 0.001f;
    if (!isfinite(abs_speed_rps)) {
        ESP_LOGE(TAG, "%s motor: invalid internal speed (NaN/Inf)",
                 (motor == MOTOR_COARSE) ? "Coarse" : "Fine");
        return ESP_ERR_INVALID_ARG;
    }

    // Calculate STEP frequency: freq = speed_rps × full_steps × microsteps
    uint32_t steps_per_rev = config->full_steps_per_rotation * config->microsteps;
    float step_freq_hz = abs_speed_rps * (float)steps_per_rev;
    if (!isfinite(step_freq_hz) || step_freq_hz <= 0.0f) {
        ESP_LOGE(TAG, "%s motor: invalid step frequency %.3f Hz",
                 (motor == MOTOR_COARSE) ? "Coarse" : "Fine",
                 step_freq_hz);
        return ESP_ERR_INVALID_ARG;
    }

    // MCPWM timer resolution is 10 MHz
    const uint32_t MCPWM_RESOLUTION_HZ = 10000000;

    // Calculate period ticks: period = resolution / frequency
    uint32_t period_ticks = (uint32_t)(MCPWM_RESOLUTION_HZ / step_freq_hz);

    // Limit to valid range (minimum 10 ticks, maximum 65535 ticks)
    if (period_ticks < 10) {
        period_ticks = 10;
    } else if (period_ticks > 65535) {
        period_ticks = 65535;
    }

    // Update timer period (this changes the frequency)
    esp_err_t ret = mcpwm_timer_set_period(timer, period_ticks);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "%s motor: mcpwm_timer_set_period failed (ticks=%lu): %s",
                 (motor == MOTOR_COARSE) ? "Coarse" : "Fine",
                 (unsigned long)period_ticks,
                 esp_err_to_name(ret));
        mcpwm_timer_start_stop(timer, MCPWM_TIMER_STOP_EMPTY);
        *running = false;
        return ret;
    }

    // Update comparator to maintain 50% duty cycle
    ret = mcpwm_comparator_set_compare_value(cmpr, period_ticks / 2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "%s motor: mcpwm_comparator_set_compare_value failed: %s",
                 (motor == MOTOR_COARSE) ? "Coarse" : "Fine",
                 esp_err_to_name(ret));
        mcpwm_timer_start_stop(timer, MCPWM_TIMER_STOP_EMPTY);
        *running = false;
        return ret;
    }

    // Start timer only if not already running (avoid glitches from repeated starts)
    if (!*running) {
        mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP);
        *running = true;
    }

    ESP_LOGD(TAG, "%s motor: target=%.3f actual=%.3f rps, freq=%.1f Hz",
             (motor == MOTOR_COARSE) ? "Coarse" : "Fine",
             target_speed, abs_speed_rps, step_freq_hz);

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
