#ifndef AUTOTUNE_H_
#define AUTOTUNE_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUTOTUNE_STATE_IDLE = 0,
    AUTOTUNE_STATE_RUNNING = 1,
    AUTOTUNE_STATE_DONE = 2,
    AUTOTUNE_STATE_ERROR = 3,
} autotune_state_t;

typedef enum {
    AUTOTUNE_STAGE_NONE = 0,
    AUTOTUNE_STAGE_COARSE = 1,
    AUTOTUNE_STAGE_FINE = 2,
} autotune_stage_t;

typedef enum {
    AUTOTUNE_SUB_IDLE = 0,
    AUTOTUNE_SUB_DISPENSING = 1,
    AUTOTUNE_SUB_REMOVE_CUP = 2,
    AUTOTUNE_SUB_RETURN_CUP = 3,
    AUTOTUNE_SUB_STABILIZING = 4,
} autotune_substatus_t;

typedef struct {
    float target_weight;        // final target weight (gn); coarse stops at target_weight - coarse_stop_threshold
    float total_target_time_s;  // target for full coarse+fine cycle; being under is always ok

    int max_runs_per_stage;

    float coarse_weight_tolerance;  // grains
    float fine_weight_tolerance;    // grains
    float time_tolerance_s;         // allowed excess over total_target_time_s (seconds)

    float fine_stop_threshold;      // gn below target at which fine motor stops (default 0.02)
    float coarse_stop_threshold;    // gn below target at which coarse motor stops (default 0.03)

    bool auto_apply;
    bool save_to_nvs;
} autotune_request_t;

typedef struct {
    autotune_state_t state;
    autotune_stage_t stage;
    autotune_substatus_t substatus;

    float progress_pct;
    int runs_total;
    int runs_done;

    int stage_run;
    int stage_max_runs;

    float active_kp;
    float active_kd;

    // Live reading during dispensing
    float current_weight;
    float current_elapsed_s;

    // Settled result after motor stop (accounts for inertia)
    float last_weight;
    float last_elapsed_s;

    float coarse_best_kp;
    float coarse_best_kd;
    float coarse_best_weight_error;
    float coarse_best_time_error;

    float fine_best_kp;
    float fine_best_kd;
    float fine_best_weight_error;
    float fine_best_time_error;

    char message[96];
} autotune_status_t;

// Trial result for web UI display
typedef struct {
    uint8_t stage;        // 1 = coarse, 2 = fine
    float kp;
    float kd;
    float weight_error;   // signed: positive = overshoot
    float time_error;     // signed: positive = too slow
    float overshoot;      // max weight above target (gn)
    float settled_weight;
    float elapsed_s;
} autotune_trial_result_t;

// Telemetry entry for one autotune dispense run
typedef struct {
    uint32_t timestamp_ms;
    uint8_t stage;        // 1 = coarse, 2 = fine
    uint8_t phase;        // 0 = search, 1 = confirm, 2 = speed_probe
    float kp;
    float kd;
    float settled_weight;
    float elapsed_s;
    float overshoot;
    float abs_weight_error;
    float quality;        // flow-model quality score (0..1)
    bool accepted;        // passed stage acceptance gate
} autotune_telemetry_entry_t;

esp_err_t autotune_init(void);
esp_err_t autotune_start(const autotune_request_t *request);
esp_err_t autotune_get_status(autotune_status_t *status);
esp_err_t autotune_cancel(void);
esp_err_t autotune_finish_now(void);

// Get trial results for current stage. Returns number of trials copied.
int autotune_get_trials(autotune_trial_result_t *out, int max_count);

// Get telemetry entries for current autotune session. Returns number of copied entries.
int autotune_get_telemetry(autotune_telemetry_entry_t *out, int max_count);

#ifdef __cplusplus
}
#endif

#endif
