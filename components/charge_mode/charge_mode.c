#include "charge_mode.h"
#include "motors.h"
#include "scale.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

// Motor speeds (can be tuned)
// These match the original OpenTrickler range - the original uses PID control
// which would vary speed dynamically; we use fixed speeds with threshold switching
#define COARSE_SPEED_RPS    1.0f    // ~60 RPM - conservative start (original allows up to 5 rps via PID)
#define FINE_SPEED_RPS      0.2f    // ~12 RPM

// Acceleration ramp (rps per task tick = rps/s * TASK_PERIOD_MS/1000)
// Without ramp, motor jumps from 0 to full speed and stalls (buzzes)
// PIO original used hardware acceleration profiles; we emulate in software
#define COARSE_ACCEL_RPS_PER_TICK   0.05f   // ~1 rps/s at 50ms tick
#define FINE_ACCEL_RPS_PER_TICK     0.02f   // ~0.4 rps/s at 50ms tick
#define COARSE_START_RPS            0.15f   // Minimum start speed (above static friction)
#define FINE_START_RPS              0.05f   // Minimum start speed (above static friction)

// Stability detection - number of consecutive stable readings required
#define ZERO_STABLE_COUNT       20  // 1s @ 50ms tick - stable zero before dispensing
#define SETTLE_STABLE_COUNT     10  // 500ms - scale settled after dispensing
#define CUP_REMOVE_COUNT         3  // 150ms - quick cup removal detection
#define CUP_RETURN_STABLE_COUNT 10  // 500ms - cup returned and stable

// Tolerance for stable reading detection
// G&G JJB resolution: 0.001g (1mg) = 0.02gn
#define ZERO_TOLERANCE_G    0.005f  // ±0.005g (±5mg ≈ ±0.08gn) for zero detection
#define STABLE_TOLERANCE_G  0.002f  // ±0.002g (±2mg) reading-to-reading stability
#define CUP_REMOVED_G      -0.3f   // Below this = cup removed (negative from tare)
#define CUP_RETURNED_MAX_G  1.0f   // Below this and near zero = empty cup returned

// Task parameters
#define TASK_PERIOD_MS      50
#define TASK_STACK          4096
#define TASK_PRIORITY       8

static const char *TAG = "ChargeMode";

#define NVS_NAMESPACE "charge_mode"
#define NVS_KEY_CONFIG "config"
#define CONFIG_VERSION 1

// Default configuration
static charge_mode_config_t charge_mode_config = {
    .config_version = CONFIG_VERSION,

    // LED colors (default from original)
    .neopixel_normal_charge_colour = RGB_COLOUR_GREEN,
    .neopixel_under_charge_colour = RGB_COLOUR_YELLOW,
    .neopixel_over_charge_colour = RGB_COLOUR_RED,
    .neopixel_not_ready_colour = RGB_COLOUR_BLUE,

    // Thresholds (default from original)
    .coarse_stop_threshold = 0.5f,
    .fine_stop_threshold = 0.05f,
    .set_point_sd_margin = 0.01f,
    .set_point_mean_margin = 0.01f,

    // Display settings
    .decimal_places = DP_3,

    // Precharge settings (default from original)
    .precharge_enable = false,
    .precharge_time_ms = 200,
    .precharge_speed_rps = 2.0f
};

// Runtime state
static charge_mode_state_t_runtime runtime_state = {
    .target_charge_weight = 0.0f,
    .charge_mode_event = 0,
    .charge_mode_state = CHARGE_MODE_EXIT,
    .current_weight = 0.0f,
    .profile_name = "Default",
    .elapsed_time_seconds = 0.0f
};

static void stop_all_motors(void)
{
    motor_set_speed(MOTOR_COARSE, 0.0f);
    motor_set_speed(MOTOR_FINE, 0.0f);
    motor_enable(MOTOR_COARSE, false);
    motor_enable(MOTOR_FINE, false);
}

