#include "ble_commands.h"
#include "ble_uart.h"

#include "charge_mode.h"
#include "profile.h"
#include "scale.h"
#include "system_control.h"
#include "flow_model.h"
#include "lvgl_port.h"
#include "ui_screens.h"
#include "motors.h"
#include "neopixel_led.h"
#include "cleanup_mode.h"

#include "cJSON.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char *TAG = "BLE_CMD";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool json_get_bool(const cJSON *obj, const char *key, bool def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(v)) return cJSON_IsTrue(v);
    // also accept 1/0 as numbers
    if (cJSON_IsNumber(v)) return v->valuedouble != 0.0;
    return def;
}

static double json_get_double(const cJSON *obj, const char *key, double def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(v)) return v->valuedouble;
    return def;
}

static int json_get_int(const cJSON *obj, const char *key, int def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(v)) return (int)v->valuedouble;
    return def;
}

static bool json_has_key(const cJSON *obj, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(obj, key) != NULL;
}

static void send_json(cJSON *resp)
{
    char *str = cJSON_PrintUnformatted(resp);
    if (str) {
        // Append newline delimiter
        size_t len = strlen(str);
        char *buf = malloc(len + 2);
        if (buf) {
            memcpy(buf, str, len);
            buf[len]     = '\n';
            buf[len + 1] = '\0';
            ble_uart_send_str(buf);
            free(buf);
        }
        cJSON_free(str);
    }
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

static void handle_charge_mode_state(const cJSON *j)
{
    charge_mode_state_t_runtime rt = {0};
    charge_mode_get_runtime_state(&rt);

    bool has_s0 = json_has_key(j, "s0");
    bool has_s2 = json_has_key(j, "s2");

    if (has_s0) {
        float target = (float)json_get_double(j, "s0", rt.target_charge_weight);
        charge_mode_set_target_weight(target);
        rt.target_charge_weight = target;
    }

    if (has_s2) {
        int s2 = json_get_int(j, "s2", (int)rt.charge_mode_state);
        charge_mode_state_t new_state = (charge_mode_state_t)s2;
        charge_mode_set_state(new_state);
        rt.charge_mode_state = new_state;

        if (lvgl_port_lock(100)) {
            if (new_state == CHARGE_MODE_WAIT_FOR_ZERO) {
                ui_screens_enter_charge(rt.target_charge_weight);
            } else if (new_state == CHARGE_MODE_EXIT) {
                ui_screens_enter_main_menu();
            }
            lvgl_port_unlock();
        }
    }

    // Re-read updated state
    charge_mode_get_runtime_state(&rt);

    // Format current weight
    char weight_str[16];
    float live = scale_get_measurement();
    bool live_valid = scale_is_measurement_valid();
    if (live_valid && !isnanf(live)) {
        snprintf(weight_str, sizeof(weight_str), "%.3f", live);
    } else if (!isnanf(rt.current_weight)) {
        snprintf(weight_str, sizeof(weight_str), "%.3f", rt.current_weight);
    } else {
        snprintf(weight_str, sizeof(weight_str), "---");
    }

    char elapsed_str[16];
    snprintf(elapsed_str, sizeof(elapsed_str), "%.2f", rt.elapsed_time_seconds);

    char settled_w[16], settled_t[16];
    snprintf(settled_w, sizeof(settled_w), "%.3f", rt.settled_weight);
    snprintf(settled_t, sizeof(settled_t), "%.2f", rt.settled_time_seconds);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "cmd", "charge_mode_state");
    cJSON_AddNumberToObject(resp, "s0", rt.target_charge_weight);
    cJSON_AddStringToObject(resp, "s1", weight_str);
    cJSON_AddNumberToObject(resp, "s2", (int)rt.charge_mode_state);
    cJSON_AddNumberToObject(resp, "s3", (double)rt.charge_mode_event);
    cJSON_AddStringToObject(resp, "s4", rt.profile_name);
    cJSON_AddStringToObject(resp, "s5", elapsed_str);
    cJSON_AddStringToObject(resp, "s6", settled_w);
    cJSON_AddStringToObject(resp, "s7", settled_t);
    send_json(resp);
    cJSON_Delete(resp);
}

