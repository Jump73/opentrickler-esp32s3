#ifndef FLOW_MODEL_H_
#define FLOW_MODEL_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLOW_TABLE_POINTS   12
#define FLOW_RECORD_MAX     512

// Single recorded sample during a dispense
typedef struct {
    uint32_t timestamp_ms;  // ms since recording start
    float    speed_rps;     // motor speed that was SET
    float    weight_gn;     // scale reading
} flow_sample_t;

// Single point in the piecewise-linear flow table
typedef struct {
    float    speed_rps;
    float    flow_rate_gn_s;
    uint16_t sample_count;  // number of dispenses that contributed
} flow_point_t;

// Flow model for one motor
typedef struct {
    uint8_t      num_points;
    float        transport_delay_ms;  // time from motor start to first weight change
    float        inertia_factor_s;    // seconds of "in-flight" powder after motor stop
    float        last_quality;        // quality score of last dispense (0..1)
    flow_point_t points[FLOW_TABLE_POINTS];
} flow_model_single_t;

#define FLOW_MODEL_VERSION  2

// Per-profile flow model (both motors)
typedef struct {
    uint8_t             version;
    flow_model_single_t coarse;
    flow_model_single_t fine;
} flow_model_t;

// Lifecycle
esp_err_t flow_model_init(void);

// Recording (called from PID loops, inline — microseconds per call)
void flow_model_record_start(uint8_t motor);
void flow_model_record_sample(float speed_rps, float weight_gn);
void flow_model_record_stop(void);

// Analysis — call after dispense completes, merges into persistent model
esp_err_t flow_model_analyze_and_update(uint8_t profile_idx);

// Query (for feedforward, phase 2)
float flow_model_get_flow_rate(uint8_t profile_idx, uint8_t motor, float speed_rps);
float flow_model_get_speed_for_rate(uint8_t profile_idx, uint8_t motor, float desired_gn_s);
float flow_model_get_transport_delay(uint8_t profile_idx, uint8_t motor);
float flow_model_get_inertia(uint8_t profile_idx, uint8_t motor);

// Persistence
esp_err_t flow_model_save(uint8_t profile_idx);
esp_err_t flow_model_load(uint8_t profile_idx);

// Confidence query — true when enough bins have sufficient data
bool flow_model_is_trusted(uint8_t profile_idx, uint8_t motor);

// Debug
esp_err_t flow_model_get(uint8_t profile_idx, flow_model_t *out);

#ifdef __cplusplus
}
#endif

#endif // FLOW_MODEL_H_
