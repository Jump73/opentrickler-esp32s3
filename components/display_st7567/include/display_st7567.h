#pragma once
#include <stdint.h>
#include <stddef.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"

typedef struct {
    spi_host_device_t host;
    int gpio_sck;
    int gpio_mosi;
    int gpio_cs;
    int gpio_a0;
    int gpio_rst;   // -1 jeśli nieużywany
    int clk_hz;
} st7567_bus_t;

typedef struct {
    st7567_bus_t bus;
    spi_device_handle_t dev;
} st7567_t;

esp_err_t st7567_init(st7567_t *d, const st7567_bus_t *bus);
esp_err_t st7567_set_page_col(st7567_t *d, uint8_t page, uint8_t col);
esp_err_t st7567_write(st7567_t *d, const uint8_t *data, size_t len);
esp_err_t st7567_fill_test_pattern(st7567_t *d);
