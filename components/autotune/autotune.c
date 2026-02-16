#include "autotune.h"

#include "motors.h"
#include "profile.h"
#include "scale.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char *TAG = "Autotune";

#define AUTOTUNE_STACK_SIZE 4096
#define AUTOTUNE_TASK_PRIO 7

typedef struct {
    float data[10];
    int head;
    int count;
} float_buf_t;

static autotune_status_t s_status = {
    .state = AUTOTUNE_STATE_IDLE,
};
static autotune_request_t s_request = {0};
static TaskHandle_t s_task_handle = NULL;

static inline float clampf(float value, float min_v, float max_v)
{
    if (value < min_v) return min_v;
    if (value > max_v) return max_v;
    return value;
}

static void float_buf_reset(float_buf_t *buf)
{
    buf->head = 0;
    buf->count = 0;
}

static void float_buf_push(float_buf_t *buf, float v)
{
    buf->data[buf->head] = v;
    buf->head = (buf->head + 1) % 10;
    if (buf->count < 10) {
        buf->count++;
    }
}

static float float_buf_mean(const float_buf_t *buf)
{
    if (buf->count == 0) return 0.0f;
    float sum = 0.0f;
    for (int i = 0; i < buf->count; i++) {
        sum += buf->data[i];
    }
    return sum / (float)buf->count;
}

static float float_buf_sd(const float_buf_t *buf)
{
    if (buf->count < 2) return 999.0f;
    float mean = float_buf_mean(buf);
    float acc = 0.0f;
    for (int i = 0; i < buf->count; i++) {
        float d = buf->data[i] - mean;
        acc += d * d;
    }
    return sqrtf(acc / (float)buf->count);
}

// Wait for scale to stabilize near zero (no cup cycle - used for initial wait)
static bool wait_for_stable_zero(void)
{
    float_buf_t buf;
    float_buf_reset(&buf);

    s_status.substatus = AUTOTUNE_SUB_STABILIZING;

    TickType_t start = xTaskGetTickCount();
    while (1) {
        if (s_status.state != AUTOTUNE_STATE_RUNNING) {
            return false;
        }

        float m = 0.0f;
        if (scale_block_wait_for_measurement(300, &m)) {
            s_status.current_weight = m;
            float_buf_push(&buf, m);
            if (buf.count >= 8) {
                float mean = float_buf_mean(&buf);
                float sd = float_buf_sd(&buf);
                if (fabsf(mean) < 0.02f && sd < 0.02f) {
                    return true;
                }
            }
        }

        if ((xTaskGetTickCount() - start) * portTICK_PERIOD_MS > 25000) {
            return false;
        }
    }
}

// Cup removal/return cycle: after dispensing, wait for user to remove cup,
// then return it, then stabilize at zero - similar to charge mode cycle
static bool wait_cup_removal_return_cycle(void)
{
    // Phase 1: REMOVE_CUP - wait for weight to drop significantly (cup removed)
    s_status.substatus = AUTOTUNE_SUB_REMOVE_CUP;
    strncpy(s_status.message, "Remove cup", sizeof(s_status.message) - 1);

    TickType_t start = xTaskGetTickCount();
    bool cup_removed = false;
    while (!cup_removed) {
        if (s_status.state != AUTOTUNE_STATE_RUNNING) {
            return false;
        }

        float m = 0.0f;
        if (scale_block_wait_for_measurement(300, &m)) {
            s_status.current_weight = m;
            // Cup is considered removed when weight drops below 0.5g
            if (m < 0.5f) {
                cup_removed = true;
            }
        }

        if ((xTaskGetTickCount() - start) * portTICK_PERIOD_MS > 60000) {
            // 60s timeout waiting for cup removal
            return false;
        }
    }

    // Phase 2: RETURN_CUP - wait for weight to increase (cup returned)
    // Small delay to let user empty the cup
    vTaskDelay(pdMS_TO_TICKS(500));

    s_status.substatus = AUTOTUNE_SUB_RETURN_CUP;
    strncpy(s_status.message, "Return cup", sizeof(s_status.message) - 1);

    start = xTaskGetTickCount();
    bool cup_returned = false;
    while (!cup_returned) {
        if (s_status.state != AUTOTUNE_STATE_RUNNING) {
            return false;
        }

        float m = 0.0f;
        if (scale_block_wait_for_measurement(300, &m)) {
            s_status.current_weight = m;
            // Cup returned when any positive weight detected (cup on scale)
            // But we accept near-zero too since an empty cup is very light
            // We detect "returned" by checking for a brief stable reading
            if (fabsf(m) < 2.0f && m > -0.5f) {
                // Something is on the scale, now wait for stability
                cup_returned = true;
            }
        }

        if ((xTaskGetTickCount() - start) * portTICK_PERIOD_MS > 60000) {
            return false;
        }
    }

    // Phase 3: STABILIZING - wait for stable zero reading
    s_status.substatus = AUTOTUNE_SUB_STABILIZING;
    strncpy(s_status.message, "Stabilizing scale...", sizeof(s_status.message) - 1);

    return wait_for_stable_zero();
}

