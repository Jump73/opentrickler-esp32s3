#include "board_expansion.h"
#include "driver/gpio.h"

#define M1_EN_GPIO   GPIO_NUM_6
#define M2_EN_GPIO   GPIO_NUM_9
#define M1_STEP_GPIO GPIO_NUM_3   // strap-safe: na starcie jako INPUT

void board_expansion_safe_init(void)
{
    gpio_config_t en = {
        .pin_bit_mask = (1ULL << M1_EN_GPIO) | (1ULL << M2_EN_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&en);

    // DISABLE (typowo TMC: EN low=enable)
    gpio_set_level(M1_EN_GPIO, 1);
    gpio_set_level(M2_EN_GPIO, 1);

    gpio_config_t step = {
        .pin_bit_mask = (1ULL << M1_STEP_GPIO),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&step);
}
