#include "lvgl_port.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "LVGL_PORT";

#define LVGL_TASK_STACK_SIZE  (8 * 1024)
#define LVGL_TASK_PRIORITY    5
#define LVGL_TICK_PERIOD_MS   2
#define LVGL_TASK_PERIOD_MS   10

// Module state
static st7567_t *s_lcd = NULL;
static SemaphoreHandle_t s_lvgl_mutex = NULL;
static lv_display_t *s_disp = NULL;
static lv_indev_t *s_indev_encoder = NULL;

// Framebuffer in ST7567 page format (used by flush callback)
static uint8_t s_st7567_fb[ST7567_FB_SIZE];

// LVGL draw buffers - must be sized for LVGL's internal rendering format.
// LVGL v9 renders at LV_COLOR_DEPTH (default 16-bit) then converts to
// the display color format (I1) in layer_reshape_draw_buf.
// Buffer size = width * height * bytes_per_pixel_at_render_depth
#define LVGL_DRAW_BUF_SIZE  (ST7567_WIDTH * ST7567_HEIGHT * (LV_COLOR_DEPTH / 8))
static uint8_t s_lv_buf1[LVGL_DRAW_BUF_SIZE];
static uint8_t s_lv_buf2[LVGL_DRAW_BUF_SIZE];

// ---------------------------------------------------------------------------
// Display flush callback
// Converts LVGL I1 horizontal bit-packing to ST7567 vertical page format
// ---------------------------------------------------------------------------
//
// LVGL I1 format: each byte = 8 horizontal pixels, MSB = leftmost pixel
//   byte index = row * stride + col/8, bit = 7 - (col % 8)
//   stride = ST7567_WIDTH / 8 = 16 bytes per row
//
// ST7567 page format: each byte = 8 vertical pixels in a column, LSB = top
//   byte index = page * 128 + col, bit n = pixel at row (page*8 + n)
//
static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    static bool first_flush = true;
    if (first_flush) {
        ESP_LOGI(TAG, "First LVGL flush: area=(%d,%d)-(%d,%d)",
                 (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2);
        first_flush = false;
    }

    // For full-frame render mode, area covers entire display
    const int stride = ST7567_WIDTH / 8;  // 16

    memset(s_st7567_fb, 0, sizeof(s_st7567_fb));

    for (int page = 0; page < ST7567_PAGES; page++) {
        for (int col = 0; col < ST7567_WIDTH; col++) {
            uint8_t page_byte = 0;
            for (int bit = 0; bit < 8; bit++) {
                int row = page * 8 + bit;
                int byte_idx = row * stride + (col / 8);
                int bit_idx = 7 - (col % 8);
                // LVGL I1: 1 = white/bright, 0 = black/dark
                // UC1701 normal mode (0xA6): 1 = pixel ON (dark), 0 = pixel OFF (light)
                // Invert: LVGL white (1) → UC1701 OFF (0), LVGL black (0) → UC1701 ON (1)
                if (!(px_map[byte_idx] & (1 << bit_idx))) {
                    page_byte |= (1 << bit);
                }
            }
            s_st7567_fb[page * ST7567_WIDTH + col] = page_byte;
        }
    }

    // Compensate 1-pixel vertical shift on left physical panel (cols 0-63 in buffer).
    // The left half appears shifted UP by 1 pixel relative to the right half.
    // Shift left-half pixel data DOWN by 1 row: move bits toward LSB, carry bit 0
    // from each page to bit 7 of the previous page.  Iterate from last page to first.
    for (int col = 0; col < 64; col++) {
        uint8_t carry = 0;
        for (int page = ST7567_PAGES - 1; page >= 0; page--) {
            int idx = page * ST7567_WIDTH + col;
            uint8_t old_byte = s_st7567_fb[idx];
            s_st7567_fb[idx] = (old_byte >> 1) | (carry << 7);
            carry = old_byte & 1;
        }
    }

    // Write page-by-page with half-swap (UC1701 dual-scan LCD panel:
    // display columns 0-63 = physical RIGHT, columns 64-127 = physical LEFT)
    for (int page = 0; page < ST7567_PAGES; page++) {
        // LVGL left half (cols 0-63) → display cols 64-127 (physical LEFT)
        st7567_set_page_col(s_lcd, (uint8_t)page, 64);
        st7567_write(s_lcd, &s_st7567_fb[page * ST7567_WIDTH], 64);
        // LVGL right half (cols 64-127) → display cols 0-63 (physical RIGHT)
        st7567_set_page_col(s_lcd, (uint8_t)page, 0);
        st7567_write(s_lcd, &s_st7567_fb[page * ST7567_WIDTH + 64], 64);
        taskYIELD();
    }
    lv_display_flush_ready(disp);
}