static void handle_profile_summary(const cJSON *j)
{
    // Handle profile selection
    if (json_has_key(j, "sel")) {
        int sel = json_get_int(j, "sel", -1);
        if (sel >= 0 && sel < MAX_PROFILE_CNT) {
            profile_select((uint8_t)sel);
            flow_model_load((uint8_t)sel);
        }
    }

    char names[MAX_PROFILE_CNT][PROFILE_NAME_MAX_LEN];
    uint8_t count = 0;
    profile_get_all_names(names, &count);
    uint16_t current_idx = profile_get_selected_idx();

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "cmd", "profile_summary");
    cJSON *s0 = cJSON_CreateObject();
    for (uint8_t i = 0; i < count; i++) {
        char key[4];
        snprintf(key, sizeof(key), "%d", i);
        cJSON_AddStringToObject(s0, key, names[i]);
    }
    cJSON_AddItemToObject(resp, "s0", s0);
    cJSON_AddNumberToObject(resp, "s1", current_idx);
    send_json(resp);
    cJSON_Delete(resp);
}

static void handle_profile_config(const cJSON *j)
{
    uint8_t profile_idx = (uint8_t)profile_get_selected_idx();

    if (json_has_key(j, "pf")) {
        int pf = json_get_int(j, "pf", profile_idx);
        if (pf >= 0 && pf < MAX_PROFILE_CNT) {
            profile_idx = (uint8_t)pf;
            profile_select(profile_idx);
            flow_model_load(profile_idx);
        }
    }

    profile_t *p = profile_get_by_idx(profile_idx);
    if (!p) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "cmd", "profile_config");
        cJSON_AddStringToObject(err, "error", "ProfileNotFound");
        send_json(err);
        cJSON_Delete(err);
        return;
    }

    bool config_changed = false;
    bool save_to_nvs = json_get_bool(j, "ee", false);

    if (json_has_key(j, "p2")) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, "p2");
        if (cJSON_IsString(v) && v->valuestring) {
            strncpy(p->name, v->valuestring, PROFILE_NAME_MAX_LEN - 1);
            p->name[PROFILE_NAME_MAX_LEN - 1] = '\0';
            config_changed = true;
        }
    }

#define SET_FLOAT(key, field) \
    if (json_has_key(j, key)) { p->field = (float)json_get_double(j, key, p->field); config_changed = true; }

    SET_FLOAT("p3",  coarse_kp)
    SET_FLOAT("p4",  coarse_ki)
    SET_FLOAT("p5",  coarse_kd)
    SET_FLOAT("p6",  coarse_min_flow_speed_rps)
    SET_FLOAT("p7",  coarse_max_flow_speed_rps)
    SET_FLOAT("p8",  fine_kp)
    SET_FLOAT("p9",  fine_ki)
    SET_FLOAT("p10", fine_kd)
    SET_FLOAT("p11", fine_min_flow_speed_rps)
    SET_FLOAT("p12", fine_max_flow_speed_rps)
#undef SET_FLOAT

    if (save_to_nvs && config_changed) {
        profile_save();
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "cmd", "profile_config");
    cJSON_AddNumberToObject(resp, "pf", profile_idx);
    cJSON_AddStringToObject(resp, "p2", p->name);
    cJSON_AddNumberToObject(resp, "p3",  p->coarse_kp);
    cJSON_AddNumberToObject(resp, "p4",  p->coarse_ki);
    cJSON_AddNumberToObject(resp, "p5",  p->coarse_kd);
    cJSON_AddNumberToObject(resp, "p6",  p->coarse_min_flow_speed_rps);
    cJSON_AddNumberToObject(resp, "p7",  p->coarse_max_flow_speed_rps);
    cJSON_AddNumberToObject(resp, "p8",  p->fine_kp);
    cJSON_AddNumberToObject(resp, "p9",  p->fine_ki);
    cJSON_AddNumberToObject(resp, "p10", p->fine_kd);
    cJSON_AddNumberToObject(resp, "p11", p->fine_min_flow_speed_rps);
    cJSON_AddNumberToObject(resp, "p12", p->fine_max_flow_speed_rps);
    send_json(resp);
    cJSON_Delete(resp);
}

