/*
 * charge_mode.c - Powder charge mode with PID control for OpenTrickler ESP32-S3
 *
 * Ported from original OpenTrickler-RP2040-Controller charge_mode.cpp.
 * Uses profile-based PID control for both coarse and fine motors,
 * blocking scale measurements, and statistical stability detection.
 */

#include "charge_mode.h"
#include "neopixel_led.h"
#include "motors.h"
#include "scale.h"
#include "profile.h"
#include "flow_model.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "ChargeMode";

#define NVS_NAMESPACE "charge_mode"
#define NVS_KEY_CONFIG "config"
#define CONFIG_VERSION 1

// Task parameters
#define TASK_STACK          4096
#define TASK_PRIORITY       8

/* ══════════════════════ Float Ring Buffer ══════════════════════ */

#define RING_BUF_MAX 16

typedef struct {
    float data[RING_BUF_MAX];
    int head;
    int count;
    int capacity;
} ring_buf_t;

static void ring_buf_init(ring_buf_t *rb, int capacity)
{
    rb->head = 0;
    rb->count = 0;
    rb->capacity = (capacity > RING_BUF_MAX) ? RING_BUF_MAX : capacity;
}

static void ring_buf_push(ring_buf_t *rb, float value)
{
    rb->data[rb->head] = value;
    rb->head = (rb->head + 1) % rb->capacity;
    if (rb->count < rb->capacity) {
        rb->count++;
    }
}

static float ring_buf_mean(const ring_buf_t *rb)
{
    if (rb->count == 0) return 0.0f;
    float sum = 0.0f;
    for (int i = 0; i < rb->count; i++) {
        sum += rb->data[i];
    }
    return sum / (float)rb->count;
}

static float ring_buf_sd(const ring_buf_t *rb)
{
    if (rb->count < 2) return 999.0f;
    float mean = ring_buf_mean(rb);
    float sum_sq = 0.0f;
    for (int i = 0; i < rb->count; i++) {
        float diff = rb->data[i] - mean;
        sum_sq += diff * diff;
    }
    return sqrtf(sum_sq / (float)rb->count);
}

/* ══════════════════════ Configuration ══════════════════════ */

