#ifndef CLEANUP_MODE_H_
#define CLEANUP_MODE_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Cleanup mode states
typedef enum {
    CLEANUP_MODE_EXIT = 0,
    CLEANUP_MODE_ENTER = 1,
} cleanup_mode_state_t;

// Cleanup mode runtime state (not saved to NVS)
typedef struct {
    float trickler_speed;
    cleanup_mode_state_t cleanup_mode_state;
} cleanup_mode_runtime_state_t;

// Initialize cleanup mode module
esp_err_t cleanup_mode_init(void);

// State management
esp_err_t cleanup_mode_set_state(cleanup_mode_state_t state);
esp_err_t cleanup_mode_set_speed(float speed);
esp_err_t cleanup_mode_get_state(cleanup_mode_runtime_state_t *state);

#ifdef __cplusplus
}
#endif

#endif // CLEANUP_MODE_H_
