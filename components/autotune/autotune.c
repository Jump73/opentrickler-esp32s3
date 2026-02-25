#include "autotune.h"

#include "motors.h"
#include "profile.h"
#include "scale.h"
#include "flow_model.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <math.h>
#include <string.h>

static const char *TAG = "Autotune";

#define AUTOTUNE_STACK_SIZE 5120
#define AUTOTUNE_TASK_PRIO 7

// Fine motor trickle threshold (shared with charge_mode logic)
#define FINE_TRICKLE_THRESHOLD_GN 0.3f
// Coarse target must stay below fine target by at least this margin.
#define AUTOTUNE_STAGE_TARGET_MIN_GAP_GN 0.05f
#define AUTOTUNE_COARSE_STABLE_CONFIRM_RUNS 2
#define AUTOTUNE_FINE_STABLE_CONFIRM_RUNS 3
#define AUTOTUNE_SPEED_PROBE_ATTEMPTS 3
#define AUTOTUNE_SPEED_PROBE_GAIN_STEP 0.12f
#define AUTOTUNE_MIN_SPEED_GAIN_S 0.10f
#define AUTOTUNE_TELEMETRY_MAX 128
#define AUTOTUNE_MIN_ACCEPT_QUALITY 0.45f

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
static SemaphoreHandle_t s_autotune_mutex = NULL;
static autotune_telemetry_entry_t s_telemetry[AUTOTUNE_TELEMETRY_MAX];
static int s_telemetry_count = 0;
static bool s_finish_now_requested = false;

static inline void autotune_lock(void)
{
    if (s_autotune_mutex) {
        xSemaphoreTake(s_autotune_mutex, portMAX_DELAY);
    }
}

static inline void autotune_unlock(void)
{
    if (s_autotune_mutex) {
        xSemaphoreGive(s_autotune_mutex);
    }
}

static inline bool autotune_is_running(void)
{
    bool running;
    autotune_lock();
    running = (s_status.state == AUTOTUNE_STATE_RUNNING);
    autotune_unlock();
    return running;
}

static bool autotune_take_finish_now_request(void)
{
    bool requested = false;
    autotune_lock();
    if (s_finish_now_requested) {
        requested = true;
        s_finish_now_requested = false;
    }
    autotune_unlock();
    return requested;
}

static inline void autotune_set_current_weight(float weight)
{
    autotune_lock();
    s_status.current_weight = weight;
    autotune_unlock();
}

static void telemetry_reset(void)
{
    autotune_lock();
    s_telemetry_count = 0;
    memset(s_telemetry, 0, sizeof(s_telemetry));
    autotune_unlock();
}

static void telemetry_add(uint8_t stage, uint8_t phase, float kp, float kd,
                          float settled_weight, float elapsed_s, float overshoot,
                          float abs_weight_error, float quality, bool accepted)
{
    autotune_lock();
    if (s_telemetry_count < AUTOTUNE_TELEMETRY_MAX) {
        s_telemetry[s_telemetry_count++] = (autotune_telemetry_entry_t){
            .timestamp_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
            .stage = stage,
            .phase = phase,
            .kp = kp,
            .kd = kd,
            .settled_weight = settled_weight,
            .elapsed_s = elapsed_s,
            .overshoot = overshoot,
            .abs_weight_error = abs_weight_error,
            .quality = quality,
            .accepted = accepted,
        };
    }
    autotune_unlock();
}

static float telemetry_recent_quality_mean(uint8_t stage, int max_samples)
{
    float sum = 0.0f;
    int count = 0;

    autotune_lock();
    for (int i = s_telemetry_count - 1; i >= 0 && count < max_samples; i--) {
        if (s_telemetry[i].stage == stage) {
            sum += s_telemetry[i].quality;
            count++;
        }
    }
    autotune_unlock();

    if (count == 0) {
        return 0.8f;
    }
    return sum / (float)count;
}

static float get_stage_quality(uint8_t stage)
{
    flow_model_t fm = {0};
    if (flow_model_get(profile_get_selected_idx(), &fm) != ESP_OK) {
        return 0.0f;
    }
    if (stage == AUTOTUNE_STAGE_COARSE) {
        return fm.coarse.last_quality;
    }
    return fm.fine.last_quality;
}

static inline float clampf(float value, float min_v, float max_v)
{
    if (value < min_v) return min_v;
    if (value > max_v) return max_v;
    return value;
}

