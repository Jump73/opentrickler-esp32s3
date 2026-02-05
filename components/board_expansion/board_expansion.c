#include "board_expansion.h"
#include "driver/gpio.h"

// --- Expansion board pin map ---
#define M1_EN_GPIO   GPIO_NUM_6
#define M2_EN_GPIO   GPIO_NUM_9
#define M1_STEP_GPIO GPIO_NUM_3   // strap-safe during boot
// --------------------------------

void board_expansion_safe_init(void)
{
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
