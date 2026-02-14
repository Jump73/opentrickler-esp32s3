#ifndef NEOPIXEL_LED_H_
#define NEOPIXEL_LED_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// RGB color macros
#define RGB_COLOUR_GREEN      0x00FF00ul
#define RGB_COLOUR_YELLOW     0xFFFF00ul
#define RGB_COLOUR_RED        0xFF0000ul
#define RGB_COLOUR_BLUE       0x0000FFul
#define RGB_COLOUR_WHITE      0xFFFFFFul
#define RGB_COLOUR_DULL_WHITE 0x0F0F0Ful

// LED chain count (1-16 LEDs)
typedef enum {
    NEOPIXEL_LED_CHAIN_COUNT_1 = 1,
    NEOPIXEL_LED_CHAIN_COUNT_2 = 2,
    NEOPIXEL_LED_CHAIN_COUNT_3 = 3,
    NEOPIXEL_LED_CHAIN_COUNT_4 = 4,
    NEOPIXEL_LED_CHAIN_COUNT_5 = 5,
    NEOPIXEL_LED_CHAIN_COUNT_6 = 6,
    NEOPIXEL_LED_CHAIN_COUNT_7 = 7,
    NEOPIXEL_LED_CHAIN_COUNT_8 = 8,
    NEOPIXEL_LED_CHAIN_COUNT_9 = 9,
    NEOPIXEL_LED_CHAIN_COUNT_10 = 10,
    NEOPIXEL_LED_CHAIN_COUNT_11 = 11,
    NEOPIXEL_LED_CHAIN_COUNT_12 = 12,
    NEOPIXEL_LED_CHAIN_COUNT_13 = 13,
    NEOPIXEL_LED_CHAIN_COUNT_14 = 14,
    NEOPIXEL_LED_CHAIN_COUNT_15 = 15,
    NEOPIXEL_LED_CHAIN_COUNT_16 = 16,
} neopixel_led_chain_count_t;

// Color order for PWM output
typedef enum {
    NEOPIXEL_COLOUR_ORDER_RGB = 0,
    NEOPIXEL_COLOUR_ORDER_GRB = 1,
} neopixel_colour_order_t;

// LED colors configuration
typedef struct {
    uint32_t led1_colour;
    uint32_t led2_colour;
    uint32_t mini12864_backlight_colour;
} neopixel_led_colours_t;

// NeoPixel configuration (stored in NVS)
typedef struct {
    uint16_t neopixel_data_rev;
    neopixel_led_colours_t default_led_colours;

    // PWM OUT LED configurations
    neopixel_led_chain_count_t pwm_out_led_chain_count;
    neopixel_colour_order_t pwm_out_led_colour_order;
    bool pwm_out_led_is_rgbw;  // true: RGBW, false: RGB
} neopixel_led_config_t;

// Number of NeoPixel LEDs on the MINI12864 V2.0 chain (RGB1 + RGB2 + backlight)
#define NEOPIXEL_BACKLIGHT_COUNT 3

// Initialize neopixel LED hardware on specified GPIO pin.
// Must be called AFTER LCD reset completes if sharing the same GPIO (time-multiplexed pin).
// Backlight LEDs are set to the saved NVS color immediately.
esp_err_t neopixel_led_init(int gpio_num);

// Initialize external PWM3 LED strip on a separate GPIO.
// Mirrors LED1 colour from the main strip. Chain count from NVS config.
// Must be called AFTER neopixel_led_init() so config is loaded.
esp_err_t neopixel_pwm3_init(int gpio_num);

// Configuration management (NVS)
esp_err_t neopixel_led_save_config(const neopixel_led_config_t *config);
esp_err_t neopixel_led_load_config(neopixel_led_config_t *config);
esp_err_t neopixel_led_get_config(neopixel_led_config_t *config);

// Set LED colors and refresh the LED strip
esp_err_t neopixel_led_set_colour(uint32_t mini12864_backlight, uint32_t led1, uint32_t led2);

#ifdef __cplusplus
}
#endif

#endif // NEOPIXEL_LED_H_