// Default configuration (matching original)
static charge_mode_config_t charge_mode_config = {
    .config_version = CONFIG_VERSION,

    .neopixel_normal_charge_colour = RGB_COLOUR_GREEN,
    .neopixel_under_charge_colour = RGB_COLOUR_YELLOW,
    .neopixel_over_charge_colour = RGB_COLOUR_RED,
    .neopixel_not_ready_colour = RGB_COLOUR_BLUE,

    .coarse_stop_threshold = 5.0f,      // Original default: 5 grains
    .fine_stop_threshold = 0.03f,        // Original default: 0.03 grains
    .set_point_sd_margin = 0.02f,
    .set_point_mean_margin = 0.02f,

    .decimal_places = DP_2,

    .precharge_enable = false,
    .precharge_time_ms = 1000,
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

/* ══════════════════════ Charge Mode Event Bits ══════════════════════ */

#define CHARGE_MODE_EVENT_NO_EVENT      (1 << 0)
#define CHARGE_MODE_EVENT_UNDER_CHARGE  (1 << 1)
#define CHARGE_MODE_EVENT_OVER_CHARGE   (1 << 2)

/* ══════════════════════ Helper ══════════════════════ */

static void stop_all_motors(void)
{
    motor_set_speed(MOTOR_COARSE, 0.0f);
    motor_set_speed(MOTOR_FINE, 0.0f);
    motor_enable(MOTOR_COARSE, false);
    motor_enable(MOTOR_FINE, false);
}

/* Check if REST API changed state to EXIT (abort requested) */
static inline bool exit_requested(void)
{
    return runtime_state.charge_mode_state == CHARGE_MODE_EXIT;
}

/* Set LED colour for charge mode status feedback.
   Mirrors original: backlight stays at default, led1 & led2 show status colour. */
static void charge_mode_set_led(uint32_t status_colour)
{
    neopixel_led_config_t led_cfg;
    neopixel_led_get_config(&led_cfg);
    neopixel_led_set_colour(
        led_cfg.default_led_colours.mini12864_backlight_colour,
        status_colour,
        status_colour
    );
}

/* Reset LEDs to default colours (backlight + led1 + led2 all default). */
static void charge_mode_reset_led(void)
{
    neopixel_led_config_t led_cfg;
    neopixel_led_get_config(&led_cfg);
    neopixel_led_set_colour(
        led_cfg.default_led_colours.mini12864_backlight_colour,
        led_cfg.default_led_colours.led1_colour,
        led_cfg.default_led_colours.led2_colour
    );
}

/* ══════════════════════ Wait for Zero ══════════════════════ */
/*
 * Original: FloatRingBuffer(10), check SD < sd_margin && abs(mean) < mean_margin
 * Waits for 10 stable readings near zero, 300ms apart (minimum 3 seconds).
 */
static void do_wait_for_zero(void)
{
    ESP_LOGI(TAG, "State: WAIT_FOR_ZERO (target=%.3f)", runtime_state.target_charge_weight);

    // Set LED to not-ready colour (blue)
    charge_mode_set_led(charge_mode_config.neopixel_not_ready_colour);

    // Clear events from previous cycle
    runtime_state.charge_mode_event = 0;

    ring_buf_t data_buffer;
    ring_buf_init(&data_buffer, 10);

    while (!exit_requested()) {
        TickType_t tick_start = xTaskGetTickCount();

        // Guard: need a valid target weight
        if (runtime_state.target_charge_weight <= 0.001f) {
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        }

        // Block wait for next measurement (up to 300ms)
        float measurement;
        if (scale_block_wait_for_measurement(300, &measurement)) {
            ring_buf_push(&data_buffer, measurement);
            runtime_state.current_weight = measurement;
        }

        // Check stop condition: 10 stable readings
        if (data_buffer.count >= 10) {
            float sd = ring_buf_sd(&data_buffer);
            float mean = ring_buf_mean(&data_buffer);

            if (sd < charge_mode_config.set_point_sd_margin &&
                fabsf(mean) < charge_mode_config.set_point_mean_margin) {
                ESP_LOGI(TAG, "Stable zero detected (mean=%.4f, sd=%.4f)", mean, sd);
                runtime_state.charge_mode_state = CHARGE_MODE_WAIT_FOR_COMPLETE;
                return;
            }
        }

        // Wait minimum 300ms between samples
        vTaskDelayUntil(&tick_start, pdMS_TO_TICKS(300));
    }
}

/* ══════════════════════ Wait for Complete (PID) ══════════════════════ */
/*
 * Original: PID control loop using profile parameters.
 * Coarse motor runs until error < coarse_stop_threshold.
 * Fine motor PID runs until error < fine_stop_threshold.
 * Speed = kp*error + ki*integral + kd*derivative, clamped to min/max.
 */
static void do_wait_for_complete(void)
{
    ESP_LOGI(TAG, "State: WAIT_FOR_COMPLETE (target=%.3f)", runtime_state.target_charge_weight);

    // Set LED to under-charge colour (yellow) at start of charging
    charge_mode_set_led(charge_mode_config.neopixel_under_charge_colour);

    float target = runtime_state.target_charge_weight;

    // Get profile PID parameters
    profile_t *profile = profile_get_selected();
    if (profile) {
        strncpy(runtime_state.profile_name, profile->name, sizeof(runtime_state.profile_name) - 1);
    }

    // Get motor speed limits from motor config and profile
    motor_config_t coarse_cfg, fine_cfg;
    motors_get_config(MOTOR_COARSE, &coarse_cfg);
    motors_get_config(MOTOR_FINE, &fine_cfg);

    float coarse_max_speed = fminf((float)coarse_cfg.max_speed_rps,
                                    profile->coarse_max_flow_speed_rps);
    float coarse_min_speed = fmaxf(coarse_cfg.min_speed_rps,
                                    profile->coarse_min_flow_speed_rps);
    float fine_max_speed = fminf((float)fine_cfg.max_speed_rps,
                                  profile->fine_max_flow_speed_rps);
    float fine_min_speed = fmaxf(fine_cfg.min_speed_rps,
                                  profile->fine_min_flow_speed_rps);

    // PD state (ki is not used)
    float last_error = 0.0f;
    TickType_t last_sample_tick = xTaskGetTickCount();
    bool coarse_running = true;

    // Flow model profile index (for recording)
    uint8_t profile_idx = profile_get_selected_idx();

    // Enable coarse motor first; fine starts after coarse stops
    motor_enable(MOTOR_COARSE, true);
    motor_enable(MOTOR_FINE, false);

    // Start flow model recording for coarse motor
    flow_model_record_start(MOTOR_COARSE);


    // Reset timer
    runtime_state.elapsed_time_seconds = 0.0f;
    TickType_t charge_start_tick = xTaskGetTickCount();

    ESP_LOGI(TAG, "PID: coarse kp=%.3f ki=%.3f kd=%.3f speed=[%.2f..%.2f]",
             profile->coarse_kp, profile->coarse_ki, profile->coarse_kd,
             coarse_min_speed, coarse_max_speed);
    ESP_LOGI(TAG, "PID: fine   kp=%.3f ki=%.3f kd=%.3f speed=[%.2f..%.2f]",
             profile->fine_kp, profile->fine_ki, profile->fine_kd,
             fine_min_speed, fine_max_speed);

    // Log feedforward model status
    {
        flow_model_t fm;
        if (flow_model_get(profile_idx, &fm) == ESP_OK) {
            ESP_LOGI(TAG, "FF: coarse model %d pts, delay=%.0fms, inertia=%.3fs",
                     fm.coarse.num_points, fm.coarse.transport_delay_ms,
                     fm.coarse.inertia_factor_s);
            ESP_LOGI(TAG, "FF: fine   model %d pts, delay=%.0fms, inertia=%.3fs",
                     fm.fine.num_points, fm.fine.transport_delay_ms,
                     fm.fine.inertia_factor_s);
        } else {
            ESP_LOGI(TAG, "FF: no model data, pure PID mode");
        }
    }

    while (!exit_requested()) {
        // Block wait for measurement
        float current_weight;
        if (!scale_block_wait_for_measurement(200, &current_weight)) {
            continue;  // Timeout, retry
        }

        TickType_t current_tick = xTaskGetTickCount();
        runtime_state.current_weight = current_weight;

        // Update elapsed time
        runtime_state.elapsed_time_seconds =
            (float)((current_tick - charge_start_tick) * portTICK_PERIOD_MS) / 1000.0f;

        float error = target - current_weight;

        // ── Stop condition: target reached ──
        if (error < charge_mode_config.fine_stop_threshold) {
            ESP_LOGI(TAG, "Target reached! weight=%.4f, error=%.4f", current_weight, error);
            motor_set_speed(MOTOR_FINE, 0);
            motor_set_speed(MOTOR_COARSE, 0);
            break;
        }

        // ── Coarse motor stop condition ──
        if (error < charge_mode_config.coarse_stop_threshold && coarse_running) {
            ESP_LOGI(TAG, "Coarse stop at weight=%.4f, error=%.4f, switching to fine",
                     current_weight, error);
            coarse_running = false;
            motor_set_speed(MOTOR_COARSE, 0);
            motor_enable(MOTOR_COARSE, false);

            // Mark coarse motor stop (recording continues for inertia measurement)
            flow_model_record_stop();
            // Collect post-stop settling samples for inertia calculation
            for (int i = 0; i < 10; i++) {
                float settle_w;
                if (scale_block_wait_for_measurement(200, &settle_w)) {
                    flow_model_record_sample(0.0f, settle_w);
                }
            }
            flow_model_analyze_and_update(profile_get_selected_idx());
            flow_model_record_start(MOTOR_FINE);

            // Start fine motor now that coarse is done
            motor_enable(MOTOR_FINE, true);
            // Reset PD state for fine motor
            last_error = error;
        }

        // ── PD calculation (pure, no feedforward) ──
        float elapsed_ms = (float)((current_tick - last_sample_tick) * portTICK_PERIOD_MS);
        if (elapsed_ms < 1.0f) elapsed_ms = 1.0f;
        float derivative = (error - last_error) / elapsed_ms;

        float set_speed = 0.0f;
        if (coarse_running) {
            set_speed = profile->coarse_kp * error
                      + profile->coarse_kd * derivative;
            set_speed = fmaxf(coarse_min_speed, fminf(set_speed, coarse_max_speed));
            motor_set_speed(MOTOR_COARSE, set_speed);
        } else {
            set_speed = profile->fine_kp * error
                      + profile->fine_kd * derivative;
            set_speed = fmaxf(fine_min_speed, fminf(set_speed, fine_max_speed));
            motor_set_speed(MOTOR_FINE, set_speed);
        }

        // Record sample for flow model
        flow_model_record_sample(set_speed, current_weight);

        last_sample_tick = current_tick;
        last_error = error;
    }

    // Finalize flow recording — collect settling samples for inertia
    flow_model_record_stop();
    for (int i = 0; i < 10; i++) {
        float settle_w;
        if (scale_block_wait_for_measurement(200, &settle_w)) {
            flow_model_record_sample(0.0f, settle_w);
        }
    }
    flow_model_analyze_and_update(profile_get_selected_idx());

    // Stop timer
    TickType_t now = xTaskGetTickCount();
    runtime_state.elapsed_time_seconds =
        (float)((now - charge_start_tick) * portTICK_PERIOD_MS) / 1000.0f;

    // Precharge: run coarse motor briefly to pre-fill the tube for next charge
    if (charge_mode_config.precharge_enable) {
        vTaskDelay(pdMS_TO_TICKS(500));
        motor_set_speed(MOTOR_COARSE, charge_mode_config.precharge_speed_rps);
        motor_enable(MOTOR_COARSE, true);
        vTaskDelay(pdMS_TO_TICKS(charge_mode_config.precharge_time_ms));
        motor_set_speed(MOTOR_COARSE, 0);
        motor_enable(MOTOR_COARSE, false);
    }

    // Disable motors
    motor_enable(MOTOR_FINE, false);
    motor_enable(MOTOR_COARSE, false);

    // NOTE: Do NOT reset LED here - keep under_charge colour until
    // do_wait_for_cup_removal() analyzes the result and sets the correct
    // colour (over/under/normal). This matches the original RP2040 behaviour.

    if (!exit_requested()) {
        runtime_state.charge_mode_state = CHARGE_MODE_WAIT_FOR_CUP_REMOVAL;
    }
}

/* ══════════════════════ Wait for Cup Removal ══════════════════════ */
/*
 * Original: Wait 1s, analyze charge result (over/under/normal),
 * then wait for 5 stable readings with mean very negative (cup removed).
 */
static void do_wait_for_cup_removal(void)
{
    ESP_LOGI(TAG, "State: WAIT_FOR_CUP_REMOVAL");

    // Wait for scale to fully settle after motor stop
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Wait for cup removal with smart reclassification:
    // - Keep reclassifying while weight stays near target (powder settling)
    // - Freeze classification when weight jumps (hand touching cup)
    ring_buf_t data_buffer;
    ring_buf_init(&data_buffer, 5);
    float classified_weight = 0.0f;
    bool classification_frozen = false;
    uint32_t last_led_state = 0;

    while (!exit_requested()) {
        TickType_t tick_start = xTaskGetTickCount();

        float measurement;
        if (!scale_block_wait_for_measurement(200, &measurement)) {
            continue;
        }
        runtime_state.current_weight = measurement;
        ring_buf_push(&data_buffer, measurement);

        // Reclassify only while weight is close to target (powder settling).
        // Freeze when weight deviates by more than 1gn from last classified
        // reading — that means user's hand is on the cup.
        if (!classification_frozen) {
            if (classified_weight != 0.0f &&
                fabsf(measurement - classified_weight) > 1.0f) {
                classification_frozen = true;
            } else {
                classified_weight = measurement;
                float error = runtime_state.target_charge_weight - measurement;

                uint32_t new_led_colour;
                if (error <= -charge_mode_config.fine_stop_threshold) {
                    runtime_state.charge_mode_event = CHARGE_MODE_EVENT_OVER_CHARGE;
                    new_led_colour = charge_mode_config.neopixel_over_charge_colour;
                } else if (error >= charge_mode_config.fine_stop_threshold) {
                    runtime_state.charge_mode_event = CHARGE_MODE_EVENT_UNDER_CHARGE;
                    new_led_colour = charge_mode_config.neopixel_under_charge_colour;
                } else {
                    runtime_state.charge_mode_event = 0;
                    new_led_colour = charge_mode_config.neopixel_normal_charge_colour;
                }

                if (new_led_colour != last_led_state) {
                    charge_mode_set_led(new_led_colour);
                    last_led_state = new_led_colour;
                    if (runtime_state.charge_mode_event == CHARGE_MODE_EVENT_OVER_CHARGE) {
                        ESP_LOGW(TAG, "OVER CHARGE: weight=%.4f, error=%.4f", measurement, error);
                    } else if (runtime_state.charge_mode_event == CHARGE_MODE_EVENT_UNDER_CHARGE) {
                        ESP_LOGW(TAG, "UNDER CHARGE: weight=%.4f, error=%.4f", measurement, error);
                    } else {
                        ESP_LOGI(TAG, "GOOD CHARGE: weight=%.4f, error=%.4f", measurement, error);
                    }
                }
            }
        }

        // Stop condition: 5 stable readings with very negative mean (cup removed)
        if (data_buffer.count >= 5) {
            float sd = ring_buf_sd(&data_buffer);
            float mean = ring_buf_mean(&data_buffer);

            // Original: mean + 10 < margin (meaning mean < margin - 10, so very negative)
            if (sd < charge_mode_config.set_point_sd_margin &&
                mean + 10.0f < charge_mode_config.set_point_mean_margin) {
                ESP_LOGI(TAG, "Cup removed (mean=%.4f, sd=%.4f)", mean, sd);
                break;
            }
        }

        vTaskDelayUntil(&tick_start, pdMS_TO_TICKS(300));
    }

    // Reset LED to default colours after cup removed (matches original)
    charge_mode_reset_led();

    if (!exit_requested()) {
        runtime_state.charge_mode_state = CHARGE_MODE_WAIT_FOR_CUP_RETURN;
    }
}

/* ══════════════════════ Wait for Cup Return ══════════════════════ */
/*
 * Original: Wait for weight >= 0 (cup placed back on scale).
 */
static void do_wait_for_cup_return(void)
{
    ESP_LOGI(TAG, "State: WAIT_FOR_CUP_RETURN");

    // Set LED to not-ready colour (blue) while waiting for cup
    charge_mode_set_led(charge_mode_config.neopixel_not_ready_colour);

    while (!exit_requested()) {
        TickType_t tick_start = xTaskGetTickCount();

        float measurement;
        if (!scale_block_wait_for_measurement(200, &measurement)) {
            continue;
        }
        runtime_state.current_weight = measurement;

        // Cup returned when weight goes positive (cup on scale near zero)
        if (measurement >= 0.0f) {
            ESP_LOGI(TAG, "Cup returned (weight=%.4f)", measurement);
            break;
        }

        vTaskDelayUntil(&tick_start, pdMS_TO_TICKS(20));
    }

    // Reset LED to default colours
    charge_mode_reset_led();

    if (!exit_requested()) {
        runtime_state.charge_mode_state = CHARGE_MODE_WAIT_FOR_ZERO;
    }
}

/* ══════════════════════ Main Task ══════════════════════ */

static void charge_mode_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Charge mode task started");

    while (1) {
        switch (runtime_state.charge_mode_state) {
            case CHARGE_MODE_WAIT_FOR_ZERO:
                do_wait_for_zero();
                break;
            case CHARGE_MODE_WAIT_FOR_COMPLETE:
                do_wait_for_complete();
                break;
            case CHARGE_MODE_WAIT_FOR_CUP_REMOVAL:
                do_wait_for_cup_removal();
                break;
            case CHARGE_MODE_WAIT_FOR_CUP_RETURN:
                do_wait_for_cup_return();
                break;
            case CHARGE_MODE_EXIT:
            default:
                // Idle - wait for REST API to start charge mode
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
        }
    }
}

/* ══════════════════════ Init / NVS ══════════════════════ */

esp_err_t charge_mode_init(void)
{
    ESP_LOGI(TAG, "Initializing charge mode module");

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
    if (!config) return ESP_ERR_INVALID_ARG;
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
    }

    runtime_state.charge_mode_state = state;
    return ESP_OK;
}

esp_err_t charge_mode_get_runtime_state(charge_mode_state_t_runtime *state)
{
    if (!state) return ESP_ERR_INVALID_ARG;
    *state = runtime_state;
    return ESP_OK;
}

void charge_mode_clear_events(void)
{
    runtime_state.charge_mode_event = 0;
}

uint32_t hex_string_to_decimal(const char *string)
{
    if (!string) return 0;

    const char *hex_start = string;
    if (string[0] == '%' && string[1] == '2' && string[2] == '3') {
        hex_start = string + 3;
    } else if (string[0] == '#') {
        hex_start = string + 1;
    }

    return strtol(hex_start, NULL, 16);
}
