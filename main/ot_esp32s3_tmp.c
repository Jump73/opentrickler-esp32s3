#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "board_expansion.h"
#include "motors_mcpwm.h"
#include "scale_generic.h"

void display_bus_test_start(void);
static const char *TAG = "OT_PORT";


static void display_bus_test_task(void *arg)
{
    printf("APP: display bus test TASK starting...\n");
    display_bus_test_start();
    vTaskDelete(NULL);
}
void app_main(void)
{
    // MINIMAL BOOT TEST - tylko printf, bez peryferiów
    printf("\n\n=== ESP32-S3 BOOT TEST ===\n");
    printf("APP: OpenTrickler ESP32-S3 Port\n");
    printf("APP: Chip: ESP32-S3\n");
    printf("APP: Minimal boot test OK!\n");

    // --- TEST ENKODERA: logowanie stanów pinów przez 20 sekund ---
    printf("\nAPP: ENCODER TEST - obracaj enkoderem i obserwuj zmiany\n");
    gpio_set_direction(ENCODER_A_PIN, GPIO_MODE_INPUT);
    gpio_set_direction(ENCODER_B_PIN, GPIO_MODE_INPUT);
    gpio_set_direction(ENCODER_BTN_PIN, GPIO_MODE_INPUT);
    for (int i = 0; i < 200; i++) {
        int a = gpio_get_level(ENCODER_A_PIN);
        int b = gpio_get_level(ENCODER_B_PIN);
        int btn = gpio_get_level(ENCODER_BTN_PIN);
        printf("ENCODER: A=%d, B=%d, BTN=%d\n", a, b, btn);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    printf("APP: ENCODER TEST END\n\n");

    printf("APP: Now starting display test...\n");
    xTaskCreate(display_bus_test_task, "disp_test", 8192, NULL, 5, NULL);
    return;
// 1) System init
    ESP_ERROR_CHECK(nvs_flash_init());

    // 2) Safe board state: motors disabled, keep strap pins safe at boot
    board_expansion_safe_init();
    ESP_LOGI(TAG, "Boot OK. Motors DISABLED (EN=1).");

    // 3) Initialize simulated scale
    ESP_LOGI("APP", "=== SCALE TEST START ===");
    scale_generic_init();
    scale_generic_tare();
    ESP_LOGI("APP", "=== SCALE INIT DONE ===");

    // 4) Guard delay to avoid touching STEP during boot (GPIO3 is a strap pin)
    vTaskDelay(pdMS_TO_TICKS(2000));

    // 5) Start MCPWM STEP test (STEP on GPIO3, strap-safe after delay)
    // DISABLED: motors_mcpwm_init(...);
    motors_mcpwm_start_test(100);
    ESP_LOGI(TAG, "STEP running @100Hz, still DISABLED (EN=1).");

    // 6) Simulate 'Start Dispense' after additional delay (enable motors)
    vTaskDelay(pdMS_TO_TICKS(3000));
    board_expansion_set_motors_enabled(1);
    ESP_LOGI(TAG, "Motors ENABLED (EN=0) - Start Dispense simulated.");

    // 7) Main loop: print simulated weight and increase it (simulate powder flow)
    while (1) {
        float w = scale_generic_get_grams();
        bool stable = scale_generic_is_stable();

        ESP_LOGI("APP", "Weight: %.3f g | Stable: %d", w, (int)stable);

        // Simulate powder flow at 0.15 g/s (simple model)
        scale_generic_feed_event(0.15f);

        vTaskDelay(pdMS_TO_TICKS(200));
    }
    printf("APP: starting display bus test...\\n");
    display_bus_test_start();

}
// static bool g_motors_disabled = true;
