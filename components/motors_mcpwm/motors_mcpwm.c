#include "motors_mcpwm.h"

#include "driver/gpio.h"
#include "driver/mcpwm_prelude.h"
#include "esp_log.h"

static const char *TAG = "MOT_MCPWM";

/*
 * Current test pin map:
 * - STEP on GPIO3 (strap pin): must be used only after a boot guard delay
 * - DIR  on GPIO7
 *
 * NOTE: This is a minimal STEP square-wave generator for bring-up tests.
 *       It is not a full motor control implementation yet.
 */
#define M2_STEP_GPIO   GPIO_NUM_3   // strap pin: only use after boot guard delay
#define M2_DIR_GPIO    GPIO_NUM_7

// MCPWM resources (IMPORTANT: correct handle types)
static mcpwm_timer_handle_t s_timer = NULL;
static mcpwm_oper_handle_t  s_oper  = NULL;
static mcpwm_cmpr_handle_t  s_cmpr  = NULL;
static mcpwm_gen_handle_t   s_gen   = NULL;

// 1 MHz resolution => period_ticks are in microseconds
static const uint32_t RESOLUTION_HZ = 1000000;

void motors_mcpwm_init(void)
{
    // DIR as plain GPIO output (fixed direction for test)
    gpio_config_t dir_cfg = {
        .pin_bit_mask = (1ULL << M2_DIR_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&dir_cfg));
    ESP_ERROR_CHECK(gpio_set_level(M2_DIR_GPIO, 0));

    // Create MCPWM timer
    mcpwm_timer_config_t tcfg = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = RESOLUTION_HZ,
        .period_ticks = 10000, // placeholder; overwritten in motors_mcpwm_start_test()
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    };
    ESP_ERROR_CHECK(mcpwm_new_timer(&tcfg, &s_timer));

    // Create operator and connect to timer
    mcpwm_operator_config_t ocfg = {
        .group_id = 0,
    };
    ESP_ERROR_CHECK(mcpwm_new_operator(&ocfg, &s_oper));
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(s_oper, s_timer));

    // Create comparator
    mcpwm_comparator_config_t ccfg = {
        .flags.update_cmp_on_tez = true, // update compare on timer = 0
    };
    ESP_ERROR_CHECK(mcpwm_new_comparator(s_oper, &ccfg, &s_cmpr));

    // Create generator on STEP pin
    mcpwm_generator_config_t gcfg = {
        .gen_gpio_num = (int)M2_STEP_GPIO,
    };
    ESP_ERROR_CHECK(mcpwm_new_generator(s_oper, &gcfg, &s_gen));

    // Generator actions:
    // - On timer empty (TEZ): set STEP HIGH
    // - On compare match:     set STEP LOW
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(
        s_gen,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,
                                     MCPWM_TIMER_EVENT_EMPTY,
                                     MCPWM_GEN_ACTION_HIGH)));

    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(
        s_gen,
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,
                                       s_cmpr,
                                       MCPWM_GEN_ACTION_LOW)));

    ESP_LOGI(TAG, "motors_mcpwm_init OK (STEP=GPIO%u, DIR=GPIO%u)",
             (unsigned)M2_STEP_GPIO, (unsigned)M2_DIR_GPIO);
}

void motors_mcpwm_start_test(uint32_t freq_hz)
{
    if (freq_hz == 0) {
        freq_hz = 1;
    }

    // period_ticks = resolution / freq
    uint32_t period_ticks = RESOLUTION_HZ / freq_hz;
    if (period_ticks < 2) {
        period_ticks = 2;
    }

    // 50% duty
    uint32_t cmp_ticks = period_ticks / 2;

    ESP_ERROR_CHECK(mcpwm_timer_set_period(s_timer, period_ticks));
    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(s_cmpr, cmp_ticks));

    ESP_ERROR_CHECK(mcpwm_timer_enable(s_timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(s_timer, MCPWM_TIMER_START_NO_STOP));

    ESP_LOGI(TAG, "STEP test START freq=%lu Hz (period_ticks=%lu, cmp=%lu)",
             (unsigned long)freq_hz,
             (unsigned long)period_ticks,
             (unsigned long)cmp_ticks);
}

void motors_mcpwm_stop(void)
{
    if (!s_timer) {
        return;
    }

    ESP_ERROR_CHECK(mcpwm_timer_start_stop(s_timer, MCPWM_TIMER_STOP_FULL));
    ESP_ERROR_CHECK(mcpwm_timer_disable(s_timer));

    // Safe state: STEP low
    ESP_ERROR_CHECK(gpio_set_level(M2_STEP_GPIO, 0));

    ESP_LOGI(TAG, "STEP test STOP");
}