static void stop_all(void)
{
    motor_set_speed(MOTOR_COARSE, 0.0f);
    motor_set_speed(MOTOR_FINE, 0.0f);
    motor_enable(MOTOR_COARSE, false);
    motor_enable(MOTOR_FINE, false);
}

// Wait for scale to settle after motor stop (inertia causes weight to keep rising).
// Takes multiple readings and returns when stable, or after timeout.
static float wait_for_settled_weight(float initial_weight, int timeout_ms)
{
    float_buf_t buf;
    float_buf_reset(&buf);
    float settled = initial_weight;
    const int min_wait_ms = 1500;  // minimum wait before accepting stability
    const int min_readings = 10;   // require more readings for reliable settle

    TickType_t start = xTaskGetTickCount();
    while (1) {
        float m = 0.0f;
        if (scale_block_wait_for_measurement(200, &m)) {
            float_buf_push(&buf, m);
            settled = m;
            s_status.current_weight = m;

            TickType_t elapsed_ms = (xTaskGetTickCount() - start) * portTICK_PERIOD_MS;
            if (buf.count >= min_readings && elapsed_ms >= (TickType_t)min_wait_ms) {
                float sd = float_buf_sd(&buf);
                if (sd < 0.015f) {
                    // Scale is stable, return mean
                    return float_buf_mean(&buf);
                }
            }
        }

        if ((xTaskGetTickCount() - start) * portTICK_PERIOD_MS > (TickType_t)timeout_ms) {
            // Timeout - return last reading
            return settled;
        }
    }
}

static bool run_single_motor_dispense(motor_type_t motor,
                                      float kp,
                                      float kd,
                                      float target_weight,
                                      float timeout_s,
                                      float min_speed,
                                      float max_speed,
                                      int settle_timeout_ms,
                                      float *final_w,
                                      float *elapsed_s)
{
    const float stop_threshold = 0.03f;

    float last_error = target_weight;
    TickType_t start_tick = xTaskGetTickCount();
    TickType_t last_tick = start_tick;

    if (motor == MOTOR_COARSE) {
        motor_enable(MOTOR_FINE, false);
        motor_enable(MOTOR_COARSE, true);
    } else {
        motor_enable(MOTOR_COARSE, false);
        motor_enable(MOTOR_FINE, true);
    }

    while (1) {
        if (s_status.state != AUTOTUNE_STATE_RUNNING) {
            stop_all();
            return false;
        }

        float weight = 0.0f;
        if (!scale_block_wait_for_measurement(200, &weight)) {
            continue;
        }

        TickType_t now = xTaskGetTickCount();
        float dt_ms = (float)((now - last_tick) * portTICK_PERIOD_MS);
        if (dt_ms < 1.0f) {
            dt_ms = 1.0f;
        }

        float cur_elapsed = (float)((now - start_tick) * portTICK_PERIOD_MS) / 1000.0f;

        // Update live readings for UI
        s_status.current_weight = weight;
        s_status.current_elapsed_s = cur_elapsed;

        float error = target_weight - weight;
        if (error <= stop_threshold) {
            *elapsed_s = cur_elapsed;
            if (motor == MOTOR_COARSE) {
                motor_set_speed(MOTOR_COARSE, 0.0f);
                motor_enable(MOTOR_COARSE, false);
            } else {
                motor_set_speed(MOTOR_FINE, 0.0f);
                motor_enable(MOTOR_FINE, false);
            }
            // Wait for scale to settle after motor inertia
            *final_w = wait_for_settled_weight(weight, settle_timeout_ms);
            s_status.current_weight = *final_w;
            return true;
        }

        float derivative = (error - last_error) / dt_ms;
        float speed = kp * error + kd * derivative;
        speed = fmaxf(min_speed, fminf(speed, max_speed));
        motor_set_speed(motor, speed);

        last_error = error;
        last_tick = now;

        if (cur_elapsed > timeout_s) {
            *elapsed_s = cur_elapsed;
            if (motor == MOTOR_COARSE) {
                motor_set_speed(MOTOR_COARSE, 0.0f);
                motor_enable(MOTOR_COARSE, false);
            } else {
                motor_set_speed(MOTOR_FINE, 0.0f);
                motor_enable(MOTOR_FINE, false);
            }
            // Wait for scale to settle after motor inertia
            *final_w = wait_for_settled_weight(weight, settle_timeout_ms);
            s_status.current_weight = *final_w;
            return true;
        }
    }
}

