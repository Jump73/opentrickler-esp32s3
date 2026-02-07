#include "display_st7567.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "rom/ets_sys.h"  // For esp_rom_delay_us

static const char *TAG = "ST7567";

static inline void set_level(int pin, int lvl) {
    if (pin >= 0) gpio_set_level((gpio_num_t)pin, lvl);
}

// Software SPI bit-banging
static void spi_tx_byte_software(st7567_t *d, uint8_t byte) {
    for (int bit = 7; bit >= 0; bit--) {
        // Set MOSI
        set_level(d->bus.gpio_mosi, (byte >> bit) & 1);

        // Clock pulse: LOW -> HIGH -> LOW
        set_level(d->bus.gpio_sck, 0);
        esp_rom_delay_us(1);  // Small delay
        set_level(d->bus.gpio_sck, 1);
        esp_rom_delay_us(1);  // Small delay
        set_level(d->bus.gpio_sck, 0);
        esp_rom_delay_us(1);  // Small delay
    }
}

static esp_err_t spi_tx(st7567_t *d, const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        spi_tx_byte_software(d, buf[i]);
    }
    return ESP_OK;
}

static esp_err_t cmd(st7567_t *d, uint8_t c) {
    set_level(d->bus.gpio_a0, 0);
    set_level(d->bus.gpio_cs, 0);
    esp_err_t r = spi_tx(d, &c, 1);
    set_level(d->bus.gpio_cs, 1);
    return r;
}

static esp_err_t data(st7567_t *d, const uint8_t *buf, size_t len) {
    set_level(d->bus.gpio_a0, 1);
    set_level(d->bus.gpio_cs, 0);
    esp_err_t r = spi_tx(d, buf, len);
    set_level(d->bus.gpio_cs, 1);
    return r;
}

esp_err_t st7567_init(st7567_t *d, const st7567_bus_t *bus)
{
    if (!d || !bus) return ESP_ERR_INVALID_ARG;
    d->bus = *bus;

    ESP_LOGI(TAG, "init: host=%d sck=%d mosi=%d cs=%d a0=%d rst=%d clk=%d",
             (int)bus->host, bus->gpio_sck, bus->gpio_mosi, bus->gpio_cs, bus->gpio_a0, bus->gpio_rst, bus->clk_hz);

    // Configure all GPIO pins as outputs
    gpio_config_t io = {0};
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_down_en = 0;
    io.pull_up_en = 0;
    io.intr_type = GPIO_INTR_DISABLE;

    // Configure CS, A0, SCK, MOSI
    io.pin_bit_mask = (1ULL<<bus->gpio_cs) | (1ULL<<bus->gpio_a0) |
                      (1ULL<<bus->gpio_sck) | (1ULL<<bus->gpio_mosi);
    ESP_ERROR_CHECK(gpio_config(&io));

    // SPECIAL: Set maximum drive strength for GPIO42 (SCK) if used
    if (bus->gpio_sck == 42 || bus->gpio_sck == 41 || bus->gpio_sck == 2) {
        ESP_LOGI(TAG, "Setting MAXIMUM drive strength for GPIO%d", bus->gpio_sck);
        gpio_set_drive_capability((gpio_num_t)bus->gpio_sck, GPIO_DRIVE_CAP_3);
    }
    if (bus->gpio_mosi == 42 || bus->gpio_mosi == 41 || bus->gpio_mosi == 2) {
        ESP_LOGI(TAG, "Setting MAXIMUM drive strength for GPIO%d", bus->gpio_mosi);
        gpio_set_drive_capability((gpio_num_t)bus->gpio_mosi, GPIO_DRIVE_CAP_3);
    }
    if (bus->gpio_cs == 42 || bus->gpio_cs == 41) {
        ESP_LOGI(TAG, "Setting MAXIMUM drive strength for GPIO%d", bus->gpio_cs);
        gpio_set_drive_capability((gpio_num_t)bus->gpio_cs, GPIO_DRIVE_CAP_3);
    }
    if (bus->gpio_a0 >= 0) {
        gpio_set_drive_capability((gpio_num_t)bus->gpio_a0, GPIO_DRIVE_CAP_3);
    }

    // Set initial levels
    set_level(bus->gpio_cs, 1);     // CS idle HIGH
    set_level(bus->gpio_a0, 0);     // A0/DC default to command mode
    set_level(bus->gpio_sck, 0);    // SCK idle LOW
    set_level(bus->gpio_mosi, 0);   // MOSI idle LOW

    ESP_LOGI(TAG, "SOFTWARE SPI mode - GPIO configured with MAX drive strength");

    // Reset sequence
    if (bus->gpio_rst >= 0) {
        io.pin_bit_mask = (1ULL<<bus->gpio_rst);
        ESP_ERROR_CHECK(gpio_config(&io));
        set_level(bus->gpio_rst, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
        set_level(bus->gpio_rst, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        set_level(bus->gpio_rst, 1);
        vTaskDelay(pdMS_TO_TICKS(100));  // Longer delay after reset
        ESP_LOGI(TAG, "rst pulse done (software SPI)");
    } else {
        ESP_LOGI(TAG, "rst disabled");
    }

    ESP_ERROR_CHECK(cmd(d, 0xAE));   // Display OFF
    ESP_ERROR_CHECK(cmd(d, 0xA2));   // Bias 1/9
    ESP_ERROR_CHECK(cmd(d, 0xA0));   // ADC normal
    ESP_ERROR_CHECK(cmd(d, 0xC8));   // COM reverse
    ESP_ERROR_CHECK(cmd(d, 0x22));   // Regulation ratio
    ESP_ERROR_CHECK(cmd(d, 0x81));   // Set contrast (2-byte command)
    ESP_ERROR_CHECK(cmd(d, 0x3F));   // Contrast value MAX (0x3F = 63)
    ESP_ERROR_CHECK(cmd(d, 0x2F));   // Power control ON
    ESP_ERROR_CHECK(cmd(d, 0x40));   // Start line 0
    ESP_ERROR_CHECK(cmd(d, 0xAF));   // Display ON

    ESP_LOGI(TAG, "init done");
    return ESP_OK;
}

esp_err_t st7567_set_page_col(st7567_t *d, uint8_t page, uint8_t col)
{
    if (!d) return ESP_ERR_INVALID_ARG;
    ESP_ERROR_CHECK(cmd(d, 0xB0 | (page & 0x0F)));
    ESP_ERROR_CHECK(cmd(d, 0x10 | ((col >> 4) & 0x0F)));
    ESP_ERROR_CHECK(cmd(d, 0x00 | (col & 0x0F)));
    return ESP_OK;
}

esp_err_t st7567_write(st7567_t *d, const uint8_t *buf, size_t len)
{
    if (!d || !buf || !len) return ESP_ERR_INVALID_ARG;
    return data(d, buf, len);
}

esp_err_t st7567_fill_test_pattern(st7567_t *d)
{
    if (!d) return ESP_ERR_INVALID_ARG;
    uint8_t line[128];
    for (int i=0;i<128;i++) line[i] = (i & 1) ? 0xAA : 0x55;

    for (int p=0;p<8;p++) {
        ESP_ERROR_CHECK(st7567_set_page_col(d, (uint8_t)p, 0));
        ESP_ERROR_CHECK(st7567_write(d, line, sizeof(line)));
    }
    ESP_LOGI(TAG, "pattern sent");
    return ESP_OK;
}
