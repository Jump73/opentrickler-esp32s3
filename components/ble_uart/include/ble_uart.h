#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Callback invoked in the NimBLE host task when data arrives on RX characteristic.
// The buffer is valid only for the duration of the call.
typedef void (*ble_uart_rx_cb_t)(const uint8_t *data, size_t len);

// Initialise NimBLE stack and start advertising.
// device_name : GAP device name (NULL → "OpenTrickler")
// rx_cb       : called when phone writes to RX characteristic (may be NULL)
esp_err_t ble_uart_init(const char *device_name, ble_uart_rx_cb_t rx_cb);

// Send data to connected phone via TX notification.
// Returns ESP_ERR_INVALID_STATE if no phone is connected,
//         ESP_ERR_NO_MEM if mbuf allocation fails.
esp_err_t ble_uart_send(const uint8_t *data, size_t len);

// Convenience wrapper: send a null-terminated string.
esp_err_t ble_uart_send_str(const char *str);

// Returns true if a phone is currently connected.
bool ble_uart_is_connected(void);

#ifdef __cplusplus
}
#endif
