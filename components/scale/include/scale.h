#ifndef SCALE_H_
#define SCALE_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Scale driver types
typedef enum {
    SCALE_DRIVER_AND_FXI = 0,
    SCALE_DRIVER_STEINBERG_SBS = 1,
    SCALE_DRIVER_GNG_JJB = 2,
    SCALE_DRIVER_USSOLID_JFDBS = 3,
    SCALE_DRIVER_JM_SCIENCE = 4,
    SCALE_DRIVER_CREEDMOOR = 5,
    SCALE_DRIVER_RADWAG_PS_R2 = 6,
    SCALE_DRIVER_SARTORIUS = 7,
    SCALE_DRIVER_GENERIC_DRV = 8,
    SCALE_DRIVER_OHAUS_PIONEER = 9,
} scale_driver_t;

// Scale baudrates
typedef enum {
    BAUDRATE_4800 = 0,
    BAUDRATE_9600 = 1,
    BAUDRATE_19200 = 2,
} scale_baudrate_t;

// Scale actions
typedef enum {
    SCALE_ACTION_NO_ACTION = 0,
    SCALE_ACTION_FORCE_ZERO = 1,
} scale_action_t;

// Scale configuration structure (stored in NVS)
typedef struct {
    uint16_t config_version;        // Config version for compatibility
    scale_driver_t scale_driver;
    scale_baudrate_t scale_baudrate;
} scale_config_t;

// Initialize scale module
esp_err_t scale_init(void);

// Configuration management (NVS)
esp_err_t scale_save_config(const scale_config_t *config);
esp_err_t scale_load_config(scale_config_t *config);
esp_err_t scale_get_config(scale_config_t *config);

// Scale control
esp_err_t scale_set_driver(scale_driver_t driver);
esp_err_t scale_perform_action(scale_action_t action);
float scale_get_measurement(void);
bool scale_is_measurement_valid(void);
bool scale_block_wait_for_measurement(uint32_t timeout_ms, float *measurement);

#ifdef __cplusplus
}
#endif

#endif // SCALE_H_