// ---------------------------------------------------------------------------
// Encoder input read callback
// ---------------------------------------------------------------------------
static void encoder_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    static bool s_btn_pressed = false;
    enc_event_t evt;

    data->enc_diff = 0;

    // Drain event queue - track physical button state
    while (encoder_poll(&evt)) {
        switch (evt) {
        case ENC_EVT_CW:
            data->enc_diff++;
            break;
        case ENC_EVT_CCW:
            data->enc_diff--;
            break;
        case ENC_EVT_BTN_DOWN:
            s_btn_pressed = true;
            break;
        case ENC_EVT_BTN_UP:
        case ENC_EVT_BTN_CLICK:
            s_btn_pressed = false;
            break;
        default:
            break;
        }
    }

    // Report current physical state - LVGL handles click/long-press internally
    data->state = s_btn_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// ---------------------------------------------------------------------------
// LVGL tick callback - returns current time in milliseconds
// ---------------------------------------------------------------------------
static uint32_t tick_get_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// ---------------------------------------------------------------------------
// LVGL handler task
// ---------------------------------------------------------------------------
static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started");
    while (1) {
        if (xSemaphoreTake(s_lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            lv_timer_handler();
            xSemaphoreGive(s_lvgl_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(LVGL_TASK_PERIOD_MS));
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t lvgl_port_init(const lvgl_port_cfg_t *cfg)
{
    if (!cfg || !cfg->lcd) return ESP_ERR_INVALID_ARG;

    s_lcd = cfg->lcd;

    // Create LVGL mutex
    s_lvgl_mutex = xSemaphoreCreateMutex();
    if (!s_lvgl_mutex) {
        ESP_LOGE(TAG, "Failed to create LVGL mutex");
        return ESP_ERR_NO_MEM;
    }

    // Initialize LVGL
    lv_init();

    // Set tick source
    lv_tick_set_cb(tick_get_cb);

    // Create display
    s_disp = lv_display_create(ST7567_WIDTH, ST7567_HEIGHT);
    if (!s_disp) {
        ESP_LOGE(TAG, "Failed to create LVGL display");
        return ESP_FAIL;
    }

    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_I1);
    lv_display_set_buffers(s_disp, s_lv_buf1, s_lv_buf2,
                           sizeof(s_lv_buf1), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(s_disp, disp_flush_cb);

    ESP_LOGI(TAG, "LVGL display created (128x64 monochrome)");

    // Initialize encoder if configured
    if (cfg->enc) {
        encoder_init(cfg->enc);

        s_indev_encoder = lv_indev_create();
        if (s_indev_encoder) {
            lv_indev_set_type(s_indev_encoder, LV_INDEV_TYPE_ENCODER);
            lv_indev_set_read_cb(s_indev_encoder, encoder_read_cb);

            // Create default group and assign encoder to it
            lv_group_t *group = lv_group_create();
            lv_group_set_default(group);
            lv_indev_set_group(s_indev_encoder, group);

            ESP_LOGI(TAG, "LVGL encoder input created");
        }
    }

    // Start LVGL handler task pinned to core 1
    // (core 0 runs IDLE0 which feeds the task watchdog)
    BaseType_t ret = xTaskCreatePinnedToCore(lvgl_task, "lvgl", LVGL_TASK_STACK_SIZE,
                                              NULL, LVGL_TASK_PRIORITY, NULL, 1);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "LVGL port initialized");
    return ESP_OK;
}

bool lvgl_port_lock(uint32_t timeout_ms)
{
    if (!s_lvgl_mutex) return false;
    return xSemaphoreTake(s_lvgl_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void lvgl_port_unlock(void)
{
    if (s_lvgl_mutex) {
        xSemaphoreGive(s_lvgl_mutex);
    }
}
