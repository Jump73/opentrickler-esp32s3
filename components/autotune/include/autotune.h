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
    // Stage 1 (coarse only)
    float coarse_target_weight;
    float coarse_target_time_s;

    // Stage 2 (coarse + fine, tune fine section)
    float fine_target_weight;
    float fine_target_time_s;

    // Iterative tuning limits
    int max_runs_per_stage;     // e.g. 15

    // Acceptance criteria
    float coarse_weight_tolerance;  // grains
    float fine_weight_tolerance;    // grains
    float time_tolerance_s;         // seconds

    // Optional persistence
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

esp_err_t autotune_init(void);
esp_err_t autotune_start(const autotune_request_t *request);
esp_err_t autotune_get_status(autotune_status_t *status);
esp_err_t autotune_cancel(void);

#ifdef __cplusplus
}
#endif

#endif
