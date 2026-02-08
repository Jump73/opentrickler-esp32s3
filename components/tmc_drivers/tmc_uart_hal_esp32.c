/*
 * tmc_uart_hal_esp32.c - UART HAL for Trinamic drivers on ESP32
 *
 * Implements UART communication functions for TMC stepper drivers
 * using ESP32-S3 UART peripheral.
 */

#include <string.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "common.h"

static const char *TAG = "TMC_UART";

// UART configuration for TMC drivers (pins from board_pins.h)
#include "board_pins.h"
#define TMC_UART_NUM        MOTOR_UART_NUM
#define TMC_UART_BAUD       250000
#define TMC_UART_TX_PIN     MOTOR_UART_TX_PIN
#define TMC_UART_RX_PIN     MOTOR_UART_RX_PIN
#define TMC_UART_BUF_SIZE   1024

static bool uart_initialized = false;

/**
 * Calculate CRC for TMC UART datagram
 * Uses CRC-8 polynomial 0x07
 */
static void tmc_uart_calc_crc(uint8_t* datagram, uint8_t datagram_length)
{
    int i, j;
    uint8_t* crc = datagram + (datagram_length - 1); // CRC located in last byte
    uint8_t current_byte;
    *crc = 0;

    for (i = 0; i < (datagram_length - 1); i++) {
        current_byte = datagram[i];
        for (j = 0; j < 8; j++) {
            if ((*crc >> 7) ^ (current_byte & 0x01)) {
                *crc = (*crc << 1) ^ 0x07;
            } else {
                *crc = (*crc << 1);
            }
            current_byte = current_byte >> 1;
        }
    }
}

/**
 * Initialize TMC UART peripheral
 * Must be called before any UART communication
 */
esp_err_t tmc_uart_init(void)
{
    if (uart_initialized) {
        return ESP_OK;
    }

    uart_config_t uart_config = {
        .baud_rate = TMC_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(TMC_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(TMC_UART_NUM, TMC_UART_TX_PIN, TMC_UART_RX_PIN,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(TMC_UART_NUM, TMC_UART_BUF_SIZE,
                                        TMC_UART_BUF_SIZE, 0, NULL, 0));

    // Note: ESP32 UART can't easily disable RX, but we'll flush before reads

    uart_initialized = true;
    ESP_LOGI(TAG, "TMC UART initialized (TX=%d, RX=%d, baud=%d)",
             TMC_UART_TX_PIN, TMC_UART_RX_PIN, TMC_UART_BAUD);

    return ESP_OK;
}

/**
 * Write a datagram to TMC driver via UART
 * This is a fire-and-forget write operation
 */
void tmc_uart_write(trinamic_motor_t driver, TMC_uart_write_datagram_t *datagram)
{
    if (!uart_initialized) {
        ESP_LOGE(TAG, "UART not initialized");
        return;
    }

    // Prepare the datagram with sync byte, slave address, and CRC
    datagram->msg.sync = 0x05;
    datagram->msg.slave = driver.address;
    datagram->msg.addr.write = 1; // Write operation
    tmc_uart_calc_crc(datagram->data, sizeof(TMC_uart_write_datagram_t));

    // Send the datagram
    uart_write_bytes(TMC_UART_NUM, datagram->data, sizeof(TMC_uart_write_datagram_t));
    uart_wait_tx_done(TMC_UART_NUM, pdMS_TO_TICKS(10));
}

/**
 * Read a register from TMC driver via UART
 *
 * Single-wire half-duplex UART: TX and RX share GPIO15 (same as original Pico design).
 * Our 4-byte TX echo appears on RX first, then TMC sends 8-byte response.
 * Total up to 12 bytes. Response identified by [0x05, 0xFF] header.
 *
 * Returns: Pointer to static response datagram, or NULL on error
 */
TMC_uart_write_datagram_t *tmc_uart_read(trinamic_motor_t driver, TMC_uart_read_datagram_t *datagram)
{
    static TMC_uart_write_datagram_t response = {0};
    uint8_t buf[12];  // Up to 12 bytes: handles both echo+response and response-only cases

    if (!uart_initialized) {
        ESP_LOGE(TAG, "UART not initialized");
        return NULL;
    }

    // Prepare read request datagram
    datagram->msg.sync = 0x05;
    datagram->msg.slave = driver.address;
    datagram->msg.addr.write = 0; // Read operation
    tmc_uart_calc_crc(datagram->data, sizeof(TMC_uart_read_datagram_t));

    // Flush RX buffer before sending request
    uart_flush_input(TMC_UART_NUM);

    // Send read request
    uart_write_bytes(TMC_UART_NUM, datagram->data, sizeof(TMC_uart_read_datagram_t));
    uart_wait_tx_done(TMC_UART_NUM, pdMS_TO_TICKS(10));

    // Single-wire UART: echo of our TX + TMC response both arrive on RX (GPIO15).
    // Total: 4 echo bytes + 8 response bytes = 12 bytes.
    // Wait for all bytes to arrive (TX=160us + TMC response=320us + margin).
    vTaskDelay(pdMS_TO_TICKS(2));

    // Read up to 12 bytes (works for both echo+response and response-only hardware)
    int n = uart_read_bytes(TMC_UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(5));
    if (n < 8) {
        ESP_LOGW(TAG, "TMC[%d] timeout (got %d bytes, need 8)", driver.address, n);
        return NULL;
    }

    // Find response by looking for [0x05, 0xFF] header
    // Echo starts with [0x05, driver_addr] (0 or 1), response with [0x05, 0xFF]
    for (int i = 0; i <= n - 8; i++) {
        if (buf[i] == 0x05 && buf[i + 1] == 0xFF) {
            memcpy(response.data, &buf[i], 8);
            // Verify CRC
            uint8_t received_crc = response.msg.crc;
            tmc_uart_calc_crc(response.data, sizeof(TMC_uart_write_datagram_t));
            if (received_crc == response.msg.crc) {
                return &response;
            }
            ESP_LOGW(TAG, "TMC[%d] CRC mismatch (got 0x%02X, expected 0x%02X)",
                     driver.address, received_crc, response.msg.crc);
        }
    }

    ESP_LOGW(TAG, "TMC[%d] no valid response found in %d bytes", driver.address, n);
    return NULL;
}
