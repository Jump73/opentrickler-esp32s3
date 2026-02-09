/*
 * tmc_uart_hal_esp32.c - UART HAL for TMC2209 on ESP32-S3
 *
 * TMC2209 uses single-wire UART (PDN_UART pin). On the OpenTrickler PCB:
 *   - MCU TX (GPIO15, pin 6) → resistor → TMC PDN_UART
 *   - MCU RX (GPIO16, pin 7) ← direct  ← TMC PDN_UART
 *
 * Standard UART with separate TX/RX pins. TX echo appears on RX because
 * both are connected to the same TMC PDN_UART wire on the PCB.
 * The echo bytes are skipped during read operations.
 */

#include <string.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "common.h"
#include "board_pins.h"

static const char *TAG = "TMC_UART";

#define TMC_UART_NUM        MOTOR_UART_NUM
#define TMC_UART_BAUD       250000
#define TMC_UART_TX_PIN     MOTOR_UART_TX_PIN   // GPIO15 (pin 6)
#define TMC_UART_RX_PIN     MOTOR_UART_RX_PIN   // GPIO16 (pin 7)
#define TMC_UART_BUF_SIZE   256

static bool uart_initialized = false;

/* ────────────────────────── CRC ────────────────────────── */

static void tmc_uart_calc_crc(uint8_t *datagram, uint8_t datagram_length)
{
    uint8_t *crc = datagram + (datagram_length - 1);
    *crc = 0;
    for (int i = 0; i < (datagram_length - 1); i++) {
        uint8_t current_byte = datagram[i];
        for (int j = 0; j < 8; j++) {
            if ((*crc >> 7) ^ (current_byte & 0x01))
                *crc = (*crc << 1) ^ 0x07;
            else
                *crc = (*crc << 1);
            current_byte >>= 1;
        }
    }
}

/* ────────────────────────── Init ────────────────────────── */

esp_err_t tmc_uart_init(void)
{
    if (uart_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "TMC UART init: TX=GPIO%d, RX=GPIO%d, baud=%d",
             TMC_UART_TX_PIN, TMC_UART_RX_PIN, TMC_UART_BAUD);

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

    ESP_LOGI(TAG, "UART%d ready", TMC_UART_NUM);

    // Echo test: TX→resistor→RX on same bus, 0x55 should echo back
    uart_flush_input(TMC_UART_NUM);
    uint8_t test_byte = 0x55;
    uart_write_bytes(TMC_UART_NUM, &test_byte, 1);
    uart_wait_tx_done(TMC_UART_NUM, pdMS_TO_TICKS(100));

    esp_rom_delay_us(500);

    uint8_t rx_byte = 0;
    int rx_ret = uart_read_bytes(TMC_UART_NUM, &rx_byte, 1, pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "Echo test: rx=%d byte=0x%02X %s",
             rx_ret, rx_byte,
             (rx_ret == 1 && rx_byte == 0x55) ? "OK" : "NO ECHO (check wiring)");

    uart_initialized = true;
    return ESP_OK;
}

/* ────────────────────────── Write ────────────────────────── */

void tmc_uart_write(trinamic_motor_t driver, TMC_uart_write_datagram_t *datagram)
{
    if (!uart_initialized) {
        ESP_LOGE(TAG, "UART not initialized");
        return;
    }

    datagram->msg.sync = 0x05;
    datagram->msg.slave = driver.address;
    datagram->msg.addr.write = 1;
    tmc_uart_calc_crc(datagram->data, sizeof(TMC_uart_write_datagram_t));

    // Flush any stale data in RX buffer
    uart_flush_input(TMC_UART_NUM);

    // Send write datagram (8 bytes). Echo captured in RX (ignored).
    uart_write_bytes(TMC_UART_NUM, datagram->data, sizeof(TMC_uart_write_datagram_t));
    uart_wait_tx_done(TMC_UART_NUM, pdMS_TO_TICKS(10));
}

/* ────────────────────────── Read ────────────────────────── */

TMC_uart_write_datagram_t *tmc_uart_read(trinamic_motor_t driver, TMC_uart_read_datagram_t *datagram)
{
    static TMC_uart_write_datagram_t response = {0};
    uint8_t buf[20];  // 4 echo + 8 response + margin

    if (!uart_initialized) {
        ESP_LOGE(TAG, "UART not initialized");
        return NULL;
    }

    // Prepare read request
    datagram->msg.sync = 0x05;
    datagram->msg.slave = driver.address;
    datagram->msg.addr.write = 0;
    tmc_uart_calc_crc(datagram->data, sizeof(TMC_uart_read_datagram_t));

    // Flush RX buffer (clean slate)
    uart_flush_input(TMC_UART_NUM);

    // Send 4-byte read request. RX captures echo (4 bytes) on same pin.
    uart_write_bytes(TMC_UART_NUM, datagram->data, sizeof(TMC_uart_read_datagram_t));
    uart_wait_tx_done(TMC_UART_NUM, pdMS_TO_TICKS(10));

    // After TX done, UART TX goes idle HIGH (open-drain = released).
    // TMC2209 responds ~8 bit-times (~32us) later. Its response (8 bytes)
    // arrives on the same pin and is captured by UART RX.
    // Wait for full response to arrive.
    vTaskDelay(1);  // At least 1 tick (10ms at 100Hz) - generous but safe

    // Read everything in buffer: echo (4 bytes) + TMC response (8 bytes)
    int n = uart_read_bytes(TMC_UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(20));

    if (n <= 0) {
        ESP_LOGW(TAG, "TMC[%d] no data (got %d bytes)", driver.address, n);
        return NULL;
    }

    // Debug: log raw bytes received
    ESP_LOGI(TAG, "TMC[%d] RX %d bytes:", driver.address, n);
    for (int i = 0; i < n && i < 20; i += 8) {
        int remaining = n - i;
        if (remaining > 8) remaining = 8;
        char hex[64];
        int pos = 0;
        for (int j = 0; j < remaining; j++) {
            pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", buf[i + j]);
        }
        ESP_LOGI(TAG, "  [%d]: %s", i, hex);
    }

    if (n < 8) {
        ESP_LOGW(TAG, "TMC[%d] too few bytes for response (got %d, need 8)", driver.address, n);
        return NULL;
    }

    // Search for response header [0x05, 0xFF].
    // Echo starts with [0x05, slave_addr], response with [0x05, 0xFF].
    for (int i = 0; i <= n - 8; i++) {
        if (buf[i] == 0x05 && buf[i + 1] == 0xFF) {
            memcpy(response.data, &buf[i], 8);
            uint8_t received_crc = response.msg.crc;
            tmc_uart_calc_crc(response.data, sizeof(TMC_uart_write_datagram_t));
            if (received_crc == response.msg.crc) {
                ESP_LOGD(TAG, "TMC[%d] valid response at offset %d", driver.address, i);
                return &response;
            }
            ESP_LOGW(TAG, "TMC[%d] CRC mismatch at offset %d (got 0x%02X, calc 0x%02X)",
                     driver.address, i, received_crc, response.msg.crc);
        }
    }

    ESP_LOGW(TAG, "TMC[%d] no valid [05 FF] response in %d bytes", driver.address, n);
    return NULL;
}
