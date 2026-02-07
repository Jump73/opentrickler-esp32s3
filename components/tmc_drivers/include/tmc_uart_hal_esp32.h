/*
 * tmc_uart_hal_esp32.h - UART HAL for Trinamic drivers on ESP32
 */

#ifndef TMC_UART_HAL_ESP32_H_
#define TMC_UART_HAL_ESP32_H_

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize TMC UART peripheral
 * Must be called before any UART communication with TMC drivers
 *
 * @return ESP_OK on success
 */
esp_err_t tmc_uart_init(void);

#ifdef __cplusplus
}
#endif

#endif // TMC_UART_HAL_ESP32_H_
