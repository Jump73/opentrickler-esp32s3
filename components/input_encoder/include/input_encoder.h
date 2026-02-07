#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    ENC_EVT_NONE = 0,
    ENC_EVT_CW,
    ENC_EVT_CCW,
    ENC_EVT_BTN_DOWN,
    ENC_EVT_BTN_UP,
    ENC_EVT_BTN_CLICK,
} enc_event_t;

typedef struct {
    int gpio_a;
    int gpio_b;
    int gpio_btn;
    bool pullups;
    int32_t position;
} encoder_cfg_t;

void encoder_init(const encoder_cfg_t *cfg);
bool encoder_poll(enc_event_t *evt_out); // non-blocking, returns true if event available
int32_t encoder_get_position(void);
