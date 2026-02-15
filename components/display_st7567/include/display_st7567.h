#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/spi_master.h"

#define ST7567_WIDTH  128
#define ST7567_HEIGHT 64
#define ST7567_PAGES  (ST7567_HEIGHT / 8)
#define ST7567_FB_SIZE (ST7567_WIDTH * ST7567_PAGES)  // 1024 bytes

typedef struct {
    spi_host_device_t host;
    int gpio_sck;
    int gpio_mosi;
    int gpio_cs;
    int gpio_a0;      // DC pin (0=command, 1=data)
    int gpio_rst;     // -1 if unused
    int clk_hz;       // SPI clock frequency (e.g. 4000000)
} st7567_bus_t;

typedef struct {
    st7567_bus_t bus;
    spi_device_handle_t spi_dev;
} st7567_t;

// Initialize ST7567 display with hardware SPI
esp_err_t st7567_init(st7567_t *d, const st7567_bus_t *bus);

// Set page (0-7) and column (0-127) address
esp_err_t st7567_set_page_col(st7567_t *d, uint8_t page, uint8_t col);

// Write raw data bytes to display (at current page/col address)
esp_err_t st7567_write(st7567_t *d, const uint8_t *data, size_t len);

// Write full framebuffer (1024 bytes, page format: fb[page*128+col])
esp_err_t st7567_write_framebuffer(st7567_t *d, const uint8_t *fb);

// Set display power save mode (true=OFF/sleep, false=ON/active)
esp_err_t st7567_power_save(st7567_t *d, bool on);

// Set display contrast (0-63)
esp_err_t st7567_set_contrast(st7567_t *d, uint8_t contrast);