static void handle_charge_mode_config(const cJSON *j)
{
    charge_mode_config_t config = {0};
    charge_mode_get_config(&config);

    bool config_changed = false;
    bool save_to_nvs = json_get_bool(j, "ee", false);

    if (json_has_key(j, "c1")) { config.neopixel_normal_charge_colour = (uint32_t)json_get_int(j, "c1", (int)config.neopixel_normal_charge_colour); config_changed = true; }
    if (json_has_key(j, "c2")) { config.neopixel_under_charge_colour  = (uint32_t)json_get_int(j, "c2", (int)config.neopixel_under_charge_colour);  config_changed = true; }
    if (json_has_key(j, "c3")) { config.neopixel_over_charge_colour   = (uint32_t)json_get_int(j, "c3", (int)config.neopixel_over_charge_colour);   config_changed = true; }
    if (json_has_key(j, "c4")) { config.neopixel_not_ready_colour     = (uint32_t)json_get_int(j, "c4", (int)config.neopixel_not_ready_colour);     config_changed = true; }
    if (json_has_key(j, "c5")) { config.coarse_stop_threshold    = (float)json_get_double(j, "c5", config.coarse_stop_threshold);   config_changed = true; }
    if (json_has_key(j, "c6")) { config.fine_stop_threshold      = (float)json_get_double(j, "c6", config.fine_stop_threshold);     config_changed = true; }
    if (json_has_key(j, "c7")) { config.set_point_sd_margin      = (float)json_get_double(j, "c7", config.set_point_sd_margin);     config_changed = true; }
    if (json_has_key(j, "c8")) { config.set_point_mean_margin    = (float)json_get_double(j, "c8", config.set_point_mean_margin);   config_changed = true; }
    if (json_has_key(j, "c9")) { config.decimal_places           = (decimal_places_t)json_get_int(j, "c9", (int)config.decimal_places); config_changed = true; }
    if (json_has_key(j, "c10")) { config.precharge_enable        = json_get_bool(j, "c10", config.precharge_enable);                config_changed = true; }
    if (json_has_key(j, "c11")) { config.precharge_time_ms       = (uint32_t)json_get_int(j, "c11", (int)config.precharge_time_ms); config_changed = true; }
    if (json_has_key(j, "c12")) { config.precharge_speed_rps     = (float)json_get_double(j, "c12", config.precharge_speed_rps);    config_changed = true; }
    if (json_has_key(j, "c13")) { config.fine_trickle_threshold  = (float)json_get_double(j, "c13", config.fine_trickle_threshold); config_changed = true; }
    if (json_has_key(j, "c14")) { config.result_tolerance        = (float)json_get_double(j, "c14", config.result_tolerance);        config_changed = true; }

    if (save_to_nvs && config_changed) {
        charge_mode_save_config(&config);
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "cmd", "charge_mode_config");
    cJSON_AddNumberToObject(resp, "c1", (double)config.neopixel_normal_charge_colour);
    cJSON_AddNumberToObject(resp, "c2", (double)config.neopixel_under_charge_colour);
    cJSON_AddNumberToObject(resp, "c3", (double)config.neopixel_over_charge_colour);
    cJSON_AddNumberToObject(resp, "c4", (double)config.neopixel_not_ready_colour);
    cJSON_AddNumberToObject(resp, "c5",  config.coarse_stop_threshold);
    cJSON_AddNumberToObject(resp, "c6",  config.fine_stop_threshold);
    cJSON_AddNumberToObject(resp, "c7",  config.set_point_sd_margin);
    cJSON_AddNumberToObject(resp, "c8",  config.set_point_mean_margin);
    cJSON_AddNumberToObject(resp, "c9",  (int)config.decimal_places);
    cJSON_AddBoolToObject  (resp, "c10", config.precharge_enable);
    cJSON_AddNumberToObject(resp, "c11", (double)config.precharge_time_ms);
    cJSON_AddNumberToObject(resp, "c12", config.precharge_speed_rps);
    cJSON_AddNumberToObject(resp, "c13", config.fine_trickle_threshold);
    cJSON_AddNumberToObject(resp, "c14", config.result_tolerance);
    send_json(resp);
    cJSON_Delete(resp);
}

