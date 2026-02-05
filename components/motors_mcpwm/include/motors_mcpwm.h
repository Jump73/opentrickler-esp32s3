#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Init MCPWM + ustawienie DIR jako GPIO (na razie testowo)
void motors_mcpwm_init(void);

// Start testowego STEP (square wave) na wybranym kanale, freq w Hz
void motors_mcpwm_start_test(uint32_t freq_hz);

// Stop generacji STEP
void motors_mcpwm_stop(void);

#ifdef __cplusplus
}
#endif
