#include "autotune.h"

#include "motors.h"
#include "profile.h"
#include "scale.h"
#include "flow_model.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char *TAG = "Autotune";

#define AUTOTUNE_STACK_SIZE 5120
#define AUTOTUNE_TASK_PRIO 7

// Fine motor trickle threshold (shared with charge_mode logic)
#define FINE_TRICKLE_THRESHOLD_GN 0.3f

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
// Wait for truly stable zero: two consecutive full buffers must pass stability check.
// This ensures the zero is not a brief dip but genuinely stable.
static bool wait_for_stable_zero(void)
{
    float_buf_t buf;
    float_buf_reset(&buf);
    const int min_wait_ms = 3000;  // minimum wait before first check
    int consecutive_stable = 0;    // need 2 consecutive stable full buffers
    const int required_stable = 2;

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

            TickType_t elapsed_ms = (xTaskGetTickCount() - start) * portTICK_PERIOD_MS;
            if (buf.count >= 10 && elapsed_ms >= (TickType_t)min_wait_ms) {
                float sd = float_buf_sd(&buf);
                if (fabsf(m) < 0.02f && sd < 0.015f) {
                    consecutive_stable++;
                    if (consecutive_stable >= required_stable) {
                        return true;
                    }
                    // Reset buffer for next verification round
                    float_buf_reset(&buf);
                } else {
                    // Not stable yet, reset counter
                    consecutive_stable = 0;
                }
            }
        }

        if ((xTaskGetTickCount() - start) * portTICK_PERIOD_MS > 45000) {
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

        // No timeout for cup removal - wait indefinitely for user
    }

    // Phase 2: RETURN_CUP - wait for weight to increase (cup returned)
    // Small delay to let user empty the cup
    vTaskDelay(pdMS_TO_TICKS(500));

    s_status.substatus = AUTOTUNE_SUB_RETURN_CUP;
    strncpy(s_status.message, "Return cup", sizeof(s_status.message) - 1);

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

        // No timeout for cup return - wait indefinitely for user
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
    const int min_wait_ms = 2000;  // minimum wait before accepting stability
    const int min_readings = 10;   // require more readings for reliable settle
    bool buf_reset_done = false;   // reset buffer once after min_wait to discard rising-phase readings

    TickType_t start = xTaskGetTickCount();
    while (1) {
        float m = 0.0f;
        if (scale_block_wait_for_measurement(200, &m)) {
            settled = m;
            s_status.current_weight = m;

            TickType_t elapsed_ms = (xTaskGetTickCount() - start) * portTICK_PERIOD_MS;

            // After min_wait, reset buffer once to discard old readings from rising phase
            if (!buf_reset_done && elapsed_ms >= (TickType_t)min_wait_ms) {
                float_buf_reset(&buf);
                buf_reset_done = true;
            }

            float_buf_push(&buf, m);

            if (buf_reset_done && buf.count >= min_readings) {
                float sd = float_buf_sd(&buf);
                if (sd < 0.015f) {
                    // Scale is stable, return last reading (not mean, to avoid
                    // averaging in slightly lower earlier values from the window)
                    return settled;
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
                                      float *elapsed_s,
                                      float *max_overshoot)
{
    const float stop_threshold = (motor == MOTOR_FINE) ? 0.02f : 0.03f;

    float last_error = target_weight;
    float peak_weight = 0.0f;  // track max weight during dispensing
    TickType_t start_tick = xTaskGetTickCount();
    TickType_t last_tick = start_tick;
    bool fine_trickle = false;  // fine motor only: crawl at min_speed near target

    if (motor == MOTOR_COARSE) {
        motor_enable(MOTOR_FINE, false);
        motor_enable(MOTOR_COARSE, true);
    } else {
        motor_enable(MOTOR_COARSE, false);
        motor_enable(MOTOR_FINE, true);
    }

    flow_model_record_start(motor);

    while (1) {
        if (s_status.state != AUTOTUNE_STATE_RUNNING) {
            flow_model_record_stop();
            stop_all();
            return false;
        }

        float weight = 0.0f;
        if (!scale_block_wait_for_measurement(200, &weight)) {
            continue;
        }

        // Track peak weight for overshoot calculation
        if (weight > peak_weight) {
            peak_weight = weight;
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
            flow_model_record_stop();
            // Collect post-stop samples for inertia measurement
            for (int si = 0; si < 10; si++) {
                float sw;
                if (scale_block_wait_for_measurement(200, &sw)) {
                    flow_model_record_sample(0.0f, sw);
                }
            }
            // Wait for scale to settle after motor inertia
            *final_w = wait_for_settled_weight(weight, settle_timeout_ms);
            s_status.current_weight = *final_w;
            // Overshoot = how much above target the settled weight is (positive = over)
            if (*final_w > peak_weight) peak_weight = *final_w;
            *max_overshoot = peak_weight - target_weight;
            flow_model_analyze_and_update(profile_get_selected_idx());
            return true;
        }

        // Fine motor: switch to trickle (P-only, no derivative) within FINE_TRICKLE_THRESHOLD_GN.
        // Proportional trickle slows motor near target without derivative noise,
        // and is faster than fixed min_speed (speed scales with remaining error).
        if (motor == MOTOR_FINE && !fine_trickle && error <= FINE_TRICKLE_THRESHOLD_GN) {
            fine_trickle = true;
            ESP_LOGI(TAG, "Fine: trickle at err=%.4f", error);
        }

        float speed;
        float derivative = (error - last_error) / dt_ms;
        if (fine_trickle) {
            // P-only proportional trickle: capped at 30% of max_speed to limit inertia.
            speed = fmaxf(min_speed, fminf(kp * error, max_speed * 0.3f));
            motor_set_speed(motor, speed);
        } else {
            speed = kp * error + kd * derivative;
            speed = fmaxf(min_speed, fminf(speed, max_speed));
            motor_set_speed(motor, speed);
        }
        flow_model_record_sample(speed, weight);

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
            flow_model_record_stop();
            // Collect post-stop samples for inertia measurement
            for (int si = 0; si < 10; si++) {
                float sw;
                if (scale_block_wait_for_measurement(200, &sw)) {
                    flow_model_record_sample(0.0f, sw);
                }
            }
            // Wait for scale to settle after motor inertia
            *final_w = wait_for_settled_weight(weight, settle_timeout_ms);
            s_status.current_weight = *final_w;
            if (*final_w > peak_weight) peak_weight = *final_w;
            *max_overshoot = peak_weight - target_weight;
            flow_model_analyze_and_update(profile_get_selected_idx());
            return true;
        }
    }
}

static bool run_fine_stage_with_coarse_prefill(float coarse_kp,
                                               float coarse_kd,
                                               float fine_kp,
                                               float fine_kd,
                                               float *final_w,
                                               float *fine_elapsed_s,
                                               float *fine_overshoot,
                                               float *coarse_elapsed_out)
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
    float coarse_overshoot = 0.0f;
    bool coarse_ok = run_single_motor_dispense(MOTOR_COARSE,
                                               coarse_kp,
                                               coarse_kd,
                                               s_request.coarse_target_weight,
                                               s_request.total_target_time_s + 15.0f,
                                               coarse_min,
                                               coarse_max,
                                               5000,
                                               &prefill_w,
                                               &coarse_elapsed,
                                               &coarse_overshoot);

    if (!coarse_ok) {
        return false;
    }

    *coarse_elapsed_out = coarse_elapsed;
    (void)coarse_overshoot;

    float fine_w = 0.0f;
    float fine_elapsed = 0.0f;
    float fine_os = 0.0f;
    bool fine_ok = run_single_motor_dispense(MOTOR_FINE,
                                             fine_kp,
                                             fine_kd,
                                             s_request.fine_target_weight,
                                             s_request.total_target_time_s + 15.0f,
                                             fine_min,
                                             fine_max,
                                             8000,
                                             &fine_w,
                                             &fine_elapsed,
                                             &fine_os);

    if (!fine_ok) {
        return false;
    }

    *final_w = fine_w;
    *fine_elapsed_s = fine_elapsed;
    *fine_overshoot = fine_os;
    return true;
}

static void autotune_finish_error(const char *msg)
{
    s_status.state = AUTOTUNE_STATE_ERROR;
    strncpy(s_status.message, msg, sizeof(s_status.message) - 1);
    s_status.message[sizeof(s_status.message) - 1] = '\0';
    stop_all();
}

// ---------------------------------------------------------------------------
// (1+1)-ES Evolutionary Strategy for PID optimization
//
// Works in log-space for Kp/Kd (multiplicative changes are natural for gains).
// Uses lexicographic scoring: weight accuracy FIRST, then time.
// Adaptive step size σ: grows on success, shrinks on failure.
// ---------------------------------------------------------------------------
#define MAX_TRIALS 64

typedef struct {
    uint8_t stage;        // 1 = coarse, 2 = fine
    float kp;
    float kd;
    float weight_error;   // signed: positive = overshoot
    float time_error;     // signed: positive = too slow
    float overshoot;      // max weight above target during dispensing (gn)
    float settled_weight;
    float elapsed_s;
} trial_t;

static trial_t s_trials[MAX_TRIALS];
static int s_trial_count;

// (1+1)-ES state
typedef struct {
    float log_kp;       // current parent in log-space
    float log_kd;
    float sigma_kp;     // step size in log-space
    float sigma_kd;
    float best_log_kp;  // best ever
    float best_log_kd;
    float best_abs_werr;
    float best_abs_terr;
    float best_overshoot;
    int successes;       // consecutive successes (for σ adaptation)
    int failures;        // consecutive failures
} es_state_t;

static es_state_t s_es;

// Simple deterministic perturbation pattern (+1, -1, +1, -1, ...)
// alternating between kp and kd dimensions for systematic exploration
static int s_perturbation_idx;

static void trials_reset(void)
{
    s_trial_count = 0;
    s_perturbation_idx = 0;
}

static void trials_add(uint8_t stage, float kp, float kd, float werr, float terr,
                        float overshoot, float settled_w, float elapsed)
{
    if (s_trial_count < MAX_TRIALS) {
        s_trials[s_trial_count] = (trial_t){
            .stage = stage,
            .kp = kp, .kd = kd,
            .weight_error = werr, .time_error = terr,
            .overshoot = overshoot,
            .settled_weight = settled_w, .elapsed_s = elapsed,
        };
        s_trial_count++;
        ESP_LOGI(TAG, "trials_add: stage=%d count=%d kp=%.4f kd=%.4f werr=%.4f",
                 stage, s_trial_count, kp, kd, werr);
    }
}

// Lexicographic comparison: weight accuracy first, then time.
// Overshoot is penalized heavily (added to weight error).
// Returns true if (w1, t1) is BETTER than (w2, t2).
static bool es_is_better(float abs_werr1, float abs_terr1, float overshoot1,
                          float abs_werr2, float abs_terr2, float overshoot2)
{
    // Effective weight error includes overshoot penalty
    float eff1 = abs_werr1 + fmaxf(0.0f, overshoot1) * 2.0f;
    float eff2 = abs_werr2 + fmaxf(0.0f, overshoot2) * 2.0f;

    // Lexicographic: if weight errors differ by > 0.005gn, that decides
    if (fabsf(eff1 - eff2) > 0.005f) {
        return eff1 < eff2;
    }
    // Weight errors are close enough, compare time
    return abs_terr1 < abs_terr2;
}

static void es_init(float kp, float kd)
{
    s_es.log_kp = logf(fmaxf(kp, 1e-6f));
    s_es.log_kd = logf(fmaxf(kd, 1e-6f));
    // Initial σ: ~30% change per step in multiplicative terms
    // log(1.3) ≈ 0.26
    s_es.sigma_kp = 0.26f;
    s_es.sigma_kd = 0.26f;
    s_es.best_log_kp = s_es.log_kp;
    s_es.best_log_kd = s_es.log_kd;
    s_es.best_abs_werr = 1e9f;
    s_es.best_abs_terr = 1e9f;
    s_es.best_overshoot = 0.0f;
    s_es.successes = 0;
    s_es.failures = 0;
}

// Generate next candidate (kp, kd) from (1+1)-ES.
// Uses alternating Rademacher-like perturbations: systematic +σ/-σ
// on each dimension in turn. This gives better coverage than random.
static void es_generate_candidate(float *kp, float *kd,
                                   float kp_min, float kp_max,
                                   float kd_min, float kd_max)
{
    // Perturbation pattern (4-cycle): +kp, -kp, +kd, -kd
    float d_kp = 0.0f, d_kd = 0.0f;
    int pat = s_perturbation_idx % 4;
    switch (pat) {
        case 0: d_kp = +s_es.sigma_kp; d_kd = +s_es.sigma_kd * 0.5f; break;
        case 1: d_kp = -s_es.sigma_kp; d_kd = -s_es.sigma_kd * 0.5f; break;
        case 2: d_kd = +s_es.sigma_kd; d_kp = +s_es.sigma_kp * 0.5f; break;
        case 3: d_kd = -s_es.sigma_kd; d_kp = -s_es.sigma_kp * 0.5f; break;
    }
    s_perturbation_idx++;

    float new_log_kp = s_es.log_kp + d_kp;
    float new_log_kd = s_es.log_kd + d_kd;

    *kp = clampf(expf(new_log_kp), kp_min, kp_max);
    *kd = clampf(expf(new_log_kd), kd_min, kd_max);
}

// Update ES state after a trial. Returns true if this was a new best.
static bool es_update(float kp, float kd,
                       float abs_werr, float abs_terr, float overshoot)
{
    bool improved = es_is_better(abs_werr, abs_terr, overshoot,
                                  s_es.best_abs_werr, s_es.best_abs_terr,
                                  s_es.best_overshoot);

    if (improved) {
        // Success: move parent to this point
        s_es.log_kp = logf(fmaxf(kp, 1e-6f));
        s_es.log_kd = logf(fmaxf(kd, 1e-6f));
        s_es.best_log_kp = s_es.log_kp;
        s_es.best_log_kd = s_es.log_kd;
        s_es.best_abs_werr = abs_werr;
        s_es.best_abs_terr = abs_terr;
        s_es.best_overshoot = overshoot;
        s_es.successes++;
        s_es.failures = 0;

        // 1/5 success rule: increase σ on success
        if (s_es.successes >= 2) {
            s_es.sigma_kp *= 1.2f;
            s_es.sigma_kd *= 1.2f;
            s_es.successes = 0;
            ESP_LOGI(TAG, "ES: σ increased → σ_kp=%.4f σ_kd=%.4f", s_es.sigma_kp, s_es.sigma_kd);
        }
    } else {
        // Failure: keep parent, shrink σ
        s_es.failures++;
        s_es.successes = 0;

        if (s_es.failures >= 2) {
            s_es.sigma_kp *= 0.8f;
            s_es.sigma_kd *= 0.8f;
            // Minimum σ: ~5% change (log(1.05) ≈ 0.05)
            s_es.sigma_kp = fmaxf(s_es.sigma_kp, 0.05f);
            s_es.sigma_kd = fmaxf(s_es.sigma_kd, 0.05f);
            s_es.failures = 0;
            ESP_LOGI(TAG, "ES: σ decreased → σ_kp=%.4f σ_kd=%.4f", s_es.sigma_kp, s_es.sigma_kd);
        }
    }

    ESP_LOGI(TAG, "ES: improved=%d parent=(%.5f,%.5f) best_werr=%.4f best_terr=%.3f",
             improved, expf(s_es.log_kp), expf(s_es.log_kd),
             s_es.best_abs_werr, s_es.best_abs_terr);

    return improved;
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

    float kp_min = kp_base * 0.10f;
    float kp_max = kp_base * 10.0f;
    float kd_min = kd_base * 0.10f;
    float kd_max = kd_base * 10.0f;

    bool tolerance_reached = false;

    s_perturbation_idx = 0;
    es_init(kp_base, kd_base);

    // First run uses base parameters (the parent)
    float kp = kp_base;
    float kd = kd_base;

    for (int run = 1; run <= s_request.max_runs_per_stage; run++) {
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
        float overshoot = 0.0f;
        bool ok = run_single_motor_dispense(MOTOR_COARSE,
                                            kp, kd,
                                            s_request.coarse_target_weight,
                                            s_request.total_target_time_s + 15.0f,
                                            min_speed, max_speed,
                                            5000,
                                            &final_w, &elapsed_s, &overshoot);
        if (!ok) {
            return false;
        }

        s_status.last_weight = final_w;
        s_status.last_elapsed_s = elapsed_s;

        float werr = final_w - s_request.coarse_target_weight;
        float abs_werr = fabsf(werr);
        // Coarse time: one-sided vs total budget (being fast is always ok; slow is excess)
        float es_terr = fmaxf(0.0f, elapsed_s - s_request.total_target_time_s);

        // Record trial (time_error = coarse elapsed relative to total target, informational)
        trials_add(1, kp, kd, werr, elapsed_s - s_request.total_target_time_s, overshoot, final_w, elapsed_s);

        // Update ES and track best (time is one-sided tiebreaker only)
        bool improved = es_update(kp, kd, abs_werr, es_terr, overshoot);
        if (improved || run == 1) {
            *best_kp = kp;
            *best_kd = kd;
            *best_abs_werr = abs_werr;
            *best_abs_terr = elapsed_s;  // store coarse elapsed for reference
            s_status.coarse_best_kp = kp;
            s_status.coarse_best_kd = kd;
            s_status.coarse_best_weight_error = abs_werr;
            s_status.coarse_best_time_error = elapsed_s;
        }

        s_status.runs_done++;
        s_status.progress_pct = ((float)s_status.runs_done / (float)s_status.runs_total) * 100.0f;

        ESP_LOGI(TAG, "COARSE run %d/%d: kp=%.5f kd=%.5f w=%.4f t=%.2f werr=%.4f os=%.4f",
                 run, s_request.max_runs_per_stage, kp, kd, final_w, elapsed_s, abs_werr, overshoot);

        // Coarse tolerance: weight only (time is evaluated at total cycle level in fine stage)
        if (abs_werr <= s_request.coarse_weight_tolerance) {
            ESP_LOGI(TAG, "COARSE tuned to weight tolerance on run %d", run);
            tolerance_reached = true;
            break;
        }

        // Generate next candidate using (1+1)-ES
        es_generate_candidate(&kp, &kd, kp_min, kp_max, kd_min, kd_max);
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

    float kp_min = kp_base * 0.10f;
    float kp_max = kp_base * 10.0f;
    float kd_min = kd_base * 0.10f;
    float kd_max = kd_base * 10.0f;

    bool tolerance_reached = false;

    s_perturbation_idx = 0;
    es_init(kp_base, kd_base);

    float kp = kp_base;
    float kd = kd_base;

    for (int run = 1; run <= s_request.max_runs_per_stage; run++) {
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
        float overshoot = 0.0f;
        float actual_coarse_elapsed = 0.0f;
        bool ok = run_fine_stage_with_coarse_prefill(coarse_kp, coarse_kd,
                                                     kp, kd,
                                                     &final_w, &fine_elapsed,
                                                     &overshoot,
                                                     &actual_coarse_elapsed);
        if (!ok) {
            return false;
        }

        float total_elapsed = actual_coarse_elapsed + fine_elapsed;
        s_status.last_weight = final_w;
        s_status.last_elapsed_s = total_elapsed;

        float werr = final_w - s_request.fine_target_weight;
        float abs_werr = fabsf(werr);
        // Total cycle time vs target: negative = under (always ok), positive = over budget
        float terr = total_elapsed - s_request.total_target_time_s;
        // One-sided with tolerance zone: excess within time_tolerance_s is free (no ES penalty).
        float es_terr = fmaxf(0.0f, terr - s_request.time_tolerance_s);

        trials_add(2, kp, kd, werr, terr, overshoot, final_w, total_elapsed);

        bool improved = es_update(kp, kd, abs_werr, es_terr, overshoot);
        if (improved || run == 1) {
            *best_kp = kp;
            *best_kd = kd;
            *best_abs_werr = abs_werr;
            *best_abs_terr = fabsf(terr);
            s_status.fine_best_kp = kp;
            s_status.fine_best_kd = kd;
            s_status.fine_best_weight_error = abs_werr;
            s_status.fine_best_time_error = terr;
        }

        s_status.runs_done++;
        s_status.progress_pct = ((float)s_status.runs_done / (float)s_status.runs_total) * 100.0f;

        ESP_LOGI(TAG, "FINE run %d/%d: kp=%.5f kd=%.5f w=%.4f t_fine=%.2f t_total=%.2f werr=%.4f terr=%.3f os=%.4f",
                 run, s_request.max_runs_per_stage, kp, kd, final_w, fine_elapsed, total_elapsed, abs_werr, terr, overshoot);

        // Tolerance: weight accuracy only — time is optimized by ES cost, not a hard gate.
        // This prevents autotune from failing when physics makes time target unreachable.
        if (abs_werr <= s_request.fine_weight_tolerance) {
            ESP_LOGI(TAG, "FINE tuned to tolerance on run %d (total=%.2fs)", run, total_elapsed);
            tolerance_reached = true;
            break;
        }

        es_generate_candidate(&kp, &kd, kp_min, kp_max, kd_min, kd_max);
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

    trials_reset();

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
        request->fine_target_weight <= 0.01f ||
        request->total_target_time_s <= 0.1f ||
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

int autotune_get_trials(autotune_trial_result_t *out, int max_count)
{
    int count = (s_trial_count < max_count) ? s_trial_count : max_count;
    for (int i = 0; i < count; i++) {
        out[i].stage = s_trials[i].stage;
        out[i].kp = s_trials[i].kp;
        out[i].kd = s_trials[i].kd;
        out[i].weight_error = s_trials[i].weight_error;
        out[i].time_error = s_trials[i].time_error;
        out[i].overshoot = s_trials[i].overshoot;
        out[i].settled_weight = s_trials[i].settled_weight;
        out[i].elapsed_s = s_trials[i].elapsed_s;
    }
    return count;
}
