#include "cleanup_mode.h"
#include "motors.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char *TAG = "CleanupMode";

// Runtime state (not persisted)
static cleanup_mode_runtime_state_t runtime_state = {
    .trickler_speed = 0.0f,
    .cleanup_mode_state = CLEANUP_MODE_EXIT
};

esp_err_t cleanup_mode_init(void)
{
    ESP_LOGI(TAG, "Initializing cleanup mode module");

    // Reset to exit state
    runtime_state.trickler_speed = 0.0f;
    runtime_state.cleanup_mode_state = CLEANUP_MODE_EXIT;

    return ESP_OK;
}

esp_err_t cleanup_mode_set_state(cleanup_mode_state_t state)
{
    ESP_LOGI(TAG, "Setting cleanup mode state to: %d", state);

    // Handle state transitions (stub)
    if (state == CLEANUP_MODE_EXIT && runtime_state.cleanup_mode_state != CLEANUP_MODE_EXIT) {
        ESP_LOGI(TAG, "Exiting cleanup mode");
        runtime_state.trickler_speed = 0.0f;
    } else if (state == CLEANUP_MODE_ENTER && runtime_state.cleanup_mode_state != CLEANUP_MODE_ENTER) {
        ESP_LOGI(TAG, "Entering cleanup mode");
    }

    runtime_state.cleanup_mode_state = state;
    return ESP_OK;
}

esp_err_t cleanup_mode_set_speed(float speed)
{
    ESP_LOGI(TAG, "Setting trickler speed to: %.3f", speed);
    runtime_state.trickler_speed = speed;

    // Enable motors if speed != 0 (supports negative for reverse), disable if speed == 0
    if (fabsf(speed) > 0.001f) {
        motor_enable(MOTOR_COARSE, true);
        motor_enable(MOTOR_FINE, true);
        motor_set_speed(MOTOR_COARSE, speed);
        motor_set_speed(MOTOR_FINE, speed);
    } else {
        motor_set_speed(MOTOR_COARSE, 0.0f);
        motor_set_speed(MOTOR_FINE, 0.0f);
        motor_enable(MOTOR_COARSE, false);
        motor_enable(MOTOR_FINE, false);
    }

    return ESP_OK;
}

esp_err_t cleanup_mode_get_state(cleanup_mode_runtime_state_t *state)
{
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }

    *state = runtime_state;
    return ESP_OK;
}
