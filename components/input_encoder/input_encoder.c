#include "input_encoder.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "ENC";

static QueueHandle_t s_q;
static encoder_cfg_t s_cfg;
static volatile int32_t s_pos = 0;

typedef struct {
    enc_event_t evt;
    int64_t t_us;
} enc_msg_t;

static volatile uint8_t s_last_ab = 0;
static volatile int64_t s_last_btn_us = 0;
static volatile int64_t s_last_rot_us = 0;
static volatile bool s_btn_state = false; // pressed?

static int gpio_read_fast(int pin) {
    return gpio_get_level((gpio_num_t)pin);
}

// Quadrature decode table (last<<2 | now) => delta
static const int8_t qdec_table[16] = {
    0, -1, +1, 0,
    +1, 0, 0, -1,
    -1, 0, 0, +1,
    0, +1, -1, 0
};

static void IRAM_ATTR isr_rot(void *arg)
{
    int64_t now = esp_timer_get_time();
    if (now - s_last_rot_us < 1000) return; // 1ms debounce
    s_last_rot_us = now;

    uint8_t a = (uint8_t)gpio_read_fast(s_cfg.gpio_a);
    uint8_t b = (uint8_t)gpio_read_fast(s_cfg.gpio_b);
    uint8_t ab = (a<<1) | b;

    uint8_t idx = (s_last_ab<<2) | ab;
    int8_t d = qdec_table[idx & 0x0F];
    s_last_ab = ab;

    if (d == 0) return;

    s_pos += d;

    enc_msg_t m = {
        .evt = (d > 0) ? ENC_EVT_CW : ENC_EVT_CCW,
        .t_us = now
    };
    BaseType_t hp = pdFALSE;
    xQueueSendFromISR(s_q, &m, &hp);
    if (hp) portYIELD_FROM_ISR();
}

static void IRAM_ATTR isr_btn(void *arg)
{
    int64_t now = esp_timer_get_time();
    if (now - s_last_btn_us < 20000) return; // 20ms debounce
    s_last_btn_us = now;

    int lvl = gpio_read_fast(s_cfg.gpio_btn);
    bool pressed = (lvl == 0); // pullup => 0 = pressed

    if (pressed == s_btn_state) return;
    s_btn_state = pressed;

    enc_msg_t m = {
        .evt = pressed ? ENC_EVT_BTN_DOWN : ENC_EVT_BTN_UP,
        .t_us = now
    };
    BaseType_t hp = pdFALSE;
    xQueueSendFromISR(s_q, &m, &hp);
    if (hp) portYIELD_FROM_ISR();
}

void encoder_init(const encoder_cfg_t *cfg)
{
    s_cfg = *cfg;
    s_q = xQueueCreate(32, sizeof(enc_msg_t));
    if (!s_q) {
        ESP_LOGE(TAG, "queue alloc failed");
        return;
    }

    gpio_config_t io = {0};
    io.mode = GPIO_MODE_INPUT;
    io.intr_type = GPIO_INTR_ANYEDGE;

    io.pin_bit_mask = (1ULL<<s_cfg.gpio_a) | (1ULL<<s_cfg.gpio_b);
    io.pull_up_en = s_cfg.pullups ? 1 : 0;
    io.pull_down_en = 0;
    ESP_ERROR_CHECK(gpio_config(&io));

    io.pin_bit_mask = (1ULL<<s_cfg.gpio_btn);
    io.pull_up_en = s_cfg.pullups ? 1 : 0;
    io.pull_down_en = 0;
    ESP_ERROR_CHECK(gpio_config(&io));

    uint8_t a = (uint8_t)gpio_get_level((gpio_num_t)s_cfg.gpio_a);
    uint8_t b = (uint8_t)gpio_get_level((gpio_num_t)s_cfg.gpio_b);
    s_last_ab = (a<<1) | b;

    s_btn_state = (gpio_get_level((gpio_num_t)s_cfg.gpio_btn) == 0);

    {
    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }
}
    ESP_ERROR_CHECK(gpio_isr_handler_add((gpio_num_t)s_cfg.gpio_a, isr_rot, NULL));
    ESP_ERROR_CHECK(gpio_isr_handler_add((gpio_num_t)s_cfg.gpio_b, isr_rot, NULL));
    ESP_ERROR_CHECK(gpio_isr_handler_add((gpio_num_t)s_cfg.gpio_btn, isr_btn, NULL));

    ESP_LOGI(TAG, "init OK A=%d B=%d BTN=%d", s_cfg.gpio_a, s_cfg.gpio_b, s_cfg.gpio_btn);
}

bool encoder_poll(enc_event_t *evt_out)
{
    if (!evt_out) return false;

    enc_msg_t m;
    if (xQueueReceive(s_q, &m, 0) != pdTRUE) return false;

    // klik = DOWN potem UP w rozsądnym czasie
    static int64_t last_down_us = 0;
    static bool down_seen = false;

    if (m.evt == ENC_EVT_BTN_DOWN) {
        down_seen = true;
        last_down_us = m.t_us;
        *evt_out = ENC_EVT_BTN_DOWN;
        return true;
    }

    if (m.evt == ENC_EVT_BTN_UP) {
        *evt_out = ENC_EVT_BTN_UP;
        if (down_seen && (m.t_us - last_down_us) < 500000) {
            // dodaj click jako osobny event (wrzucamy wirtualnie)
            // prosto: zwracamy CLICK zamiast UP
            *evt_out = ENC_EVT_BTN_CLICK;
        }
        down_seen = false;
        return true;
    }

    *evt_out = m.evt;
    return true;
}

int32_t encoder_get_position(void)
{
    return s_pos;
}