static bool run_fine_stage_with_coarse_prefill(float coarse_kp,
                                               float coarse_kd,
                                               float fine_kp,
                                               float fine_kd,
                                               float *final_w,
                                               float *fine_elapsed_s)
{
    profile_t *profile = profile_get_selected();
    if (!profile) {
        return false;
    }

    motor_config_t coarse_cfg;
    motor_config_t fine_cfg;
    motors_get_config(MOTOR_COARSE, &coarse_cfg);
    motors_get_config(MOTOR_FINE, &fine_cfg);

    const float coarse_min = fmaxf(coarse_cfg.min_speed_rps, profile->coarse_min_flow_speed_rps);
    const float coarse_max = fminf((float)coarse_cfg.max_speed_rps, profile->coarse_max_flow_speed_rps);
    const float fine_min = fmaxf(fine_cfg.min_speed_rps, profile->fine_min_flow_speed_rps);
    const float fine_max = fminf((float)fine_cfg.max_speed_rps, profile->fine_max_flow_speed_rps);

    float prefill_w = 0.0f;
    float coarse_elapsed = 0.0f;
    bool coarse_ok = run_single_motor_dispense(MOTOR_COARSE,
                                               coarse_kp,
                                               coarse_kd,
                                               s_request.coarse_target_weight,
                                               s_request.coarse_target_time_s * 1.7f + 5.0f,
                                               coarse_min,
                                               coarse_max,
                                               5000,
                                               &prefill_w,
                                               &coarse_elapsed);

    if (!coarse_ok) {
        return false;
    }

    (void)coarse_elapsed;

    float fine_w = 0.0f;
    float fine_elapsed = 0.0f;
    bool fine_ok = run_single_motor_dispense(MOTOR_FINE,
                                             fine_kp,
                                             fine_kd,
                                             s_request.fine_target_weight,
                                             s_request.fine_target_time_s * 1.7f + 5.0f,
                                             fine_min,
                                             fine_max,
                                             8000,
                                             &fine_w,
                                             &fine_elapsed);

    if (!fine_ok) {
        return false;
    }

    *final_w = fine_w;
    *fine_elapsed_s = fine_elapsed;
    return true;
}

static void autotune_finish_error(const char *msg)
{
    s_status.state = AUTOTUNE_STATE_ERROR;
    strncpy(s_status.message, msg, sizeof(s_status.message) - 1);
    s_status.message[sizeof(s_status.message) - 1] = '\0';
    stop_all();
}

static float compute_score(float abs_weight_error, float abs_time_error)
{
    return abs_weight_error * 100.0f + abs_time_error;
}