static inline void apply_fine_over_guard(float *kp_io,
                                         float *kd_io,
                                         float kp_min,
                                         float kp_ceiling,
                                         float kd_floor,
                                         float kd_max)
{
    *kp_io = clampf(*kp_io, kp_min, kp_ceiling);
    *kd_io = clampf(*kd_io, kd_floor, kd_max);
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

    autotune_lock();
    s_status.substatus = AUTOTUNE_SUB_STABILIZING;
    autotune_unlock();

    TickType_t start = xTaskGetTickCount();
    while (1) {
        if (!autotune_is_running()) {
            return false;
        }

        float m = 0.0f;
        if (scale_block_wait_for_measurement(300, &m)) {
            autotune_set_current_weight(m);
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
    autotune_lock();
    s_status.substatus = AUTOTUNE_SUB_REMOVE_CUP;
    strncpy(s_status.message, "Remove cup", sizeof(s_status.message) - 1);
    s_status.message[sizeof(s_status.message) - 1] = '\0';
    autotune_unlock();

    bool cup_removed = false;
    while (!cup_removed) {
        if (!autotune_is_running()) {
            return false;
        }

        float m = 0.0f;
        if (scale_block_wait_for_measurement(300, &m)) {
            autotune_set_current_weight(m);
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

    autotune_lock();
    s_status.substatus = AUTOTUNE_SUB_RETURN_CUP;
    strncpy(s_status.message, "Return cup", sizeof(s_status.message) - 1);
    s_status.message[sizeof(s_status.message) - 1] = '\0';
    autotune_unlock();

    bool cup_returned = false;
    while (!cup_returned) {
        if (!autotune_is_running()) {
            return false;
        }

        float m = 0.0f;
        if (scale_block_wait_for_measurement(300, &m)) {
            autotune_set_current_weight(m);
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
    autotune_lock();
    s_status.substatus = AUTOTUNE_SUB_STABILIZING;
    strncpy(s_status.message, "Stabilizing scale...", sizeof(s_status.message) - 1);
    s_status.message[sizeof(s_status.message) - 1] = '\0';
    autotune_unlock();

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
            autotune_set_current_weight(m);

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
        if (!autotune_is_running()) {
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
        autotune_lock();
        s_status.current_weight = weight;
        s_status.current_elapsed_s = cur_elapsed;
        autotune_unlock();

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
            autotune_set_current_weight(*final_w);
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
            // Fixed min_speed trickle: at minimum speed the flow rate is low enough
            // that in-flight powder at stop is within the stop threshold.
            speed = min_speed;
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
            autotune_set_current_weight(*final_w);
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
    if (s_request.coarse_target_weight >=
        (s_request.fine_target_weight - AUTOTUNE_STAGE_TARGET_MIN_GAP_GN)) {
        ESP_LOGE(TAG, "Invalid targets for coarse+fine: coarse=%.3f fine=%.3f (need coarse < fine by >= %.3f)",
                 s_request.coarse_target_weight, s_request.fine_target_weight,
                 AUTOTUNE_STAGE_TARGET_MIN_GAP_GN);
        return false;
    }

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
    autotune_lock();
    s_status.state = AUTOTUNE_STATE_ERROR;
    strncpy(s_status.message, msg, sizeof(s_status.message) - 1);
    s_status.message[sizeof(s_status.message) - 1] = '\0';
    autotune_unlock();
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

// ---------------------------------------------------------------------------
// Coordinate Descent optimizer for PD gains
//
// Tests Kp and Kd independently in log-space. Each 4-run cycle:
//   KP_POS: test kp*exp(+δ), kd fixed
//   KP_NEG: test kp*exp(-δ), kd fixed  → pick better, update best_kp, adapt δ_kp
//   KD_POS: test best_kp, kd*exp(+δ)
//   KD_NEG: test best_kp, kd*exp(-δ)  → pick better, update best_kd, adapt δ_kd
//
// Improvement is unambiguously attributed to the correct parameter.
// CONFIRM phase (existing, unchanged) provides noise filtering (Option B).
// ---------------------------------------------------------------------------
typedef enum {
    CD_STEP_BASELINE = -1,  // first run at profile defaults
    CD_STEP_KP_POS   =  0,
    CD_STEP_KP_NEG   =  1,
    CD_STEP_KD_POS   =  2,
    CD_STEP_KD_NEG   =  3,
} cd_step_t;

typedef struct {
    float best_kp, best_kd;
    float best_abs_werr, best_abs_terr, best_overshoot;
    float delta_kp, delta_kd;       // step size in log-space
    // result of the "+" half-step, compared with "-" after both run
    float pos_abs_werr, pos_abs_terr, pos_overshoot;
    float pos_kp, pos_kd;
    int kp_fail_streak;
    int kd_fail_streak;
    cd_step_t step;
} cd_state_t;

static cd_state_t s_cd;

static void trials_reset(void)
{
    autotune_lock();
    s_trial_count = 0;
    autotune_unlock();
}

static void trials_add(uint8_t stage, float kp, float kd, float werr, float terr,
                        float overshoot, float settled_w, float elapsed)
{
    autotune_lock();
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
    autotune_unlock();
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

static void cd_init(float kp, float kd)
{
    s_cd.best_kp        = fmaxf(kp, 1e-6f);
    s_cd.best_kd        = fmaxf(kd, 1e-6f);
    s_cd.best_abs_werr  = 1e9f;
    s_cd.best_abs_terr  = 1e9f;
    s_cd.best_overshoot = 0.0f;
    s_cd.delta_kp       = 0.26f;   // ~30% multiplicative step (log(1.3) ≈ 0.26)
    s_cd.delta_kd       = 0.26f;
    s_cd.kp_fail_streak = 0;
    s_cd.kd_fail_streak = 0;
    s_cd.step           = CD_STEP_BASELINE;
}

// Produce the (kp, kd) to test in the next run.
// For KP_POS and KD_POS: also saves pos_kp/pos_kd so cd_update can compare.
static void cd_generate_candidate(float *kp, float *kd,
                                   float kp_min, float kp_max,
                                   float kd_min, float kd_max)
{
    switch (s_cd.step) {
        case CD_STEP_BASELINE:
            // First run uses profile defaults set by caller; just return current best.
            *kp = s_cd.best_kp;
            *kd = s_cd.best_kd;
            break;
        case CD_STEP_KP_POS:
            *kp = clampf(s_cd.best_kp * expf(+s_cd.delta_kp), kp_min, kp_max);
            *kd = s_cd.best_kd;
            s_cd.pos_kp = *kp;
            s_cd.pos_kd = *kd;
            break;
        case CD_STEP_KP_NEG:
            *kp = clampf(s_cd.best_kp * expf(-s_cd.delta_kp), kp_min, kp_max);
            *kd = s_cd.best_kd;
            break;
        case CD_STEP_KD_POS:
            *kp = s_cd.best_kp;
            *kd = clampf(s_cd.best_kd * expf(+s_cd.delta_kd), kd_min, kd_max);
            s_cd.pos_kp = *kp;
            s_cd.pos_kd = *kd;
            break;
        case CD_STEP_KD_NEG:
            *kp = s_cd.best_kp;
            *kd = clampf(s_cd.best_kd * expf(-s_cd.delta_kd), kd_min, kd_max);
            break;
    }
}

// Record result for current CD step. Advances state machine.
// Returns true when global best (best_kp, best_kd) was updated.
// Only call during SEARCH phase.
static bool cd_update(float kp, float kd,
                       float abs_werr, float abs_terr, float overshoot)
{
    bool improved = false;

    switch (s_cd.step) {
        case CD_STEP_BASELINE:
            // Record profile-default run as starting point.
            s_cd.best_kp        = kp;
            s_cd.best_kd        = kd;
            s_cd.best_abs_werr  = abs_werr;
            s_cd.best_abs_terr  = abs_terr;
            s_cd.best_overshoot = overshoot;
            s_cd.step = CD_STEP_KP_POS;
            ESP_LOGI(TAG, "CD: baseline kp=%.5f kd=%.5f werr=%.4f", kp, kd, abs_werr);
            break;

        case CD_STEP_KP_POS:
            // Save result; comparison happens after KP_NEG.
            s_cd.pos_abs_werr  = abs_werr;
            s_cd.pos_abs_terr  = abs_terr;
            s_cd.pos_overshoot = overshoot;
            s_cd.step = CD_STEP_KP_NEG;
            break;

        case CD_STEP_KP_NEG: {
            // Pick the better of (+delta, -delta) for Kp.
            bool pos_wins = es_is_better(s_cd.pos_abs_werr, s_cd.pos_abs_terr, s_cd.pos_overshoot,
                                          abs_werr, abs_terr, overshoot);
            float cand_werr, cand_terr, cand_os, cand_kp, cand_kd;
            if (pos_wins) {
                cand_werr = s_cd.pos_abs_werr; cand_terr = s_cd.pos_abs_terr;
                cand_os   = s_cd.pos_overshoot;
                cand_kp   = s_cd.pos_kp;       cand_kd   = s_cd.pos_kd;
            } else {
                cand_werr = abs_werr; cand_terr = abs_terr;
                cand_os   = overshoot;
                cand_kp   = kp;       cand_kd   = kd;
            }
            if (es_is_better(cand_werr, cand_terr, cand_os,
                              s_cd.best_abs_werr, s_cd.best_abs_terr, s_cd.best_overshoot)) {
                s_cd.best_kp        = cand_kp;
                s_cd.best_kd        = cand_kd;
                s_cd.best_abs_werr  = cand_werr;
                s_cd.best_abs_terr  = cand_terr;
                s_cd.best_overshoot = cand_os;
                s_cd.delta_kp       = fminf(s_cd.delta_kp * 1.25f, 0.60f);
                s_cd.kp_fail_streak = 0;
                improved = true;
                ESP_LOGI(TAG, "CD: Kp improved → best_kp=%.5f kd=%.5f werr=%.4f δ_kp=%.3f",
                         s_cd.best_kp, s_cd.best_kd, s_cd.best_abs_werr, s_cd.delta_kp);
            } else {
                s_cd.delta_kp = fmaxf(s_cd.delta_kp * 0.75f, 0.05f);
                s_cd.kp_fail_streak++;
                ESP_LOGI(TAG, "CD: Kp no improvement δ_kp=%.3f streak=%d",
                         s_cd.delta_kp, s_cd.kp_fail_streak);
            }
            s_cd.step = CD_STEP_KD_POS;
            break;
        }

        case CD_STEP_KD_POS:
            s_cd.pos_abs_werr  = abs_werr;
            s_cd.pos_abs_terr  = abs_terr;
            s_cd.pos_overshoot = overshoot;
            s_cd.step = CD_STEP_KD_NEG;
            break;

        case CD_STEP_KD_NEG: {
            // Pick the better of (+delta, -delta) for Kd.
            bool pos_wins = es_is_better(s_cd.pos_abs_werr, s_cd.pos_abs_terr, s_cd.pos_overshoot,
                                          abs_werr, abs_terr, overshoot);
            float cand_werr, cand_terr, cand_os, cand_kp, cand_kd;
            if (pos_wins) {
                cand_werr = s_cd.pos_abs_werr; cand_terr = s_cd.pos_abs_terr;
                cand_os   = s_cd.pos_overshoot;
                cand_kp   = s_cd.pos_kp;       cand_kd   = s_cd.pos_kd;
            } else {
                cand_werr = abs_werr; cand_terr = abs_terr;
                cand_os   = overshoot;
                cand_kp   = kp;       cand_kd   = kd;
            }
            if (es_is_better(cand_werr, cand_terr, cand_os,
                              s_cd.best_abs_werr, s_cd.best_abs_terr, s_cd.best_overshoot)) {
                s_cd.best_kp        = cand_kp;
                s_cd.best_kd        = cand_kd;
                s_cd.best_abs_werr  = cand_werr;
                s_cd.best_abs_terr  = cand_terr;
                s_cd.best_overshoot = cand_os;
                s_cd.delta_kd       = fminf(s_cd.delta_kd * 1.25f, 0.60f);
                s_cd.kd_fail_streak = 0;
                improved = true;
                ESP_LOGI(TAG, "CD: Kd improved → kp=%.5f best_kd=%.5f werr=%.4f δ_kd=%.3f",
                         s_cd.best_kp, s_cd.best_kd, s_cd.best_abs_werr, s_cd.delta_kd);
            } else {
                s_cd.delta_kd = fmaxf(s_cd.delta_kd * 0.75f, 0.05f);
                s_cd.kd_fail_streak++;
                ESP_LOGI(TAG, "CD: Kd no improvement δ_kd=%.3f streak=%d",
                         s_cd.delta_kd, s_cd.kd_fail_streak);
            }
            s_cd.step = CD_STEP_KP_POS;  // 4-step cycle complete, restart from Kp
            break;
        }
    }

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

    cd_init(kp_base, kd_base);

    // First run uses base parameters (the profile defaults)
    float kp = kp_base;
    float kd = kd_base;
    float stable_kp = kp_base;
    float stable_kd = kd_base;
    float stable_time_s = 1e9f;
    float stable_abs_werr = 1e9f;
    float confirmed_kp = kp_base;
    float confirmed_kd = kd_base;
    float confirmed_time_s = 1e9f;
    float confirmed_abs_werr = 1e9f;
    bool confirmed_ready = false;
    float fallback_kp = kp_base;
    float fallback_kd = kd_base;
    float fallback_time_s = 1e9f;
    float fallback_abs_werr = 1e9f;
    int stable_confirmations = 0;
    int speed_probe_idx = 0;
    bool reconfirming_speed_candidate = false;

    typedef enum {
        COARSE_PHASE_SEARCH = 0,
        COARSE_PHASE_CONFIRM = 1,
        COARSE_PHASE_SPEED_PROBE = 2,
    } coarse_phase_t;
    coarse_phase_t phase = COARSE_PHASE_SEARCH;

    for (int run = 1; run <= s_request.max_runs_per_stage; run++) {
        if (autotune_take_finish_now_request()) {
            ESP_LOGI(TAG, "Finish-now requested during COARSE: proceeding to FINE");
            autotune_lock();
            strncpy(s_status.message, "Finish-now: moving to fine stage",
                    sizeof(s_status.message) - 1);
            s_status.message[sizeof(s_status.message) - 1] = '\0';
            autotune_unlock();
            tolerance_reached = true;
            break;
        }

        if (run == 1) {
            autotune_lock();
            strncpy(s_status.message, "Waiting for stable reading...", sizeof(s_status.message) - 1);
            s_status.message[sizeof(s_status.message) - 1] = '\0';
            autotune_unlock();
            if (!wait_for_stable_zero()) {
                return false;
            }
        } else {
            if (!wait_cup_removal_return_cycle()) {
                return false;
            }
            if (autotune_take_finish_now_request()) {
                ESP_LOGI(TAG, "Finish-now requested in COARSE: using current coarse result and moving to FINE");
                autotune_lock();
                s_status.stage = AUTOTUNE_STAGE_FINE;
                s_status.substatus = AUTOTUNE_SUB_REMOVE_CUP;
                strncpy(s_status.message, "Finish-now: moving to fine stage",
                        sizeof(s_status.message) - 1);
                s_status.message[sizeof(s_status.message) - 1] = '\0';
                autotune_unlock();
                tolerance_reached = true;
                break;
            }
        }

        autotune_lock();
        s_status.stage = AUTOTUNE_STAGE_COARSE;
        s_status.stage_run = run;
        s_status.stage_max_runs = s_request.max_runs_per_stage;
        s_status.active_kp = kp;
        s_status.active_kd = kd;
        s_status.substatus = AUTOTUNE_SUB_DISPENSING;
        snprintf(s_status.message, sizeof(s_status.message), "COARSE run %d/%d", run, s_request.max_runs_per_stage);
        autotune_unlock();

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

        autotune_lock();
        s_status.last_weight = final_w;
        s_status.last_elapsed_s = elapsed_s;
        autotune_unlock();

        float werr = final_w - s_request.coarse_target_weight;
        float abs_werr = fabsf(werr);
        // Coarse time: one-sided vs total budget (being fast is always ok; slow is excess)
        float es_terr = fmaxf(0.0f, elapsed_s - s_request.total_target_time_s);

        // Record trial (time_error = coarse elapsed relative to total target, informational)
        trials_add(1, kp, kd, werr, elapsed_s - s_request.total_target_time_s, overshoot, final_w, elapsed_s);

        // Update CD and track best (only during SEARCH; CONFIRM/SPEED_PROBE manage
        // their own best tracking via stable_kp/confirmed_kp logic below).
        if (phase == COARSE_PHASE_SEARCH) {
            bool improved = cd_update(kp, kd, abs_werr, es_terr, overshoot);
            if (improved || run == 1) {
                *best_kp = s_cd.best_kp;
                *best_kd = s_cd.best_kd;
                *best_abs_werr = s_cd.best_abs_werr;
                *best_abs_terr = elapsed_s;
                autotune_lock();
                s_status.coarse_best_kp = s_cd.best_kp;
                s_status.coarse_best_kd = s_cd.best_kd;
                s_status.coarse_best_weight_error = s_cd.best_abs_werr;
                s_status.coarse_best_time_error = elapsed_s;
                autotune_unlock();
            }
        }

        autotune_lock();
        s_status.runs_done++;
        s_status.progress_pct = ((float)s_status.runs_done / (float)s_status.runs_total) * 100.0f;
        autotune_unlock();

        ESP_LOGI(TAG, "COARSE run %d/%d: kp=%.5f kd=%.5f w=%.4f t=%.2f werr=%.4f os=%.4f",
                 run, s_request.max_runs_per_stage, kp, kd, final_w, elapsed_s, abs_werr, overshoot);

        if (autotune_take_finish_now_request()) {
            ESP_LOGI(TAG, "Finish-now requested in COARSE: using current coarse result and moving to FINE");
            autotune_lock();
            s_status.stage = AUTOTUNE_STAGE_FINE;
            s_status.substatus = AUTOTUNE_SUB_REMOVE_CUP;
            strncpy(s_status.message, "Finish-now: moving to fine stage",
                    sizeof(s_status.message) - 1);
            s_status.message[sizeof(s_status.message) - 1] = '\0';
            autotune_unlock();
            tolerance_reached = true;
            break;
        }

        // Coarse acceptance: require both settled weight accuracy and limited overshoot.
        float positive_overshoot = fmaxf(0.0f, overshoot);
        float run_quality = get_stage_quality(AUTOTUNE_STAGE_COARSE);
        float quality_floor = fmaxf(AUTOTUNE_MIN_ACCEPT_QUALITY, telemetry_recent_quality_mean(AUTOTUNE_STAGE_COARSE, 8) * 0.70f);
        bool accepted = (abs_werr <= s_request.coarse_weight_tolerance) &&
                        (positive_overshoot <= s_request.coarse_weight_tolerance) &&
                        (run_quality >= quality_floor);
        telemetry_add(AUTOTUNE_STAGE_COARSE, (uint8_t)phase, kp, kd, final_w, elapsed_s,
                      overshoot, abs_werr, run_quality, accepted);

        if (phase == COARSE_PHASE_SEARCH) {
            if (accepted) {
                stable_kp = kp;
                stable_kd = kd;
                stable_time_s = elapsed_s;
                stable_abs_werr = abs_werr;
                stable_confirmations = 1;
                speed_probe_idx = 0;
                phase = COARSE_PHASE_CONFIRM;
                *best_kp = stable_kp;
                *best_kd = stable_kd;
                *best_abs_werr = stable_abs_werr;
                *best_abs_terr = stable_time_s;
                ESP_LOGI(TAG, "COARSE candidate accepted, confirmation 1/%d", AUTOTUNE_COARSE_STABLE_CONFIRM_RUNS);
                kp = stable_kp;
                kd = stable_kd;
                continue;
            }
            // Keep searching with coordinate descent
            cd_generate_candidate(&kp, &kd, kp_min, kp_max, kd_min, kd_max);
            continue;
        }

        if (phase == COARSE_PHASE_CONFIRM) {
            if (accepted) {
                stable_confirmations++;
                if (elapsed_s < stable_time_s) {
                    stable_time_s = elapsed_s;
                }
                if (abs_werr < stable_abs_werr) {
                    stable_abs_werr = abs_werr;
                }

                *best_kp = stable_kp;
                *best_kd = stable_kd;
                *best_abs_werr = stable_abs_werr;
                *best_abs_terr = stable_time_s;

                ESP_LOGI(TAG, "COARSE confirmation %d/%d OK",
                         stable_confirmations, AUTOTUNE_COARSE_STABLE_CONFIRM_RUNS);

                if (stable_confirmations >= AUTOTUNE_COARSE_STABLE_CONFIRM_RUNS) {
                    confirmed_kp = stable_kp;
                    confirmed_kd = stable_kd;
                    confirmed_time_s = stable_time_s;
                    confirmed_abs_werr = stable_abs_werr;
                    confirmed_ready = true;

                    phase = COARSE_PHASE_SPEED_PROBE;
                    speed_probe_idx = 0;
                    reconfirming_speed_candidate = false;
                    fallback_kp = stable_kp;
                    fallback_kd = stable_kd;
                    fallback_time_s = stable_time_s;
                    fallback_abs_werr = stable_abs_werr;
                    float q = telemetry_recent_quality_mean(AUTOTUNE_STAGE_COARSE, 8);
                    float adaptive_step = AUTOTUNE_SPEED_PROBE_GAIN_STEP * (0.70f + 0.60f * clampf(q, 0.0f, 1.0f));
                    float speed_factor = 1.0f + adaptive_step * (float)(speed_probe_idx + 1);
                    kp = clampf(stable_kp * speed_factor, kp_min, kp_max);
                    kd = clampf(stable_kd * speed_factor, kd_min, kd_max);
                    ESP_LOGI(TAG, "COARSE stable confirmed, starting speed probes");
                } else {
                    kp = stable_kp;
                    kd = stable_kd;
                }
            } else {
                if (reconfirming_speed_candidate) {
                    // Step back to last stable setup instead of restarting full search.
                    stable_kp = fallback_kp;
                    stable_kd = fallback_kd;
                    stable_time_s = fallback_time_s;
                    stable_abs_werr = fallback_abs_werr;
                    if (confirmed_ready) {
                        *best_kp = confirmed_kp;
                        *best_kd = confirmed_kd;
                        *best_abs_werr = confirmed_abs_werr;
                        *best_abs_terr = confirmed_time_s;
                    } else {
                        *best_kp = stable_kp;
                        *best_kd = stable_kd;
                        *best_abs_werr = stable_abs_werr;
                        *best_abs_terr = stable_time_s;
                    }
                    tolerance_reached = true;
                    ESP_LOGI(TAG, "COARSE speed candidate not confirmed, stepping back one level");
                    break;
                }
                ESP_LOGI(TAG, "COARSE confirmation failed, back to search");
                phase = COARSE_PHASE_SEARCH;
                stable_confirmations = 0;
                cd_generate_candidate(&kp, &kd, kp_min, kp_max, kd_min, kd_max);
            }
            continue;
        }

        // COARSE_PHASE_SPEED_PROBE
        bool faster_and_precise = accepted && (elapsed_s + AUTOTUNE_MIN_SPEED_GAIN_S < stable_time_s);
        if (faster_and_precise) {
            fallback_kp = stable_kp;
            fallback_kd = stable_kd;
            fallback_time_s = stable_time_s;
            fallback_abs_werr = stable_abs_werr;
            stable_kp = kp;
            stable_kd = kd;
            stable_time_s = elapsed_s;
            stable_abs_werr = abs_werr;
            stable_confirmations = 1;
            reconfirming_speed_candidate = true;
            phase = COARSE_PHASE_CONFIRM;
            *best_kp = stable_kp;
            *best_kd = stable_kd;
            *best_abs_werr = stable_abs_werr;
            *best_abs_terr = stable_time_s;
            ESP_LOGI(TAG, "COARSE faster precise setup found, re-confirming");
            kp = stable_kp;
            kd = stable_kd;
            continue;
        }

        speed_probe_idx++;
        if (speed_probe_idx >= AUTOTUNE_SPEED_PROBE_ATTEMPTS) {
            // Could not find a faster setup with same precision. Keep stable setup.
            tolerance_reached = true;
            if (confirmed_ready) {
                *best_kp = confirmed_kp;
                *best_kd = confirmed_kd;
                *best_abs_werr = confirmed_abs_werr;
                *best_abs_terr = confirmed_time_s;
            } else {
                *best_kp = stable_kp;
                *best_kd = stable_kd;
                *best_abs_werr = stable_abs_werr;
                *best_abs_terr = stable_time_s;
            }
            ESP_LOGI(TAG, "COARSE final stable setup kept (no faster precise variant)");
            break;
        }

        float q = telemetry_recent_quality_mean(AUTOTUNE_STAGE_COARSE, 8);
        float adaptive_step = AUTOTUNE_SPEED_PROBE_GAIN_STEP * (0.70f + 0.60f * clampf(q, 0.0f, 1.0f));
        float speed_factor = 1.0f + adaptive_step * (float)(speed_probe_idx + 1);
        kp = clampf(stable_kp * speed_factor, kp_min, kp_max);
        kd = clampf(stable_kd * speed_factor, kd_min, kd_max);
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

    cd_init(kp_base, kd_base);

    float kp = kp_base;
    float kd = kd_base;
    float stable_kp = kp_base;
    float stable_kd = kd_base;
    float stable_total_time_s = 1e9f;
    float stable_abs_werr = 1e9f;
    float confirmed_kp = kp_base;
    float confirmed_kd = kd_base;
    float confirmed_total_time_s = 1e9f;
    float confirmed_abs_werr = 1e9f;
    bool confirmed_ready = false;
    float fallback_kp = kp_base;
    float fallback_kd = kd_base;
    float fallback_total_time_s = 1e9f;
    float fallback_abs_werr = 1e9f;
    int stable_confirmations = 0;
    int speed_probe_idx = 0;
    bool reconfirming_speed_candidate = false;
    float over_guard_kp_ceiling = kp_max;
    float over_guard_kd_floor = kd_min;

    typedef enum {
        FINE_PHASE_SEARCH = 0,
        FINE_PHASE_CONFIRM = 1,
        FINE_PHASE_SPEED_PROBE = 2,
    } fine_phase_t;
    fine_phase_t phase = FINE_PHASE_SEARCH;

    for (int run = 1; run <= s_request.max_runs_per_stage; run++) {
        if (autotune_take_finish_now_request()) {
            ESP_LOGI(TAG, "Finish-now requested during FINE: ending autotune");
            autotune_lock();
            strncpy(s_status.message, "Finish-now: finalizing",
                    sizeof(s_status.message) - 1);
            s_status.message[sizeof(s_status.message) - 1] = '\0';
            autotune_unlock();
            tolerance_reached = true;
            break;
        }

        if (!wait_cup_removal_return_cycle()) {
            return false;
        }
        if (autotune_take_finish_now_request()) {
            ESP_LOGI(TAG, "Finish-now requested in FINE: keeping current result and ending autotune");
            autotune_lock();
            s_status.stage = AUTOTUNE_STAGE_FINE;
            s_status.substatus = AUTOTUNE_SUB_REMOVE_CUP;
            strncpy(s_status.message, "Finish-now: finalizing",
                    sizeof(s_status.message) - 1);
            s_status.message[sizeof(s_status.message) - 1] = '\0';
            autotune_unlock();
            tolerance_reached = true;
            break;
        }

        autotune_lock();
        s_status.stage = AUTOTUNE_STAGE_FINE;
        s_status.stage_run = run;
        s_status.stage_max_runs = s_request.max_runs_per_stage;
        s_status.active_kp = kp;
        s_status.active_kd = kd;
        s_status.substatus = AUTOTUNE_SUB_DISPENSING;
        snprintf(s_status.message, sizeof(s_status.message), "FINE run %d/%d", run, s_request.max_runs_per_stage);
        autotune_unlock();

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
        autotune_lock();
        s_status.last_weight = final_w;
        s_status.last_elapsed_s = total_elapsed;
        autotune_unlock();

        float werr = final_w - s_request.fine_target_weight;
        float abs_werr = fabsf(werr);
        // Total cycle time vs target: negative = under (always ok), positive = over budget
        float terr = total_elapsed - s_request.total_target_time_s;
        // One-sided with tolerance zone: excess within time_tolerance_s is free (no ES penalty).
        float es_terr = fmaxf(0.0f, terr - s_request.time_tolerance_s);

        trials_add(2, kp, kd, werr, terr, overshoot, final_w, total_elapsed);

        if (phase == FINE_PHASE_SEARCH) {
            bool improved = cd_update(kp, kd, abs_werr, es_terr, overshoot);
            if (improved || run == 1) {
                *best_kp = s_cd.best_kp;
                *best_kd = s_cd.best_kd;
                *best_abs_werr = s_cd.best_abs_werr;
                *best_abs_terr = fabsf(terr);
                autotune_lock();
                s_status.fine_best_kp = s_cd.best_kp;
                s_status.fine_best_kd = s_cd.best_kd;
                s_status.fine_best_weight_error = s_cd.best_abs_werr;
                s_status.fine_best_time_error = terr;
                autotune_unlock();
            }
        }

        autotune_lock();
        s_status.runs_done++;
        s_status.progress_pct = ((float)s_status.runs_done / (float)s_status.runs_total) * 100.0f;
        autotune_unlock();

        ESP_LOGI(TAG, "FINE run %d/%d: kp=%.5f kd=%.5f w=%.4f t_fine=%.2f t_total=%.2f werr=%.4f terr=%.3f os=%.4f",
                 run, s_request.max_runs_per_stage, kp, kd, final_w, fine_elapsed, total_elapsed, abs_werr, terr, overshoot);

        if (autotune_take_finish_now_request()) {
            ESP_LOGI(TAG, "Finish-now requested in FINE: keeping current result and ending autotune");
            autotune_lock();
            strncpy(s_status.message, "Finish-now: finalizing",
                    sizeof(s_status.message) - 1);
            s_status.message[sizeof(s_status.message) - 1] = '\0';
            autotune_unlock();
            tolerance_reached = true;
            break;
        }

        float positive_overshoot = fmaxf(0.0f, overshoot);
        float run_quality = get_stage_quality(AUTOTUNE_STAGE_FINE);
        float quality_floor = fmaxf(AUTOTUNE_MIN_ACCEPT_QUALITY, telemetry_recent_quality_mean(AUTOTUNE_STAGE_FINE, 8) * 0.70f);
        bool accepted = (abs_werr <= s_request.fine_weight_tolerance) &&
                        (positive_overshoot <= s_request.fine_weight_tolerance) &&
                        (run_quality >= quality_floor);
        telemetry_add(AUTOTUNE_STAGE_FINE, (uint8_t)phase, kp, kd, final_w, total_elapsed,
                      overshoot, abs_werr, run_quality, accepted);

        if (phase == FINE_PHASE_SEARCH) {
            if (accepted) {
                stable_kp = kp;
                stable_kd = kd;
                stable_total_time_s = total_elapsed;
                stable_abs_werr = abs_werr;
                stable_confirmations = 1;
                speed_probe_idx = 0;
                phase = FINE_PHASE_CONFIRM;
                *best_kp = stable_kp;
                *best_kd = stable_kd;
                *best_abs_werr = stable_abs_werr;
                *best_abs_terr = fabsf(terr);
                ESP_LOGI(TAG, "FINE candidate accepted, confirmation 1/%d", AUTOTUNE_FINE_STABLE_CONFIRM_RUNS);
                kp = stable_kp;
                kd = stable_kd;
                continue;
            }
            cd_generate_candidate(&kp, &kd, kp_min, kp_max, kd_min, kd_max);
            continue;
        }

        if (phase == FINE_PHASE_CONFIRM) {
            if (accepted) {
                stable_confirmations++;
                if (total_elapsed < stable_total_time_s) {
                    stable_total_time_s = total_elapsed;
                }
                if (abs_werr < stable_abs_werr) {
                    stable_abs_werr = abs_werr;
                }

                *best_kp = stable_kp;
                *best_kd = stable_kd;
                *best_abs_werr = stable_abs_werr;
                *best_abs_terr = fabsf(stable_total_time_s - s_request.total_target_time_s);

                ESP_LOGI(TAG, "FINE confirmation %d/%d OK",
                         stable_confirmations, AUTOTUNE_FINE_STABLE_CONFIRM_RUNS);

                if (stable_confirmations >= AUTOTUNE_FINE_STABLE_CONFIRM_RUNS) {
                    confirmed_kp = stable_kp;
                    confirmed_kd = stable_kd;
                    confirmed_total_time_s = stable_total_time_s;
                    confirmed_abs_werr = stable_abs_werr;
                    confirmed_ready = true;

                    phase = FINE_PHASE_SPEED_PROBE;
                    speed_probe_idx = 0;
                    reconfirming_speed_candidate = false;
                    fallback_kp = stable_kp;
                    fallback_kd = stable_kd;
                    fallback_total_time_s = stable_total_time_s;
                    fallback_abs_werr = stable_abs_werr;
                    float q = telemetry_recent_quality_mean(AUTOTUNE_STAGE_FINE, 8);
                    float adaptive_step = AUTOTUNE_SPEED_PROBE_GAIN_STEP * (0.70f + 0.60f * clampf(q, 0.0f, 1.0f));
                    float speed_factor = 1.0f + adaptive_step * (float)(speed_probe_idx + 1);
                    kp = clampf(stable_kp * speed_factor, kp_min, kp_max);
                    kd = stable_kd;
                    apply_fine_over_guard(&kp, &kd, kp_min, over_guard_kp_ceiling, over_guard_kd_floor, kd_max);
                    ESP_LOGI(TAG, "FINE stable confirmed, starting speed probes");
                } else {
                    kp = stable_kp;
                    kd = stable_kd;
                }
            } else {
                if (reconfirming_speed_candidate) {
                    // Step back one level to previously confirmed setup.
                    stable_kp = fallback_kp;
                    stable_kd = fallback_kd;
                    stable_total_time_s = fallback_total_time_s;
                    stable_abs_werr = fallback_abs_werr;
                    if (confirmed_ready) {
                        *best_kp = confirmed_kp;
                        *best_kd = confirmed_kd;
                        *best_abs_werr = confirmed_abs_werr;
                        *best_abs_terr = fabsf(confirmed_total_time_s - s_request.total_target_time_s);
                    } else {
                        *best_kp = stable_kp;
                        *best_kd = stable_kd;
                        *best_abs_werr = stable_abs_werr;
                        *best_abs_terr = fabsf(stable_total_time_s - s_request.total_target_time_s);
                    }
                    tolerance_reached = true;
                    ESP_LOGI(TAG, "FINE speed candidate not confirmed, stepping back one level");
                    break;
                }
                ESP_LOGI(TAG, "FINE confirmation failed, back to search");
                phase = FINE_PHASE_SEARCH;
                stable_confirmations = 0;
                cd_generate_candidate(&kp, &kd, kp_min, kp_max, kd_min, kd_max);
            }
            continue;
        }

        // FINE_PHASE_SPEED_PROBE
        bool over_limit = (abs_werr > 0.01f) || (positive_overshoot > 0.01f);
        if (over_limit) {
            over_guard_kp_ceiling = fminf(over_guard_kp_ceiling, fmaxf(kp_min, kp * 0.85f));
            float kd_guard_cap = fminf(kd_max, kd_base * 2.5f);
            over_guard_kd_floor = fmaxf(over_guard_kd_floor, fminf(kd_guard_cap, kd * 1.02f));
            apply_fine_over_guard(&kp, &kd, kp_min, over_guard_kp_ceiling, over_guard_kd_floor, kd_max);
            ESP_LOGI(TAG, "FINE over-guard active: kp<=%.5f kd>=%.5f",
                     over_guard_kp_ceiling, over_guard_kd_floor);
        }

        bool faster_and_precise = accepted && (total_elapsed + AUTOTUNE_MIN_SPEED_GAIN_S < stable_total_time_s);
        if (faster_and_precise) {
            fallback_kp = stable_kp;
            fallback_kd = stable_kd;
            fallback_total_time_s = stable_total_time_s;
            fallback_abs_werr = stable_abs_werr;
            stable_kp = kp;
            stable_kd = kd;
            stable_total_time_s = total_elapsed;
            stable_abs_werr = abs_werr;
            stable_confirmations = 1;
            reconfirming_speed_candidate = true;
            phase = FINE_PHASE_CONFIRM;
            *best_kp = stable_kp;
            *best_kd = stable_kd;
            *best_abs_werr = stable_abs_werr;
            *best_abs_terr = fabsf(total_elapsed - s_request.total_target_time_s);
            ESP_LOGI(TAG, "FINE faster precise setup found, re-confirming");
            kp = stable_kp;
            kd = stable_kd;
            continue;
        }

        speed_probe_idx++;
        if (speed_probe_idx >= AUTOTUNE_SPEED_PROBE_ATTEMPTS) {
            tolerance_reached = true;
            if (confirmed_ready) {
                *best_kp = confirmed_kp;
                *best_kd = confirmed_kd;
                *best_abs_werr = confirmed_abs_werr;
                *best_abs_terr = fabsf(confirmed_total_time_s - s_request.total_target_time_s);
            } else {
                *best_kp = stable_kp;
                *best_kd = stable_kd;
                *best_abs_werr = stable_abs_werr;
                *best_abs_terr = fabsf(stable_total_time_s - s_request.total_target_time_s);
            }
            ESP_LOGI(TAG, "FINE final stable setup kept (no faster precise variant)");
            break;
        }

        float q = telemetry_recent_quality_mean(AUTOTUNE_STAGE_FINE, 8);
        float adaptive_step = AUTOTUNE_SPEED_PROBE_GAIN_STEP * (0.70f + 0.60f * clampf(q, 0.0f, 1.0f));
        float speed_factor = 1.0f + adaptive_step * (float)(speed_probe_idx + 1);
        kp = clampf(stable_kp * speed_factor, kp_min, kp_max);
        kd = stable_kd;
        apply_fine_over_guard(&kp, &kd, kp_min, over_guard_kp_ceiling, over_guard_kd_floor, kd_max);
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
    telemetry_reset();

    autotune_lock();
    s_status.runs_total = s_request.max_runs_per_stage * 2;
    s_status.runs_done = 0;
    autotune_unlock();

    float best_coarse_kp = profile->coarse_kp;
    float best_coarse_kd = profile->coarse_kd;
    float best_coarse_werr = 999.0f;
    float best_coarse_terr = 999.0f;

    bool coarse_ok = tune_coarse_stage(profile,
                                       &best_coarse_kp,
                                       &best_coarse_kd,
                                       &best_coarse_werr,
                                       &best_coarse_terr);
    if (!coarse_ok) {
        bool coarse_fallback_ready = isfinite(best_coarse_werr) && (best_coarse_werr < 999.0f);
        if (coarse_fallback_ready) {
            ESP_LOGW(TAG, "COARSE run limit reached; using last stable coarse setup");
            coarse_ok = true;
        }
    }
    if (!coarse_ok) {
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

    autotune_lock();
    s_status.stage = AUTOTUNE_STAGE_NONE;
    s_status.substatus = AUTOTUNE_SUB_IDLE;
    s_status.state = AUTOTUNE_STATE_DONE;
    s_status.progress_pct = 100.0f;
    strncpy(s_status.message, "Autotune coarse+fine completed", sizeof(s_status.message) - 1);
    s_status.message[sizeof(s_status.message) - 1] = '\0';
    autotune_unlock();

    stop_all();
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t autotune_init(void)
{
    if (!s_autotune_mutex) {
        s_autotune_mutex = xSemaphoreCreateMutex();
        if (!s_autotune_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }

    memset(&s_request, 0, sizeof(s_request));
    memset(&s_status, 0, sizeof(s_status));

    autotune_lock();
    s_status.state = AUTOTUNE_STATE_IDLE;
    s_status.stage = AUTOTUNE_STAGE_NONE;
    s_status.substatus = AUTOTUNE_SUB_IDLE;
    s_status.progress_pct = 0.0f;
    strncpy(s_status.message, "Idle", sizeof(s_status.message) - 1);
    s_status.message[sizeof(s_status.message) - 1] = '\0';
    autotune_unlock();

    return ESP_OK;
}

esp_err_t autotune_start(const autotune_request_t *request)
{
    if (!request) {
        return ESP_ERR_INVALID_ARG;
    }
    if (autotune_is_running()) {
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

    // Fine stage must always target a higher weight than coarse prefill.
    if (request->coarse_target_weight >=
        (request->fine_target_weight - AUTOTUNE_STAGE_TARGET_MIN_GAP_GN)) {
        return ESP_ERR_INVALID_ARG;
    }

    autotune_lock();
    s_request = *request;
    s_finish_now_requested = false;

    memset(&s_status, 0, sizeof(s_status));
    s_status.state = AUTOTUNE_STATE_RUNNING;
    s_status.stage = AUTOTUNE_STAGE_COARSE;
    s_status.stage_max_runs = s_request.max_runs_per_stage;
    strncpy(s_status.message, "Autotune coarse+fine in progress", sizeof(s_status.message) - 1);
    s_status.message[sizeof(s_status.message) - 1] = '\0';
    autotune_unlock();

    BaseType_t ret = xTaskCreate(autotune_task,
                                 "autotune",
                                 AUTOTUNE_STACK_SIZE,
                                 NULL,
                                 AUTOTUNE_TASK_PRIO,
                                 &s_task_handle);
    if (ret != pdPASS) {
        autotune_lock();
        s_status.state = AUTOTUNE_STATE_ERROR;
        strncpy(s_status.message, "Failed to start autotune task", sizeof(s_status.message) - 1);
        s_status.message[sizeof(s_status.message) - 1] = '\0';
        autotune_unlock();
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t autotune_get_status(autotune_status_t *status)
{
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }
    autotune_lock();
    *status = s_status;
    autotune_unlock();
    return ESP_OK;
}

esp_err_t autotune_cancel(void)
{
    if (!autotune_is_running()) {
        return ESP_ERR_INVALID_STATE;
    }

    autotune_lock();
    s_status.state = AUTOTUNE_STATE_IDLE;
    s_status.stage = AUTOTUNE_STAGE_NONE;
    s_status.substatus = AUTOTUNE_SUB_IDLE;
    s_status.progress_pct = 0.0f;
    strncpy(s_status.message, "Autotune cancelled", sizeof(s_status.message) - 1);
    s_status.message[sizeof(s_status.message) - 1] = '\0';
    autotune_unlock();

    stop_all();
    return ESP_OK;
}

esp_err_t autotune_finish_now(void)
{
    if (!autotune_is_running()) {
        return ESP_ERR_INVALID_STATE;
    }

    autotune_lock();
    s_finish_now_requested = true;
    if (s_status.stage == AUTOTUNE_STAGE_COARSE) {
        s_status.stage = AUTOTUNE_STAGE_FINE;
        s_status.substatus = AUTOTUNE_SUB_REMOVE_CUP;
        strncpy(s_status.message, "Finish-now requested: moving to fine stage",
                sizeof(s_status.message) - 1);
    } else if (s_status.stage == AUTOTUNE_STAGE_FINE) {
        s_status.substatus = AUTOTUNE_SUB_REMOVE_CUP;
        strncpy(s_status.message, "Finish-now requested: finalizing",
                sizeof(s_status.message) - 1);
    } else {
        strncpy(s_status.message, "Finish-now requested",
                sizeof(s_status.message) - 1);
    }
    s_status.message[sizeof(s_status.message) - 1] = '\0';
    autotune_unlock();
    return ESP_OK;
}

int autotune_get_trials(autotune_trial_result_t *out, int max_count)
{
    autotune_lock();
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
    autotune_unlock();
    return count;
}

int autotune_get_telemetry(autotune_telemetry_entry_t *out, int max_count)
{
    if (!out || max_count <= 0) {
        return 0;
    }

    autotune_lock();
    int count = (s_telemetry_count < max_count) ? s_telemetry_count : max_count;
    for (int i = 0; i < count; i++) {
        out[i] = s_telemetry[i];
    }
    autotune_unlock();
    return count;
}
