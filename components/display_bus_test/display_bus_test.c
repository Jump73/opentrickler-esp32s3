#include <stdio.h>
#include "ot_pins.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display_st7567.h"
#include "input_encoder.h"

static const char *TAG = "UI_SIMPLE";

/* Use pins from ot_pins.h - NO local overrides! */
// LCD pins: LCD_SCK, LCD_MOSI, LCD_CS, LCD_A0, LCD_RST
// Encoder pins: BTN_EN1, BTN_EN2, ENC_BTN
#define ENC_A     BTN_EN1
#define ENC_B     BTN_EN2
// ENC_BTN already defined in ot_pins.h

/* 128x64 = 8 pages x 128 cols */
static uint8_t fb[128 * 8];

/* Minimalny font 5x7 (kolumny) tylko dla znaków używanych na ekranie */
static const uint8_t* font5x7(char c)
{
    /* Każdy znak = 5 bajtów (kolumny), bit0 = górny piksel w wierszu */
    switch (c) {
        case ' ': { static const uint8_t g[5]={0,0,0,0,0}; return g; }
        case ':': { static const uint8_t g[5]={0x00,0x36,0x36,0x00,0x00}; return g; }
        case '.': { static const uint8_t g[5]={0x00,0x60,0x60,0x00,0x00}; return g; }
        case '-': { static const uint8_t g[5]={0x08,0x08,0x08,0x08,0x08}; return g; }

        case '0': { static const uint8_t g[5]={0x3E,0x51,0x49,0x45,0x3E}; return g; }
        case '1': { static const uint8_t g[5]={0x00,0x42,0x7F,0x40,0x00}; return g; }
        case '2': { static const uint8_t g[5]={0x42,0x61,0x51,0x49,0x46}; return g; }
        case '3': { static const uint8_t g[5]={0x21,0x41,0x45,0x4B,0x31}; return g; }
        case '4': { static const uint8_t g[5]={0x18,0x14,0x12,0x7F,0x10}; return g; }
        case '5': { static const uint8_t g[5]={0x27,0x45,0x45,0x45,0x39}; return g; }
        case '6': { static const uint8_t g[5]={0x3C,0x4A,0x49,0x49,0x30}; return g; }
        case '7': { static const uint8_t g[5]={0x01,0x71,0x09,0x05,0x03}; return g; }
        case '8': { static const uint8_t g[5]={0x36,0x49,0x49,0x49,0x36}; return g; }
        case '9': { static const uint8_t g[5]={0x06,0x49,0x49,0x29,0x1E}; return g; }

        case 'A': { static const uint8_t g[5]={0x7E,0x11,0x11,0x11,0x7E}; return g; }
        case 'D': { static const uint8_t g[5]={0x7F,0x41,0x41,0x22,0x1C}; return g; }
        case 'E': { static const uint8_t g[5]={0x7F,0x49,0x49,0x49,0x41}; return g; }
        case 'G': { static const uint8_t g[5]={0x3E,0x41,0x49,0x49,0x3A}; return g; }
        case 'H': { static const uint8_t g[5]={0x7F,0x08,0x08,0x08,0x7F}; return g; }
        case 'I': { static const uint8_t g[5]={0x00,0x41,0x7F,0x41,0x00}; return g; }
        case 'L': { static const uint8_t g[5]={0x7F,0x40,0x40,0x40,0x40}; return g; }
        case 'N': { static const uint8_t g[5]={0x7F,0x02,0x04,0x08,0x7F}; return g; }
        case 'O': { static const uint8_t g[5]={0x3E,0x41,0x41,0x41,0x3E}; return g; }
        case 'R': { static const uint8_t g[5]={0x7F,0x09,0x19,0x29,0x46}; return g; }
        case 'S': { static const uint8_t g[5]={0x46,0x49,0x49,0x49,0x31}; return g; }
        case 'T': { static const uint8_t g[5]={0x01,0x01,0x7F,0x01,0x01}; return g; }
        case 'U': { static const uint8_t g[5]={0x3F,0x40,0x40,0x40,0x3F}; return g; }
        case 'W': { static const uint8_t g[5]={0x7F,0x20,0x18,0x20,0x7F}; return g; }

        case 'g': { static const uint8_t g[5]={0x0C,0x52,0x52,0x52,0x3E}; return g; } // małe g
        default:  { static const uint8_t g[5]={0,0,0,0,0}; return g; }
    }
}

static void fb_clear(void)
{
    memset(fb, 0, sizeof(fb));
}