static void adapt_pid(float target_weight,
                      float target_time_s,
                      float final_w,
                      float elapsed_s,
                      float *kp,
                      float *kd,
                      float kp_min,
                      float kp_max,
                      float kd_min,
                      float kd_max)
{
    float weight_error = final_w - target_weight;      // +over / -under
    float time_error = elapsed_s - target_time_s;      // +slow / -fast

    float target_time_guard = fmaxf(target_time_s, 0.2f);
    float weight_norm = weight_error / fmaxf(target_weight * 0.05f, 0.02f);
    float time_norm = time_error / target_time_guard;

    float kp_factor = 1.0f + 0.18f * time_norm - 0.05f * weight_norm;
    float kd_factor = 1.0f + 0.28f * weight_norm;

    *kp *= clampf(kp_factor, 0.75f, 1.30f);
    *kd *= clampf(kd_factor, 0.70f, 1.35f);

    *kp = clampf(*kp, kp_min, kp_max);
    *kd = clampf(*kd, kd_min, kd_max);
}

static bool tune_coarse_stage(profile_t *profile,
                              float *best_kp,
                              float *best_kd,
                              float *best_abs_werr,
                              float *best_abs_terr)
{
    motor_config_t coarse_cfg;
    motors_get_config(MOTOR_COARSE, &coarse_cfg);

    float min_speed = fmaxf(coarse_cfg.min_speed_rps, profile->coarse_min_flow_speed_rps);
    float max_speed = fminf((float)coarse_cfg.max_speed_rps, profile->coarse_max_flow_speed_rps);

    float kp_base = fmaxf(0.0001f, profile->coarse_kp);
    float kd_base = fmaxf(0.0001f, profile->coarse_kd);

    float kp = kp_base;
    float kd = kd_base;

    float kp_min = kp_base * 0.20f;
    float kp_max = kp_base * 5.00f;
    float kd_min = kd_base * 0.20f;
    float kd_max = kd_base * 5.00f;

    float best_score = 1e9f;
    bool tolerance_reached = false;

    for (int run = 1; run <= s_request.max_runs_per_stage; run++) {
        // First run: just wait for stable zero; subsequent runs: cup removal/return cycle
        if (run == 1) {
            strncpy(s_status.message, "Waiting for stable reading...", sizeof(s_status.message) - 1);
            if (!wait_for_stable_zero()) {
                return false;
            }
        } else {
            if (!wait_cup_removal_return_cycle()) {
                return false;
            }
        }

        s_status.stage = AUTOTUNE_STAGE_COARSE;
        s_status.stage_run = run;
        s_status.stage_max_runs = s_request.max_runs_per_stage;
        s_status.active_kp = kp;
        s_status.active_kd = kd;
        s_status.substatus = AUTOTUNE_SUB_DISPENSING;
        snprintf(s_status.message, sizeof(s_status.message), "COARSE run %d/%d", run, s_request.max_runs_per_stage);

        float final_w = 0.0f;
        float elapsed_s = 0.0f;
        bool ok = run_single_motor_dispense(MOTOR_COARSE,
                                            kp,
                                            kd,
                                            s_request.coarse_target_weight,
                                            s_request.coarse_target_time_s * 1.7f + 5.0f,
                                            min_speed,
                                            max_speed,
                                            5000,
                                            &final_w,
                                            &elapsed_s);
        if (!ok) {
            return false;
        }

        s_status.last_weight = final_w;
        s_status.last_elapsed_s = elapsed_s;

        float abs_werr = fabsf(final_w - s_request.coarse_target_weight);
        float abs_terr = fabsf(elapsed_s - s_request.coarse_target_time_s);
        float score = compute_score(abs_werr, abs_terr);

        if (score < best_score) {
            best_score = score;
            *best_kp = kp;
            *best_kd = kd;
            *best_abs_werr = abs_werr;
            *best_abs_terr = abs_terr;
            s_status.coarse_best_kp = kp;
            s_status.coarse_best_kd = kd;
            s_status.coarse_best_weight_error = abs_werr;
            s_status.coarse_best_time_error = abs_terr;
        }

        s_status.runs_done++;
        s_status.progress_pct = ((float)s_status.runs_done / (float)s_status.runs_total) * 100.0f;

        ESP_LOGI(TAG, "COARSE run %d/%d: kp=%.5f kd=%.5f weight=%.4f t=%.2f err_w=%.4f err_t=%.3f",
                 run, s_request.max_runs_per_stage, kp, kd, final_w, elapsed_s, abs_werr, abs_terr);

        if (abs_werr <= s_request.coarse_weight_tolerance && abs_terr <= s_request.time_tolerance_s) {
            ESP_LOGI(TAG, "COARSE tuned to tolerance on run %d", run);
            tolerance_reached = true;
            break;
        }

        adapt_pid(s_request.coarse_target_weight,
                  s_request.coarse_target_time_s,
                  final_w,
                  elapsed_s,
                  &kp,
                  &kd,
                  kp_min,
                  kp_max,
                  kd_min,
                  kd_max);
    }

    return tolerance_reached;
}

