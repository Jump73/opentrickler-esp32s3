#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "GPIO_SCAN";

// GPIO do przetestowania (potencjalne LCD piny)
int test_gpios[] = {
    1,   // Powinno być SCK (pin 24)
    2,   // Powinno być MOSI (pin 25)
    4,   // Powinno być A0 (pin 26)
    38,  // Powinno być RST (pin 17)
    41,  // Powinno być CS (pin 22)
    // Dodatkowe do sprawdzenia:
    6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18
};

void app_main(void)
{
    ESP_LOGI(TAG, "=== GPIO SCAN TEST ===");
    ESP_LOGI(TAG, "Podłącz multimetr lub LED do pinów na gnieździe");
    ESP_LOGI(TAG, "Sprawdź który fizyczny pin mruga dla każdego GPIO");

    int num_gpios = sizeof(test_gpios) / sizeof(test_gpios[0]);

    // Konfiguruj wszystkie GPIO jako output
    for(int i = 0; i < num_gpios; i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << test_gpios[i]),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = 0,
            .pull_down_en = 0,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t err = gpio_config(&io_conf);
        if (err == ESP_OK) {
            gpio_set_level(test_gpios[i], 0);
            ESP_LOGI(TAG, "GPIO%d configured OK", test_gpios[i]);
        } else {
            ESP_LOGE(TAG, "GPIO%d config FAILED (error %d)", test_gpios[i], err);
        }
    }

    ESP_LOGI(TAG, "\n\n=== ROZPOCZYNAM SKANOWANIE ===\n");
    vTaskDelay(pdMS_TO_TICKS(2000));

    while(1) {
        for(int i = 0; i < num_gpios; i++) {
            ESP_LOGI(TAG, "\n>>> Testuję GPIO%d <<<", test_gpios[i]);
            ESP_LOGI(TAG, "    (sprawdź który pin na gnieździe mruga!)");

            // Mrugaj 5 razy
            for(int blink = 0; blink < 5; blink++) {
                gpio_set_level(test_gpios[i], 1);
                ESP_LOGI(TAG, "    GPIO%d = HIGH", test_gpios[i]);
                vTaskDelay(pdMS_TO_TICKS(500));

                gpio_set_level(test_gpios[i], 0);
                ESP_LOGI(TAG, "    GPIO%d = LOW", test_gpios[i]);
                vTaskDelay(pdMS_TO_TICKS(500));
            }

            ESP_LOGI(TAG, ">>> GPIO%d test zakończony <<<\n", test_gpios[i]);
            vTaskDelay(pdMS_TO_TICKS(2000)); // Pauza przed następnym
        }

        ESP_LOGI(TAG, "\n\n=== CYKL ZAKOŃCZONY - POWTARZAM ===\n\n");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