static void handle_scale_config(const cJSON *j)
{
    scale_config_t config = {0};
    scale_get_config(&config);

    bool config_changed = false;
    bool save_to_nvs = json_get_bool(j, "ee", false);

    if (json_has_key(j, "s0")) {
        config.scale_driver = (scale_driver_t)json_get_int(j, "s0", (int)config.scale_driver);
        scale_set_driver(config.scale_driver);
        scale_get_config(&config); // driver may have changed baudrate
        config_changed = true;
    }
    if (json_has_key(j, "s1")) {
        config.scale_baudrate = (scale_baudrate_t)json_get_int(j, "s1", (int)config.scale_baudrate);
        config_changed = true;
    }

    if (save_to_nvs && config_changed) {
        scale_save_config(&config);
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "cmd", "scale_config");
    cJSON_AddNumberToObject(resp, "s0", (int)config.scale_driver);
    cJSON_AddNumberToObject(resp, "s1", (int)config.scale_baudrate);
    send_json(resp);
    cJSON_Delete(resp);
}

static void handle_scale_action(const cJSON *j)
{
    int action = json_get_int(j, "a0", SCALE_ACTION_NO_ACTION);
    scale_perform_action((scale_action_t)action);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "cmd", "scale_action");
    cJSON_AddNumberToObject(resp, "a0", action);
    send_json(resp);
    cJSON_Delete(resp);
}

static void handle_system_control(const cJSON *j)
{
    system_info_t info = {0};
    system_control_get_info(&info);

    bool do_reboot    = json_get_bool(j, "s5", false);
    bool do_erase_nvs = json_get_bool(j, "s6", false);

    if (do_erase_nvs) {
        // erase NVS then reboot
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "cmd", "system_control");
        cJSON_AddStringToObject(resp, "s0", info.unique_id);
        cJSON_AddStringToObject(resp, "s1", info.version_string);
        cJSON_AddBoolToObject(resp, "s6", true);
        send_json(resp);
        cJSON_Delete(resp);
        system_control_erase_nvs(true);
        return;
    }

    if (do_reboot) {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "cmd", "system_control");
        cJSON_AddStringToObject(resp, "s0", info.unique_id);
        cJSON_AddStringToObject(resp, "s1", info.version_string);
        cJSON_AddBoolToObject(resp, "s5", true);
        send_json(resp);
        cJSON_Delete(resp);
        system_control_reboot();
        return;
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "cmd", "system_control");
    cJSON_AddStringToObject(resp, "s0", info.unique_id);
    cJSON_AddStringToObject(resp, "s1", info.version_string);
    send_json(resp);
    cJSON_Delete(resp);
}

static void handle_motor_config(const cJSON *j)
{
    int mt_int = json_get_int(j, "mt", -1);
    if (mt_int < 0 || mt_int > 1) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "cmd", "motor_config");
        cJSON_AddStringToObject(err, "error", "missing mt (0=coarse,1=fine)");
        send_json(err);
        cJSON_Delete(err);
        return;
    }
    motor_type_t mt = (motor_type_t)mt_int;

    motor_config_t config = {0};
    motors_get_config(mt, &config);

    bool config_changed = false;
    bool save_to_nvs = json_get_bool(j, "ee", false);

    if (json_has_key(j, "m0")) { config.angular_acceleration      = (float)json_get_double(j, "m0", config.angular_acceleration);           config_changed = true; }
    if (json_has_key(j, "m1")) { config.full_steps_per_rotation   = (uint32_t)json_get_int(j, "m1", (int)config.full_steps_per_rotation);   config_changed = true; }
    if (json_has_key(j, "m2")) { config.current_ma                = (uint16_t)json_get_int(j, "m2", (int)config.current_ma);                config_changed = true; }
    if (json_has_key(j, "m3")) { config.microsteps                = (uint16_t)json_get_int(j, "m3", (int)config.microsteps);                config_changed = true; }
    if (json_has_key(j, "m4")) { config.max_speed_rps             = (uint16_t)json_get_int(j, "m4", (int)config.max_speed_rps);             config_changed = true; }
    if (json_has_key(j, "m5")) { config.r_sense                   = (uint16_t)json_get_int(j, "m5", (int)config.r_sense);                   config_changed = true; }
    if (json_has_key(j, "m6")) { config.min_speed_rps             = (float)json_get_double(j, "m6", config.min_speed_rps);                  config_changed = true; }
    if (json_has_key(j, "m7")) { config.gear_ratio                = (float)json_get_double(j, "m7", config.gear_ratio);                     config_changed = true; }
    if (json_has_key(j, "m8")) { config.inverted_enable           = json_get_bool(j, "m8", config.inverted_enable);                         config_changed = true; }
    if (json_has_key(j, "m9")) { config.inverted_direction        = json_get_bool(j, "m9", config.inverted_direction);                      config_changed = true; }

    if (save_to_nvs && config_changed) {
        motors_save_config(mt, &config);
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "cmd", "motor_config");
    cJSON_AddNumberToObject(resp, "mt", mt_int);
    cJSON_AddNumberToObject(resp, "m0", config.angular_acceleration);
    cJSON_AddNumberToObject(resp, "m1", (double)config.full_steps_per_rotation);
    cJSON_AddNumberToObject(resp, "m2", (double)config.current_ma);
    cJSON_AddNumberToObject(resp, "m3", (double)config.microsteps);
    cJSON_AddNumberToObject(resp, "m4", (double)config.max_speed_rps);
    cJSON_AddNumberToObject(resp, "m5", (double)config.r_sense);
    cJSON_AddNumberToObject(resp, "m6", config.min_speed_rps);
    cJSON_AddNumberToObject(resp, "m7", config.gear_ratio);
    cJSON_AddBoolToObject  (resp, "m8", config.inverted_enable);
    cJSON_AddBoolToObject  (resp, "m9", config.inverted_direction);
    send_json(resp);
    cJSON_Delete(resp);
}

