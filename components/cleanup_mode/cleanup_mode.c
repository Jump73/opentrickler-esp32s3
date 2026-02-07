#include "cleanup_mode.h"
#include "esp_log.h"
#include <string.h>

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
    ESP_LOGI(TAG, "Setting trickler speed to: %.3f (stub)", speed);
    runtime_state.trickler_speed = speed;

    // TODO: Set motor speed when motor control is implemented
    // motor_set_speed(SELECT_BOTH_MOTOR, speed);

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
