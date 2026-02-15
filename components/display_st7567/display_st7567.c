#include "display_st7567.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <string.h>

static const char *TAG = "ST7567";

// DC pin GPIO number for pre-transfer callback
static int s_dc_gpio = -1;

// Pre-transfer callback: set DC pin from transaction user field
// user = (void*)0 for command, (void*)1 for data
static void IRAM_ATTR spi_pre_transfer_cb(spi_transaction_t *t)
{
    gpio_set_level((gpio_num_t)s_dc_gpio, (int)t->user);
}

static esp_err_t st7567_cmd(st7567_t *d, uint8_t c)
{
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &c,
        .user = (void *)0,  // DC = 0 (command)
    };
    return spi_device_polling_transmit(d->spi_dev, &t);
}

static esp_err_t st7567_data(st7567_t *d, const uint8_t *buf, size_t len)
{
    if (len == 0) return ESP_OK;
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = buf,
        .user = (void *)1,  // DC = 1 (data)
    };
    return spi_device_polling_transmit(d->spi_dev, &t);
}

esp_err_t st7567_init(st7567_t *d, const st7567_bus_t *bus)
{
    if (!d || !bus) return ESP_ERR_INVALID_ARG;
    d->bus = *bus;
    s_dc_gpio = bus->gpio_a0;

    ESP_LOGI(TAG, "init: host=%d sck=%d mosi=%d cs=%d a0=%d rst=%d clk=%d",
             (int)bus->host, bus->gpio_sck, bus->gpio_mosi,
             bus->gpio_cs, bus->gpio_a0, bus->gpio_rst, bus->clk_hz);

    // Configure DC (A0) pin as GPIO output
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << bus->gpio_a0),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level((gpio_num_t)bus->gpio_a0, 0);

    // Configure RST pin if used
    if (bus->gpio_rst >= 0) {
        io.pin_bit_mask = (1ULL << bus->gpio_rst);
        ESP_ERROR_CHECK(gpio_config(&io));
    }

    // Initialize SPI bus
    spi_bus_config_t buscfg = {
        .mosi_io_num = bus->gpio_mosi,
        .miso_io_num = -1,
        .sclk_io_num = bus->gpio_sck,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = ST7567_FB_SIZE,
    };
    esp_err_t ret = spi_bus_initialize(bus->host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Add ST7567 as SPI device
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = bus->clk_hz,
        .mode = 0,                    // SPI Mode 0 (CPOL=0, CPHA=0)
        .spics_io_num = bus->gpio_cs,
        .queue_size = 4,
        .pre_cb = spi_pre_transfer_cb,
    };
    ret = spi_bus_add_device(bus->host, &devcfg, &d->spi_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Hardware SPI configured on host %d at %d Hz", (int)bus->host, bus->clk_hz);

    // Hardware reset sequence
    if (bus->gpio_rst >= 0) {
        gpio_set_level((gpio_num_t)bus->gpio_rst, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level((gpio_num_t)bus->gpio_rst, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        gpio_set_level((gpio_num_t)bus->gpio_rst, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        ESP_LOGI(TAG, "reset pulse done");
    }

    // UC1701 mini12864 initialization sequence (exact match of u8g2 u8x8_d_uc1701_mini12864.c)
    ESP_ERROR_CHECK(st7567_cmd(d, 0xE2));  // System Reset
    vTaskDelay(pdMS_TO_TICKS(5));          // Wait after soft reset
    ESP_ERROR_CHECK(st7567_cmd(d, 0x40));  // Set scroll line to 0
    ESP_ERROR_CHECK(st7567_cmd(d, 0xA0));  // ADC set to normal
    ESP_ERROR_CHECK(st7567_cmd(d, 0xC8));  // COM Output Scan Direction: Reverse
    ESP_ERROR_CHECK(st7567_cmd(d, 0xA2));  // LCD Bias = 1/9
    // Staged power-up (per u8g2 and UC1701 datasheet)
    ESP_ERROR_CHECK(st7567_cmd(d, 0x2C));  // Booster Circuits ON
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_ERROR_CHECK(st7567_cmd(d, 0x2E));  // Booster + Voltage Regulator ON
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_ERROR_CHECK(st7567_cmd(d, 0x2F));  // Booster + Vreg + V-follower ON
    ESP_ERROR_CHECK(st7567_cmd(d, 0xF8));  // Set booster ratio (2-byte command)
    ESP_ERROR_CHECK(st7567_cmd(d, 0x00));  // Booster ratio = 4x
    ESP_ERROR_CHECK(st7567_cmd(d, 0x27));  // V0 voltage resistor ratio = 7
    ESP_ERROR_CHECK(st7567_cmd(d, 0x81));  // Set electronic volume (2-byte command)
    ESP_ERROR_CHECK(st7567_cmd(d, 0x10));  // Electronic volume value = 16
    ESP_ERROR_CHECK(st7567_cmd(d, 0xAC));  // Static indicator OFF (2-byte command)
    ESP_ERROR_CHECK(st7567_cmd(d, 0x00));  // Static indicator register = 0
    ESP_ERROR_CHECK(st7567_cmd(d, 0xA6));  // Normal display (bit inversion done in LVGL flush)
    // Init ends with display OFF in power-save mode (matches u8g2 behavior)
    ESP_ERROR_CHECK(st7567_cmd(d, 0xAE));  // Display OFF
    ESP_ERROR_CHECK(st7567_cmd(d, 0xA5));  // All pixels ON (power save)

    ESP_LOGI(TAG, "init done (hardware SPI, UC1701 mini12864 sequence)");
    return ESP_OK;
}

esp_err_t st7567_set_page_col(st7567_t *d, uint8_t page, uint8_t col)
{
    if (!d) return ESP_ERR_INVALID_ARG;
    ESP_ERROR_CHECK(st7567_cmd(d, 0xB0 | (page & 0x0F)));
    ESP_ERROR_CHECK(st7567_cmd(d, 0x10 | ((col >> 4) & 0x0F)));
    ESP_ERROR_CHECK(st7567_cmd(d, 0x00 | (col & 0x0F)));
    return ESP_OK;
}

esp_err_t st7567_write(st7567_t *d, const uint8_t *buf, size_t len)
{
    if (!d || !buf || !len) return ESP_ERR_INVALID_ARG;
    return st7567_data(d, buf, len);
}

esp_err_t st7567_write_framebuffer(st7567_t *d, const uint8_t *fb)
{
    if (!d || !fb) return ESP_ERR_INVALID_ARG;

    // Half-swap for UC1701 dual-scan LCD panel:
    // display columns 0-63 = physical RIGHT, columns 64-127 = physical LEFT
    for (int page = 0; page < ST7567_PAGES; page++) {
        // Buffer left half (cols 0-63) → display cols 64-127 (physical LEFT)
        ESP_ERROR_CHECK(st7567_set_page_col(d, (uint8_t)page, 64));
        ESP_ERROR_CHECK(st7567_data(d, &fb[page * ST7567_WIDTH], 64));
        // Buffer right half (cols 64-127) → display cols 0-63 (physical RIGHT)
        ESP_ERROR_CHECK(st7567_set_page_col(d, (uint8_t)page, 0));
        ESP_ERROR_CHECK(st7567_data(d, &fb[page * ST7567_WIDTH + 64], 64));
    }
    return ESP_OK;
}

esp_err_t st7567_power_save(st7567_t *d, bool on)
{
    if (!d) return ESP_ERR_INVALID_ARG;
    if (on) {
        // Enter power save: display OFF, all pixels ON
        ESP_ERROR_CHECK(st7567_cmd(d, 0xAE));  // Display OFF
        ESP_ERROR_CHECK(st7567_cmd(d, 0xA5));  // All pixels ON (power save)
    } else {
        // Exit power save: all pixels OFF, display ON
        ESP_ERROR_CHECK(st7567_cmd(d, 0xA4));  // All pixels OFF (normal)
        ESP_ERROR_CHECK(st7567_cmd(d, 0xAF));  // Display ON
    }
    ESP_LOGI(TAG, "power save %s", on ? "ON" : "OFF");
    return ESP_OK;
}

esp_err_t st7567_set_contrast(st7567_t *d, uint8_t contrast)
{
    if (!d) return ESP_ERR_INVALID_ARG;
    if (contrast > 63) contrast = 63;
    ESP_ERROR_CHECK(st7567_cmd(d, 0x81));
    ESP_ERROR_CHECK(st7567_cmd(d, contrast));
    return ESP_OK;
}