static void handle_neopixel_config(const cJSON *j)
{
    neopixel_led_config_t config = {0};
    neopixel_led_get_config(&config);

    bool config_changed = false;
    bool save_to_nvs = json_get_bool(j, "ee", false);

    if (json_has_key(j, "bl")) { config.default_led_colours.mini12864_backlight_colour = (uint32_t)json_get_int(j, "bl", (int)config.default_led_colours.mini12864_backlight_colour); config_changed = true; }
    if (json_has_key(j, "l1")) { config.default_led_colours.led1_colour                = (uint32_t)json_get_int(j, "l1", (int)config.default_led_colours.led1_colour);                config_changed = true; }
    if (json_has_key(j, "l2")) { config.default_led_colours.led2_colour                = (uint32_t)json_get_int(j, "l2", (int)config.default_led_colours.led2_colour);                config_changed = true; }
    if (json_has_key(j, "l3")) { config.pwm_out_led_chain_count                        = (neopixel_led_chain_count_t)json_get_int(j, "l3", (int)config.pwm_out_led_chain_count);     config_changed = true; }
    if (json_has_key(j, "l4")) { config.pwm_out_led_is_rgbw                            = json_get_bool(j, "l4", config.pwm_out_led_is_rgbw);                                         config_changed = true; }
    if (json_has_key(j, "l5")) { config.pwm_out_led_colour_order                       = (neopixel_colour_order_t)json_get_int(j, "l5", (int)config.pwm_out_led_colour_order);       config_changed = true; }

    if (config_changed) {
        if (save_to_nvs) {
            neopixel_led_save_config(&config);
        }
        neopixel_led_set_colour(
            config.default_led_colours.mini12864_backlight_colour,
            config.default_led_colours.led1_colour,
            config.default_led_colours.led2_colour
        );
    }

    neopixel_led_get_config(&config);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "cmd", "neopixel_config");
    cJSON_AddNumberToObject(resp, "bl", (double)config.default_led_colours.mini12864_backlight_colour);
    cJSON_AddNumberToObject(resp, "l1", (double)config.default_led_colours.led1_colour);
    cJSON_AddNumberToObject(resp, "l2", (double)config.default_led_colours.led2_colour);
    cJSON_AddNumberToObject(resp, "l3", (double)config.pwm_out_led_chain_count);
    cJSON_AddBoolToObject  (resp, "l4", config.pwm_out_led_is_rgbw);
    cJSON_AddNumberToObject(resp, "l5", (double)config.pwm_out_led_colour_order);
    send_json(resp);
    cJSON_Delete(resp);
}