static void charge_mode_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Charge mode task started");

    bool coarse_running = false;
    bool fine_running = false;
    bool settling = false;       // true = motors stopped, waiting for scale to settle
    int stable_count = 0;        // consecutive stable readings counter
    float prev_weight = 0.0f;    // previous reading for stability check
    float coarse_speed = 0.0f;   // current coarse motor speed (ramped)
    float fine_speed = 0.0f;     // current fine motor speed (ramped)

    while (1) {
        float weight = scale_get_measurement();

        // Update runtime weight for REST API
        if (!isnanf(weight)) {
            runtime_state.current_weight = weight;
        }

        switch (runtime_state.charge_mode_state) {

            case CHARGE_MODE_WAIT_FOR_ZERO:
                if (coarse_running || fine_running) {
                    stop_all_motors();
                    coarse_running = false;
                    fine_running = false;
                }
                // Guard: need a valid target
                if (runtime_state.target_charge_weight <= 0.001f) {
                    stable_count = 0;
                    break;
                }
                // Require N consecutive stable readings near zero
                if (!isnanf(weight) && fabsf(weight) <= ZERO_TOLERANCE_G
                    && fabsf(weight - prev_weight) <= STABLE_TOLERANCE_G) {
                    stable_count++;
                } else {
                    stable_count = 0;
                }
                if (stable_count >= ZERO_STABLE_COUNT) {
                    stable_count = 0;
                    settling = false;
                    ESP_LOGI(TAG, "Stable zero (%.4f), starting dispense to %.4f",
                             weight, runtime_state.target_charge_weight);
                    motor_enable(MOTOR_COARSE, true);
                    motor_enable(MOTOR_FINE, true);
                    // Start above static friction threshold
                    coarse_speed = COARSE_START_RPS;
                    fine_speed = FINE_START_RPS;
                    motor_set_speed(MOTOR_COARSE, coarse_speed);
                    motor_set_speed(MOTOR_FINE, fine_speed);
                    coarse_running = true;
                    fine_running = true;
                    runtime_state.elapsed_time_seconds = 0.0f;
                    runtime_state.charge_mode_state = CHARGE_MODE_WAIT_FOR_COMPLETE;
                }
                break;

            case CHARGE_MODE_WAIT_FOR_COMPLETE:
                if (!isnanf(weight)) {
                    runtime_state.elapsed_time_seconds += TASK_PERIOD_MS / 1000.0f;
                    float target = runtime_state.target_charge_weight;

                    if (settling) {
                        // Waiting for scale to settle after motors stopped
                        if (fabsf(weight - prev_weight) <= STABLE_TOLERANCE_G) {
                            stable_count++;
                        } else {
                            stable_count = 0;
                        }
                        if (stable_count >= SETTLE_STABLE_COUNT) {
                            ESP_LOGI(TAG, "Scale settled, final: %.4f (target: %.4f)",
                                     weight, target);
                            stable_count = 0;
                            settling = false;
                            runtime_state.charge_mode_state = CHARGE_MODE_WAIT_FOR_CUP_REMOVAL;
                        }
                    } else if (weight >= target - charge_mode_config.fine_stop_threshold) {
                        // Target reached - stop all motors, begin settling
                        ESP_LOGI(TAG, "Target reached (%.4f), settling...", weight);
                        stop_all_motors();
                        coarse_running = false;
                        fine_running = false;
                        coarse_speed = 0.0f;
                        fine_speed = 0.0f;
                        settling = true;
                        stable_count = 0;
                    } else if (coarse_running &&
                               weight >= target - charge_mode_config.coarse_stop_threshold) {
                        // Near target - switch to fine motor only
                        ESP_LOGI(TAG, "Coarse stop (%.4f), fine only", weight);
                        motor_set_speed(MOTOR_COARSE, 0.0f);
                        motor_enable(MOTOR_COARSE, false);
                        coarse_running = false;
                        coarse_speed = 0.0f;
                    } else {
                        // Motors running - apply acceleration ramp
                        if (coarse_running && coarse_speed < COARSE_SPEED_RPS) {
                            coarse_speed += COARSE_ACCEL_RPS_PER_TICK;
                            if (coarse_speed > COARSE_SPEED_RPS) coarse_speed = COARSE_SPEED_RPS;
                            motor_set_speed(MOTOR_COARSE, coarse_speed);
                        }
                        if (fine_running && fine_speed < FINE_SPEED_RPS) {
                            fine_speed += FINE_ACCEL_RPS_PER_TICK;
                            if (fine_speed > FINE_SPEED_RPS) fine_speed = FINE_SPEED_RPS;
                            motor_set_speed(MOTOR_FINE, fine_speed);
                        }
                    }
                }
                break;

            case CHARGE_MODE_WAIT_FOR_CUP_REMOVAL:
                // Detect cup removal: weight drops negative (below tare point)
                if (!isnanf(weight) && weight < CUP_REMOVED_G) {
                    stable_count++;
                } else {
                    stable_count = 0;
                }
                if (stable_count >= CUP_REMOVE_COUNT) {
                    stable_count = 0;
                    ESP_LOGI(TAG, "Cup removed (%.4f)", weight);
                    runtime_state.charge_mode_state = CHARGE_MODE_WAIT_FOR_CUP_RETURN;
                }
                break;

            case CHARGE_MODE_WAIT_FOR_CUP_RETURN:
                // Detect cup return: N stable readings near zero (empty cup on scale)
                if (!isnanf(weight) && weight >= CUP_REMOVED_G && weight < CUP_RETURNED_MAX_G
                    && fabsf(weight - prev_weight) <= STABLE_TOLERANCE_G) {
                    stable_count++;
                } else {
                    stable_count = 0;
                }
                if (stable_count >= CUP_RETURN_STABLE_COUNT) {
                    stable_count = 0;
                    ESP_LOGI(TAG, "Cup returned, stable at %.4f", weight);
                    runtime_state.charge_mode_state = CHARGE_MODE_WAIT_FOR_ZERO;
                }
                break;

            case CHARGE_MODE_EXIT:
            default:
                if (coarse_running || fine_running) {
                    stop_all_motors();
                    coarse_running = false;
                    fine_running = false;
                }
                stable_count = 0;
                settling = false;
                break;
        }

        prev_weight = (!isnanf(weight)) ? weight : prev_weight;
        vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_MS));
    }
}

