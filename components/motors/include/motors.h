#ifndef MOTORS_H_
#define MOTORS_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Motor configuration structure (stored in NVS)
typedef struct {
    uint16_t config_version;        // Config version for compatibility
    uint32_t full_steps_per_rotation;
    uint16_t current_ma;
    uint16_t microsteps;
    uint16_t max_speed_rps;
    uint16_t r_sense;
    float angular_acceleration;     // In rev/s^2
    float min_speed_rps;
    float gear_ratio;
    bool inverted_direction;
    bool inverted_enable;
} motor_config_t;

// Motor selection enum
typedef enum {
    MOTOR_COARSE = 0,
    MOTOR_FINE = 1
} motor_type_t;

// Initialize motors module
esp_err_t motors_init(void);

// Configuration management (NVS)
esp_err_t motors_save_config(motor_type_t motor, const motor_config_t *config);
esp_err_t motors_load_config(motor_type_t motor, motor_config_t *config);
esp_err_t motors_get_config(motor_type_t motor, motor_config_t *config);

// Motor control (stubs for now - hardware implementation later)
esp_err_t motor_set_speed(motor_type_t motor, float speed_rps);
esp_err_t motor_enable(motor_type_t motor, bool enable);

#ifdef __cplusplus
}
#endif

#endif // MOTORS_H_
