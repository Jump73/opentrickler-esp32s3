#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "GPIO_TEST";

void app_main(void)
{
    ESP_LOGI(TAG, "=== GPIO TEST ===");

    // Test GPIO10-14 (nasze nowe LCD piny)
    int test_gpios[] = {10, 11, 12, 13, 14};

    for(int i=0; i<5; i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << test_gpios[i]),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = 0,
            .pull_down_en = 0,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        ESP_LOGI(TAG, "GPIO%d configured as output", test_gpios[i]);
    }

    // Mrugaj wszystkimi pinami
    while(1) {
        for(int i=0; i<5; i++) {
            gpio_set_level(test_gpios[i], 1);
            ESP_LOGI(TAG, "GPIO%d = HIGH", test_gpios[i]);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));

        for(int i=0; i<5; i++) {
            gpio_set_level(test_gpios[i], 0);
            ESP_LOGI(TAG, "GPIO%d = LOW", test_gpios[i]);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