esp_err_t charge_mode_init(void)
{
    ESP_LOGI(TAG, "Initializing charge mode module");

    // Try to load saved configuration
    charge_mode_config_t temp_config;
    if (charge_mode_load_config(&temp_config) == ESP_OK) {
        charge_mode_config = temp_config;
        ESP_LOGI(TAG, "Loaded charge mode config from NVS");
    } else {
        ESP_LOGI(TAG, "Using default charge mode config");
    }

    ESP_LOGI(TAG, "Coarse threshold: %.3f, Fine threshold: %.3f",
             charge_mode_config.coarse_stop_threshold,
             charge_mode_config.fine_stop_threshold);

    // Start charge mode task
    BaseType_t ret = xTaskCreate(charge_mode_task, "charge_mode",
                                  TASK_STACK, NULL, TASK_PRIORITY, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create charge mode task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t charge_mode_save_config(const charge_mode_config_t *config)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ESP_LOGI(TAG, "Saving charge mode config to NVS");

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }

    // Save config with version
    charge_mode_config_t config_to_save = *config;
    config_to_save.config_version = CONFIG_VERSION;

    ret = nvs_set_blob(nvs_handle, NVS_KEY_CONFIG, &config_to_save, sizeof(charge_mode_config_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error saving config: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }

    ret = nvs_commit(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Charge mode config saved successfully");
        // Update local copy
        charge_mode_config = config_to_save;
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t charge_mode_load_config(charge_mode_config_t *config)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No saved charge mode config found");
        return ret;
    }

    size_t required_size = sizeof(charge_mode_config_t);
    ret = nvs_get_blob(nvs_handle, NVS_KEY_CONFIG, config, &required_size);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Loaded charge mode config from NVS");
    } else {
        ESP_LOGW(TAG, "Failed to load config: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t charge_mode_get_config(charge_mode_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    *config = charge_mode_config;
    return ESP_OK;
}

esp_err_t charge_mode_set_target_weight(float weight)
{
    ESP_LOGI(TAG, "Setting target weight to: %.3f", weight);
    runtime_state.target_charge_weight = weight;
    return ESP_OK;
}

esp_err_t charge_mode_set_state(charge_mode_state_t state)
{
    ESP_LOGI(TAG, "Charge mode state: %d -> %d",
             runtime_state.charge_mode_state, state);

    if (state == CHARGE_MODE_EXIT && runtime_state.charge_mode_state != CHARGE_MODE_EXIT) {
        ESP_LOGI(TAG, "Exiting charge mode - stopping motors");
        stop_all_motors();
        runtime_state.elapsed_time_seconds = 0.0f;
    } else if (state == CHARGE_MODE_WAIT_FOR_ZERO &&
               runtime_state.charge_mode_state == CHARGE_MODE_EXIT) {
        ESP_LOGI(TAG, "Entering charge mode");
    }

    runtime_state.charge_mode_state = state;
    return ESP_OK;
}

esp_err_t charge_mode_get_runtime_state(charge_mode_state_t_runtime *state)
{
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }

    *state = runtime_state;
    return ESP_OK;
}

uint32_t hex_string_to_decimal(const char *string)
{
    uint32_t value = 0;

    if (!string) {
        return 0;
    }

    // Handle URL-encoded # (%23)
    const char *hex_start = string;
    if (string[0] == '%' && string[1] == '2' && string[2] == '3') {
        // URL-encoded # found, skip %23
        hex_start = string + 3;
    } else if (string[0] == '#') {
        // Regular # found, skip it
        hex_start = string + 1;
    } else {
        // No # found, assume it's already just hex digits
        hex_start = string;
    }

    // Parse hex string
    value = strtol(hex_start, NULL, 16);

    return value;
}
