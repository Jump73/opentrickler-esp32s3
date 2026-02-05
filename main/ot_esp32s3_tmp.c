#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "board_expansion.h"
#include "motors_mcpwm.h"

static const char *TAG = "OT_PORT";

void app_main(void)
{
    // 1) System init
    ESP_ERROR_CHECK(nvs_flash_init());

    // 2) Safe board state: motors disabled, GPIO3 strap-safe
    board_expansion_safe_init();
    ESP_LOGI(TAG, "Boot OK. Motors DISABLED (EN=1).");

    // 3) Guard delay to avoid touching STEP during boot
    vTaskDelay(pdMS_TO_TICKS(2000));

    // 4) Start MCPWM STEP test (currently on GPIO8)
    motors_mcpwm_init();
    motors_mcpwm_start_test(100);
    ESP_LOGI(TAG, "STEP running @100Hz, still DISABLED (EN=1).");

    // 5) Simulate 'Start Dispense' after additional delay
    vTaskDelay(pdMS_TO_TICKS(3000));
    board_expansion_set_motors_enabled(1);
    ESP_LOGI(TAG, "Motors ENABLED (EN=0) - Start Dispense simulated.");

    // 6) Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
