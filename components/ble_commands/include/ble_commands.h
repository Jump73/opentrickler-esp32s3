#pragma once
#include <stdint.h>
#include <stddef.h>

/**
 * @brief Handle a single newline-terminated JSON command received over BLE.
 *
 * Parses the JSON, dispatches to the appropriate component API,
 * builds a JSON response (with "cmd" field), and sends it via ble_uart_send_str().
 *
 * Must be called from a task that can safely call component APIs
 * (e.g. the NimBLE host task or a dedicated command task).
 *
 * @param data Raw bytes (may NOT be null-terminated)
 * @param len  Length of data
 */
void ble_commands_handle(const uint8_t *data, size_t len);