static bool tune_fine_stage(profile_t *profile,
                            float coarse_kp,
                            float coarse_kd,
                            float *best_kp,
                            float *best_kd,
                            float *best_abs_werr,
                            float *best_abs_terr)
{
    float kp_base = fmaxf(0.0001f, profile->fine_kp);
    float kd_base = fmaxf(0.0001f, profile->fine_kd);

    float kp = kp_base;
    float kd = kd_base;

    float kp_min = kp_base * 0.20f;
    float kp_max = kp_base * 5.00f;
    float kd_min = kd_base * 0.20f;
    float kd_max = kd_base * 5.00f;

    float best_score = 1e9f;
    bool tolerance_reached = false;

    for (int run = 1; run <= s_request.max_runs_per_stage; run++) {
        // Every fine run needs cup removal/return cycle
        // (previous run or coarse stage left powder on scale)
        if (!wait_cup_removal_return_cycle()) {
            return false;
        }

        s_status.stage = AUTOTUNE_STAGE_FINE;
        s_status.stage_run = run;
        s_status.stage_max_runs = s_request.max_runs_per_stage;
        s_status.active_kp = kp;
        s_status.active_kd = kd;
        s_status.substatus = AUTOTUNE_SUB_DISPENSING;
        snprintf(s_status.message, sizeof(s_status.message), "FINE run %d/%d", run, s_request.max_runs_per_stage);

        float final_w = 0.0f;
        float fine_elapsed = 0.0f;
        bool ok = run_fine_stage_with_coarse_prefill(coarse_kp,
                                                     coarse_kd,
                                                     kp,
                                                     kd,
                                                     &final_w,
                                                     &fine_elapsed);
        if (!ok) {
            return false;
        }

        s_status.last_weight = final_w;
        s_status.last_elapsed_s = fine_elapsed;

        float abs_werr = fabsf(final_w - s_request.fine_target_weight);
        float abs_terr = fabsf(fine_elapsed - s_request.fine_target_time_s);
        float score = compute_score(abs_werr, abs_terr);

        if (score < best_score) {
            best_score = score;
            *best_kp = kp;
            *best_kd = kd;
            *best_abs_werr = abs_werr;
            *best_abs_terr = abs_terr;
            s_status.fine_best_kp = kp;
            s_status.fine_best_kd = kd;
            s_status.fine_best_weight_error = abs_werr;
            s_status.fine_best_time_error = abs_terr;
        }

        s_status.runs_done++;
        s_status.progress_pct = ((float)s_status.runs_done / (float)s_status.runs_total) * 100.0f;

        ESP_LOGI(TAG, "FINE run %d/%d: kp=%.5f kd=%.5f weight=%.4f t=%.2f err_w=%.4f err_t=%.3f",
                 run, s_request.max_runs_per_stage, kp, kd, final_w, fine_elapsed, abs_werr, abs_terr);

        if (abs_werr <= s_request.fine_weight_tolerance && abs_terr <= s_request.time_tolerance_s) {
            ESP_LOGI(TAG, "FINE tuned to tolerance on run %d", run);
            tolerance_reached = true;
            break;
        }

        adapt_pid(s_request.fine_target_weight,
                  s_request.fine_target_time_s,
                  final_w,
                  fine_elapsed,
                  &kp,
                  &kd,
                  kp_min,
                  kp_max,
                  kd_min,
                  kd_max);
    }

    return tolerance_reached;
}

