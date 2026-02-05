#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "OT_PORT";

// płyta rozszerzeń: mapowanie z Twojego schematu
#define M1_EN_GPIO   GPIO_NUM_6
#define M2_EN_GPIO   GPIO_NUM_9
#define M1_STEP_GPIO GPIO_NUM_3  // strap -> na starcie nietykane

static void early_safe_state(void)
{
    gpio_config_t en = {
        .pin_bit_mask = (1ULL<<M1_EN_GPIO) | (1ULL<<M2_EN_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&en);

    // DISABLE (typowo TMC: EN low=enable)
    gpio_set_level(M1_EN_GPIO, 1);
    gpio_set_level(M2_EN_GPIO, 1);

    // GPIO3 jako INPUT, bez pulli
    gpio_config_t step = {
        .pin_bit_mask = (1ULL<<M1_STEP_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&step);
}

void app_main(void)
{
    nvs_flash_init();
    early_safe_state();

    ESP_LOGI(TAG, "OpenTrickler ESP32-S3 port bootstrap OK (safe boot, EN disabled)");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
