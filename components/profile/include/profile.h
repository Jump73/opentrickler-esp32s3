#ifndef PROFILE_H_
#define PROFILE_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PROFILE_NAME_MAX_LEN    16
#define MAX_PROFILE_CNT         8

// Profile structure with PID parameters for motors
typedef struct {
    uint32_t rev;
    uint32_t compatibility;

    char name[PROFILE_NAME_MAX_LEN];

    // Coarse motor PID parameters
    float coarse_kp;
    float coarse_ki;
    float coarse_kd;
    float coarse_min_flow_speed_rps;
    float coarse_max_flow_speed_rps;

    // Fine motor PID parameters
    float fine_kp;
    float fine_ki;
    float fine_kd;
    float fine_min_flow_speed_rps;
    float fine_max_flow_speed_rps;
} profile_t;

// Profile data stored in NVS
typedef struct {
    uint16_t profile_data_rev;
    uint16_t current_profile_idx;
    profile_t profiles[MAX_PROFILE_CNT];
} profile_data_t;

// Initialize profile system
esp_err_t profile_init(void);

// Save/load profile data (NVS)
esp_err_t profile_save(void);
esp_err_t profile_load(profile_data_t *data);

// Profile selection
esp_err_t profile_select(uint8_t idx);
uint16_t profile_get_selected_idx(void);
profile_t* profile_get_selected(void);
profile_t* profile_get_by_idx(uint8_t idx);

// Get all profile names for summary
esp_err_t profile_get_all_names(char names[][PROFILE_NAME_MAX_LEN], uint8_t *count);

#ifdef __cplusplus
}
#endif

#endif // PROFILE_H_
