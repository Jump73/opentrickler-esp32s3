#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "board_expansion.h"

static const char *TAG = "OT_PORT";

void app_main(void)
{
    nvs_flash_init();
    board_expansion_safe_init();

    ESP_LOGI(TAG, "Bootstrap OK (board_expansion_safe_init)");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