/* Rysuje znak w siatce 6x8 (5 kolumn + 1 odstępu), y_page = 0..7 */
static void fb_draw_char(int x, int y_page, char c)
{
    if (y_page < 0 || y_page > 7) return;
    if (x < 0 || x > 127) return;

    const uint8_t *g = font5x7(c);
    int base = y_page * 128 + x;

    for (int i=0;i<5;i++) {
        int xi = x + i;
        if (xi >= 0 && xi < 128) fb[y_page*128 + xi] = g[i];
    }
    int sp = x + 5;
    if (sp >= 0 && sp < 128) fb[y_page*128 + sp] = 0x00;
}

static void fb_draw_text(int x, int y_page, const char *s)
{
    int cx = x;
    while (*s) {
        fb_draw_char(cx, y_page, *s++);
        cx += 6;
        if (cx > 127) break;
    }
}

static void lcd_flush(st7567_t *lcd)
{
    for (int p=0; p<8; p++) {
        st7567_set_page_col(lcd, (uint8_t)p, 0);
        st7567_write(lcd, &fb[p*128], 128);
    }
}

static void render_screen(st7567_t *lcd, int32_t target_mg, int32_t weight_mg, int running)
{
    char line1[32], line2[32], line3[32];

    int32_t t_g = target_mg / 1000;
    int32_t t_m = target_mg % 1000;
    if (t_m < 0) t_m = -t_m;

    int32_t w_g = weight_mg / 1000;
    int32_t w_m = weight_mg % 1000;
    if (w_m < 0) w_m = -w_m;

    snprintf(line1, sizeof(line1), "TARGET: %2ld.%03ld g", (long)t_g, (long)t_m);
    snprintf(line2, sizeof(line2), "WEIGHT: %2ld.%03ld g", (long)w_g, (long)w_m);
    snprintf(line3, sizeof(line3), "STATUS: %s", running ? "RUN" : "IDLE");

    fb_clear();
    fb_draw_text(0, 0, line1);
    fb_draw_text(0, 2, line2);
    fb_draw_text(0, 4, line3);
    lcd_flush(lcd);
}

void display_bus_test_start(void)
{
    ESP_LOGI(TAG, "UI start (ST7567 + encoder).");

    st7567_t lcd = {0};
    st7567_bus_t bus = {
        .host = SPI3_HOST,  // CHANGED from SPI2_HOST - try SPI3
        .gpio_sck = LCD_SCK,
        .gpio_mosi = LCD_MOSI,
        .gpio_cs = LCD_CS,
        .gpio_a0 = LCD_A0,
        .gpio_rst = LCD_RST,
        .clk_hz = 100000  // REDUCED to 100kHz for debugging
    };

    ESP_LOGI(TAG, "LCD config: SCK=%d MOSI=%d CS=%d A0=%d RST=%d",
             LCD_SCK, LCD_MOSI, LCD_CS, LCD_A0, LCD_RST);
    ESP_ERROR_CHECK(st7567_init(&lcd, &bus));

    // TEST: Fill with pattern to verify SPI works
    ESP_LOGI(TAG, "Filling test pattern...");
    st7567_fill_test_pattern(&lcd);
    ESP_LOGI(TAG, "Pattern sent. Check LCD for stripes!");
    vTaskDelay(pdMS_TO_TICKS(2000)); // Wait 2s to see pattern

    encoder_cfg_t ecfg = {
        .gpio_a = ENC_A,
        .gpio_b = ENC_B,
        .gpio_btn = ENC_BTN,
        .pullups = true,
        .position = 0
    };
    encoder_init(&ecfg);

    int running = 0;
    int32_t target_mg = 2500; // 2.500 g
    int32_t weight_mg = 0;    // placeholder

    render_screen(&lcd, target_mg, weight_mg, running);

    while (1) {
        enc_event_t evt;
        int changed = 0;

        while (encoder_poll(&evt)) {
            if (evt == ENC_EVT_CW) {
                target_mg += 10; // 0.010 g
                if (target_mg > 99999) target_mg = 99999;
                changed = 1;
            } else if (evt == ENC_EVT_CCW) {
                target_mg -= 10;
                if (target_mg < 0) target_mg = 0;
                changed = 1;
            } else if (evt == ENC_EVT_BTN_CLICK) {
                running = !running;
                changed = 1;
            }
        }

        if (changed) {
            render_screen(&lcd, target_mg, weight_mg, running);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}





