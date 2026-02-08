#include "board_expansion.h"
#include "board_pins.h"
#include "driver/gpio.h"

// Use centralized pin definitions from board_pins.h
#define M1_EN_GPIO   COARSE_MOTOR_EN_PIN
#define M2_EN_GPIO   FINE_MOTOR_EN_PIN
#define M1_STEP_GPIO COARSE_MOTOR_STEP_PIN

void board_expansion_safe_init(void)
{
    // Pre-initialize Scale UART TX pin as HIGH (UART idle state)
    // This prevents a ~2s "break" signal to the scale during boot
    // before the UART peripheral is configured
    gpio_config_t scale_tx_cfg = {
        .pin_bit_mask = (1ULL << SCALE_UART_TX_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&scale_tx_cfg);
    gpio_set_level(SCALE_UART_TX_PIN, 1);  // UART idle = HIGH

    // Configure EN pins as outputs and set to DISABLED state
    gpio_config_t en_cfg = {
        .pin_bit_mask = (1ULL << M1_EN_GPIO) | (1ULL << M2_EN_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&en_cfg);

    // Disable motors (typical TMC: HIGH = disabled)
    gpio_set_level(M1_EN_GPIO, 1);
    gpio_set_level(M2_EN_GPIO, 1);

    // Configure GPIO3 as INPUT without pull to remain strap-safe
    gpio_config_t step_cfg = {
        .pin_bit_mask = (1ULL << M1_STEP_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&step_cfg);
}

void board_expansion_set_motors_enabled(int enabled)
{
    // Typical TMC logic: LOW = enable, HIGH = disable
    int level = enabled ? 0 : 1;
    gpio_set_level(M1_EN_GPIO, level);
    gpio_set_level(M2_EN_GPIO, level);
}