static void autotune_task(void *arg)
{
    (void)arg;

    profile_t *profile = profile_get_selected();
    if (!profile) {
        autotune_finish_error("No active profile");
        s_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    s_status.runs_total = s_request.max_runs_per_stage * 2;
    s_status.runs_done = 0;

    float best_coarse_kp = profile->coarse_kp;
    float best_coarse_kd = profile->coarse_kd;
    float best_coarse_werr = 999.0f;
    float best_coarse_terr = 999.0f;

    if (!tune_coarse_stage(profile,
                           &best_coarse_kp,
                           &best_coarse_kd,
                           &best_coarse_werr,
                           &best_coarse_terr)) {
        autotune_finish_error("COARSE did not reach tolerance within run limit");
        s_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    float best_fine_kp = profile->fine_kp;
    float best_fine_kd = profile->fine_kd;
    float best_fine_werr = 999.0f;
    float best_fine_terr = 999.0f;

    if (!tune_fine_stage(profile,
                         best_coarse_kp,
                         best_coarse_kd,
                         &best_fine_kp,
                         &best_fine_kd,
                         &best_fine_werr,
                         &best_fine_terr)) {
        autotune_finish_error("FINE did not reach tolerance within run limit");
        s_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    if (s_request.auto_apply) {
        profile->coarse_kp = best_coarse_kp;
        profile->coarse_kd = best_coarse_kd;
        profile->fine_kp = best_fine_kp;
        profile->fine_kd = best_fine_kd;

        if (s_request.save_to_nvs) {
            profile_save();
        }
    }

    s_status.stage = AUTOTUNE_STAGE_NONE;
    s_status.substatus = AUTOTUNE_SUB_IDLE;
    s_status.state = AUTOTUNE_STATE_DONE;
    s_status.progress_pct = 100.0f;
    strncpy(s_status.message, "Autotune coarse+fine completed", sizeof(s_status.message) - 1);
    s_status.message[sizeof(s_status.message) - 1] = '\0';

    stop_all();
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t autotune_init(void)
{
    memset(&s_request, 0, sizeof(s_request));
    memset(&s_status, 0, sizeof(s_status));

    s_status.state = AUTOTUNE_STATE_IDLE;
    s_status.stage = AUTOTUNE_STAGE_NONE;
    s_status.substatus = AUTOTUNE_SUB_IDLE;
    s_status.progress_pct = 0.0f;
    strncpy(s_status.message, "Idle", sizeof(s_status.message) - 1);

    return ESP_OK;
}

esp_err_t autotune_start(const autotune_request_t *request)
{
    if (!request) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_status.state == AUTOTUNE_STATE_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }

    if (request->coarse_target_weight <= 0.01f ||
        request->coarse_target_time_s <= 0.1f ||
        request->fine_target_weight <= 0.01f ||
        request->fine_target_time_s <= 0.1f ||
        request->max_runs_per_stage < 1 ||
        request->coarse_weight_tolerance <= 0.0f ||
        request->fine_weight_tolerance <= 0.0f ||
        request->time_tolerance_s <= 0.0f) {
        return ESP_ERR_INVALID_ARG;
    }

    s_request = *request;

    memset(&s_status, 0, sizeof(s_status));
    s_status.state = AUTOTUNE_STATE_RUNNING;
    s_status.stage = AUTOTUNE_STAGE_COARSE;
    s_status.stage_max_runs = s_request.max_runs_per_stage;
    strncpy(s_status.message, "Autotune coarse+fine in progress", sizeof(s_status.message) - 1);

    BaseType_t ret = xTaskCreate(autotune_task,
                                 "autotune",
                                 AUTOTUNE_STACK_SIZE,
                                 NULL,
                                 AUTOTUNE_TASK_PRIO,
                                 &s_task_handle);
    if (ret != pdPASS) {
        s_status.state = AUTOTUNE_STATE_ERROR;
        strncpy(s_status.message, "Failed to start autotune task", sizeof(s_status.message) - 1);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t autotune_get_status(autotune_status_t *status)
{
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }
    *status = s_status;
    return ESP_OK;
}

esp_err_t autotune_cancel(void)
{
    if (s_status.state != AUTOTUNE_STATE_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }

    s_status.state = AUTOTUNE_STATE_IDLE;
    s_status.stage = AUTOTUNE_STAGE_NONE;
    s_status.substatus = AUTOTUNE_SUB_IDLE;
    s_status.progress_pct = 0.0f;
    strncpy(s_status.message, "Autotune cancelled", sizeof(s_status.message) - 1);

    stop_all();
    return ESP_OK;
}