static void handle_cleanup_mode_state(const cJSON *j)
{
    cleanup_mode_runtime_state_t rt = {0};
    cleanup_mode_get_state(&rt);

    if (json_has_key(j, "s0")) {
        cleanup_mode_state_t new_state = (cleanup_mode_state_t)json_get_int(j, "s0", (int)rt.cleanup_mode_state);
        cleanup_mode_set_state(new_state);
        rt.cleanup_mode_state = new_state;
    }

    if (json_has_key(j, "s1")) {
        float speed = (float)json_get_double(j, "s1", rt.trickler_speed);
        cleanup_mode_set_speed(speed);
        rt.trickler_speed = speed;
    }

    cleanup_mode_get_state(&rt);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "cmd", "cleanup_mode_state");
    cJSON_AddNumberToObject(resp, "s0", (int)rt.cleanup_mode_state);
    cJSON_AddNumberToObject(resp, "s1", rt.trickler_speed);
    send_json(resp);
    cJSON_Delete(resp);
}

static void handle_display_config(const cJSON *j)
{
    mini_12864_config_t config = {0};
    lvgl_port_get_mini12864_config(&config);

    bool config_changed = false;
    bool save_to_nvs = json_get_bool(j, "ee", false);

    if (json_has_key(j, "b0")) { config.inverted_encoder  = json_get_bool(j, "b0", config.inverted_encoder);                  config_changed = true; }
    if (json_has_key(j, "b1")) { config.display_rotation  = (uint8_t)json_get_int(j, "b1", config.display_rotation);          config_changed = true; }

    if (config_changed) {
        lvgl_port_set_mini12864_config(&config, save_to_nvs);
    }

    // Re-read after apply (in case normalisation occurred)
    lvgl_port_get_mini12864_config(&config);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "cmd", "display_config");
    cJSON_AddBoolToObject  (resp, "b0", config.inverted_encoder);
    cJSON_AddNumberToObject(resp, "b1", config.display_rotation);
    send_json(resp);
    cJSON_Delete(resp);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ble_commands_handle(const uint8_t *data, size_t len)
{
    // Make a null-terminated copy
    char *buf = malloc(len + 1);
    if (!buf) {
        ESP_LOGE(TAG, "OOM");
        return;
    }
    memcpy(buf, data, len);
    buf[len] = '\0';

    // Strip trailing whitespace / \r
    for (int i = (int)len - 1; i >= 0 && (buf[i] == '\r' || buf[i] == '\n' || buf[i] == ' '); i--) {
        buf[i] = '\0';
    }

    ESP_LOGI(TAG, "RX: %s", buf);

    cJSON *j = cJSON_Parse(buf);
    free(buf);

    if (!j) {
        ESP_LOGW(TAG, "JSON parse error");
        return;
    }

    const cJSON *cmd_item = cJSON_GetObjectItemCaseSensitive(j, "cmd");
    if (!cJSON_IsString(cmd_item) || !cmd_item->valuestring) {
        ESP_LOGW(TAG, "Missing 'cmd' field");
        cJSON_Delete(j);
        return;
    }

    const char *cmd = cmd_item->valuestring;

    if      (strcmp(cmd, "charge_mode_state")  == 0) handle_charge_mode_state(j);
    else if (strcmp(cmd, "profile_summary")    == 0) handle_profile_summary(j);
    else if (strcmp(cmd, "profile_config")     == 0) handle_profile_config(j);
    else if (strcmp(cmd, "charge_mode_config") == 0) handle_charge_mode_config(j);
    else if (strcmp(cmd, "scale_config")       == 0) handle_scale_config(j);
    else if (strcmp(cmd, "scale_action")       == 0) handle_scale_action(j);
    else if (strcmp(cmd, "system_control")     == 0) handle_system_control(j);
    else if (strcmp(cmd, "motor_config")       == 0) handle_motor_config(j);
    else if (strcmp(cmd, "display_config")     == 0) handle_display_config(j);
    else if (strcmp(cmd, "neopixel_config")    == 0) handle_neopixel_config(j);
    else if (strcmp(cmd, "cleanup_mode_state") == 0) handle_cleanup_mode_state(j);
    else ESP_LOGW(TAG, "Unknown cmd: %s", cmd);

    cJSON_Delete(j);
}
