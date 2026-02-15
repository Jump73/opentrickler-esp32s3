#include "ui_screens.h"
#include "lvgl.h"
#include "charge_mode.h"
#include "cleanup_mode.h"
#include "scale.h"
#include "profile.h"
#include "lvgl_port.h"
#include "wifi_manager.h"
#include "system_control.h"
#include "input_encoder.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI";

// ---------------------------------------------------------------------------
// Screen identifiers
// ---------------------------------------------------------------------------
typedef enum {
    SCR_MAIN_MENU,
    SCR_PROFILE_SELECT,
    SCR_WEIGHT_INPUT,
    SCR_CHARGE_MODE,
    SCR_CLEANUP_MODE,
    SCR_WIRELESS_INFO,
    SCR_SETTINGS,
    SCR_SCALE_SETTINGS,
    SCR_SCALE_DRIVER,
    SCR_SCALE_BAUDRATE,
    SCR_PROFILE_VIEW,
    SCR_VERSION_INFO,
} screen_id_t;

static screen_id_t s_active_screen = SCR_MAIN_MENU;

// LVGL encoder group
static lv_group_t *s_group = NULL;

// Bitmap fonts
LV_FONT_DECLARE(lv_font_unscii_8);
LV_FONT_DECLARE(lv_font_unscii_16);

// ---------------------------------------------------------------------------
// Lookup tables
// ---------------------------------------------------------------------------
static const char *scale_driver_names[] = {
    "A&D FX-i", "Steinberg SBS", "G&G JJB", "US Solid JFDBS",
    "JM Science", "Creedmoor", "Radwag PS R2", "Sartorius", "Generic",
};
#define NUM_SCALE_DRIVERS (sizeof(scale_driver_names)/sizeof(scale_driver_names[0]))

static const char *scale_baudrate_names[] = { "4800", "9600", "19200" };
#define NUM_SCALE_BAUDRATES (sizeof(scale_baudrate_names)/sizeof(scale_baudrate_names[0]))

// ---------------------------------------------------------------------------
// Screen objects
// ---------------------------------------------------------------------------
static lv_obj_t *s_scr_main_menu = NULL;

static lv_obj_t *s_scr_profile_select = NULL;

static lv_obj_t *s_scr_weight_input = NULL;
static float s_target_weight = 41.50f;

// Per-digit weight editor
#define MAX_WEIGHT_DIGITS 5
static lv_obj_t *s_digit_btns[MAX_WEIGHT_DIGITS] = {0};
static int s_wd[MAX_WEIGHT_DIGITS] = {4, 1, 5, 0, 0}; // tens, ones, tenths, hundredths, thousandths
static int s_weight_num_frac = 2; // 2 or 3 decimal places

static lv_obj_t *s_scr_charge = NULL;
static lv_obj_t *s_lbl_charge_title = NULL;
static lv_obj_t *s_lbl_charge_timer = NULL;
static lv_obj_t *s_lbl_charge_target = NULL;
static lv_obj_t *s_lbl_charge_weight = NULL;
static lv_obj_t *s_lbl_charge_result = NULL;
static lv_obj_t *s_lbl_charge_profile = NULL;

static lv_obj_t *s_scr_cleanup = NULL;
static lv_obj_t *s_lbl_cleanup_speed = NULL;
static lv_obj_t *s_lbl_cleanup_weight = NULL;
static int32_t s_cleanup_last_enc_pos = 0;
static float s_cleanup_speed = 0.0f;

static lv_obj_t *s_scr_wireless = NULL;
static lv_obj_t *s_lbl_wifi_status = NULL;
static lv_obj_t *s_lbl_wifi_ip = NULL;

static lv_obj_t *s_scr_settings = NULL;

static lv_obj_t *s_scr_scale_settings = NULL;
static lv_obj_t *s_lbl_scale_driver = NULL;
static lv_obj_t *s_lbl_scale_baudrate = NULL;

static lv_obj_t *s_scr_scale_driver = NULL;
static lv_obj_t *s_scr_scale_baudrate = NULL;

static lv_obj_t *s_scr_profile_view = NULL;
static lv_obj_t *s_lbl_profile_info = NULL;

static lv_obj_t *s_scr_version = NULL;
static lv_obj_t *s_lbl_ver_text = NULL;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void switch_to_screen(screen_id_t id, lv_obj_t *scr)
{
    lv_group_remove_all_objs(s_group);
    s_active_screen = id;
    lv_screen_load(scr);
}

static void group_add_children(lv_obj_t *parent)
{
    uint32_t cnt = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(parent, (int32_t)i);
        if (lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE)) {
            lv_group_add_obj(s_group, child);
        }
    }
}

static lv_obj_t *create_menu_btn(lv_obj_t *parent, const char *text,
                                  lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_set_style_pad_all(btn, 2, 0);
    lv_obj_set_style_min_height(btn, 10, 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, lv_color_black(), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn, lv_color_white(), LV_STATE_FOCUSED);

    lv_obj_t *label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, &lv_font_unscii_8, 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    return btn;
}

static lv_obj_t *create_menu_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_pad_row(scr, 1, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    return scr;
}

static lv_obj_t *create_inline_btn(lv_obj_t *parent, const char *text,
                                    int w, int h, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 1, 0);
    lv_obj_set_style_bg_color(btn, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, lv_color_black(), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn, lv_color_white(), LV_STATE_FOCUSED);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &lv_font_unscii_8, 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

static const char *charge_state_name(charge_mode_state_t state)
{
    switch (state) {
    case CHARGE_MODE_EXIT:                  return "Idle";
    case CHARGE_MODE_WAIT_FOR_ZERO:         return "Wait Zero";
    case CHARGE_MODE_WAIT_FOR_COMPLETE:     return "Dispensing";
    case CHARGE_MODE_WAIT_FOR_CUP_REMOVAL:  return "Remove Cup";
    case CHARGE_MODE_WAIT_FOR_CUP_RETURN:   return "Return Cup";
    default:                                return "Unknown";
    }
}

// Forward declarations for dynamic screens
static void rebuild_profile_select(void);
static void rebuild_weight_input(void);
static void rebuild_scale_driver_screen(void);
static void rebuild_scale_baudrate_screen(void);
static void rebuild_profile_view(uint8_t idx);

// ---------------------------------------------------------------------------
// Weight digit helpers
// ---------------------------------------------------------------------------
static void weight_to_digits(float w)
{
    if (s_weight_num_frac == 3) {
        int val = (int)(w * 1000.0f + 0.5f);
        if (val < 0) val = 0;
        if (val > 99999) val = 99999;
        s_wd[0] = (val / 10000) % 10;
        s_wd[1] = (val / 1000) % 10;
        s_wd[2] = (val / 100) % 10;
        s_wd[3] = (val / 10) % 10;
        s_wd[4] = val % 10;
    } else {
        int val = (int)(w * 100.0f + 0.5f);
        if (val < 0) val = 0;
        if (val > 9999) val = 9999;
        s_wd[0] = (val / 1000) % 10;
        s_wd[1] = (val / 100) % 10;
        s_wd[2] = (val / 10) % 10;
        s_wd[3] = val % 10;
        s_wd[4] = 0;
    }
}

static float digits_to_weight(void)
{
    float w = s_wd[0] * 10.0f + (float)s_wd[1];
    w += s_wd[2] * 0.1f + s_wd[3] * 0.01f;
    if (s_weight_num_frac >= 3) w += s_wd[4] * 0.001f;
    return w;
}

// =========================================================================
// EVENT CALLBACKS
// =========================================================================

static void evt_goto_main(lv_event_t *e)
{
    (void)e;
    switch_to_screen(SCR_MAIN_MENU, s_scr_main_menu);
    group_add_children(s_scr_main_menu);
}

static void evt_goto_settings(lv_event_t *e)
{
    (void)e;
    switch_to_screen(SCR_SETTINGS, s_scr_settings);
    group_add_children(s_scr_settings);
}

// --- Main menu ---
static void evt_start_charge_flow(lv_event_t *e)
{
    (void)e;
    rebuild_profile_select();
    switch_to_screen(SCR_PROFILE_SELECT, s_scr_profile_select);
    group_add_children(s_scr_profile_select);
    ESP_LOGI(TAG, "-> Profile Select");
}

static void evt_enter_cleanup(lv_event_t *e)
{
    (void)e;
    cleanup_mode_set_state(CLEANUP_MODE_ENTER);
    s_cleanup_speed = 0.0f;
    cleanup_mode_set_speed(0.0f);
    s_cleanup_last_enc_pos = encoder_get_position();
    switch_to_screen(SCR_CLEANUP_MODE, s_scr_cleanup);
    group_add_children(s_scr_cleanup);
    ESP_LOGI(TAG, "-> Cleanup Mode");
}

static void evt_goto_wireless(lv_event_t *e)
{
    (void)e;
    switch_to_screen(SCR_WIRELESS_INFO, s_scr_wireless);
    group_add_children(s_scr_wireless);
}

// --- Profile select ---
static void evt_select_profile(lv_event_t *e)
{
    uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    profile_select(idx);

    charge_mode_state_t_runtime rt;
    if (charge_mode_get_runtime_state(&rt) == ESP_OK && rt.target_charge_weight > 0.01f) {
        s_target_weight = rt.target_charge_weight;
    }

    // Determine decimal places from config
    charge_mode_config_t cfg;
    s_weight_num_frac = 2;
    if (charge_mode_get_config(&cfg) == ESP_OK && cfg.decimal_places == DP_3) {
        s_weight_num_frac = 3;
    }

    // Convert float weight to individual digits
    weight_to_digits(s_target_weight);

    // Rebuild the weight input screen with digit buttons
    rebuild_weight_input();

    switch_to_screen(SCR_WEIGHT_INPUT, s_scr_weight_input);
    group_add_children(s_scr_weight_input);
    ESP_LOGI(TAG, "Profile %d selected -> Weight Input", idx);
}

// --- Weight input: per-digit click handler ---
// Matches original OpenTrickler behavior:
//   Encoder rotation = navigate between digits (standard group navigation)
//   Encoder click    = increment current digit by 1 (wraps 9 -> 0)
static void evt_digit_click(lv_event_t *e)
{
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= MAX_WEIGHT_DIGITS) return;

    s_wd[idx] = (s_wd[idx] + 1) % 10;

    // Update the digit label
    char d[2] = { '0' + s_wd[idx], '\0' };
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (lbl) lv_label_set_text(lbl, d);

    // Keep float in sync
    s_target_weight = digits_to_weight();
}

static void evt_start_charging(lv_event_t *e)
{
    (void)e;
    s_target_weight = digits_to_weight();
    charge_mode_set_target_weight(s_target_weight);
    charge_mode_set_state(CHARGE_MODE_WAIT_FOR_ZERO);

    // Show target weight on charge screen
    static char tgt_buf[24];
    charge_mode_config_t cfg;
    if (charge_mode_get_config(&cfg) == ESP_OK && cfg.decimal_places == DP_3) {
        snprintf(tgt_buf, sizeof(tgt_buf), "Target: %.3f", s_target_weight);
    } else {
        snprintf(tgt_buf, sizeof(tgt_buf), "Target: %.2f", s_target_weight);
    }
    lv_label_set_text(s_lbl_charge_target, tgt_buf);

    switch_to_screen(SCR_CHARGE_MODE, s_scr_charge);
    group_add_children(s_scr_charge);
    ESP_LOGI(TAG, "-> Charge Mode (target=%.2f)", s_target_weight);
}

static void evt_back_to_profiles(lv_event_t *e)
{
    (void)e;
    rebuild_profile_select();
    switch_to_screen(SCR_PROFILE_SELECT, s_scr_profile_select);
    group_add_children(s_scr_profile_select);
}

// --- Charge mode ---
static void evt_exit_charge(lv_event_t *e)
{
    (void)e;
    charge_mode_set_state(CHARGE_MODE_EXIT);
    switch_to_screen(SCR_MAIN_MENU, s_scr_main_menu);
    group_add_children(s_scr_main_menu);
    ESP_LOGI(TAG, "Charge exited -> Main");
}

void ui_screens_enter_charge(float target_weight)
{
    if (!s_scr_charge) return;

    // Update target label (same logic as evt_start_charging)
    static char tgt_buf[24];
    charge_mode_config_t cfg;
    if (charge_mode_get_config(&cfg) == ESP_OK && cfg.decimal_places == DP_3) {
        snprintf(tgt_buf, sizeof(tgt_buf), "Target: %.3f", target_weight);
    } else {
        snprintf(tgt_buf, sizeof(tgt_buf), "Target: %.2f", target_weight);
    }
    lv_label_set_text(s_lbl_charge_target, tgt_buf);
    lv_label_set_text(s_lbl_charge_result, "");

    switch_to_screen(SCR_CHARGE_MODE, s_scr_charge);
    group_add_children(s_scr_charge);
    ESP_LOGI(TAG, "REST -> Charge Mode screen (target=%.2f)", target_weight);
}

void ui_screens_enter_main_menu(void)
{
    if (!s_scr_main_menu) return;
    switch_to_screen(SCR_MAIN_MENU, s_scr_main_menu);
    group_add_children(s_scr_main_menu);
    ESP_LOGI(TAG, "REST -> Main Menu screen");
}

// --- Cleanup mode ---
static void evt_stop_cleanup(lv_event_t *e)
{
    (void)e;
    cleanup_mode_set_speed(0.0f);
    cleanup_mode_set_state(CLEANUP_MODE_EXIT);
    switch_to_screen(SCR_MAIN_MENU, s_scr_main_menu);
    group_add_children(s_scr_main_menu);
    ESP_LOGI(TAG, "Cleanup stopped -> Main");
}

// --- Settings ---
static void evt_goto_scale_settings(lv_event_t *e)
{
    (void)e;
    scale_config_t scfg;
    if (scale_get_config(&scfg) == ESP_OK) {
        static char drv_buf[32], baud_buf[24];
        snprintf(drv_buf, sizeof(drv_buf), "Drv: %s",
                 (scfg.scale_driver < NUM_SCALE_DRIVERS) ? scale_driver_names[scfg.scale_driver] : "?");
        snprintf(baud_buf, sizeof(baud_buf), "Baud: %s",
                 (scfg.scale_baudrate < NUM_SCALE_BAUDRATES) ? scale_baudrate_names[scfg.scale_baudrate] : "?");
        lv_label_set_text(s_lbl_scale_driver, drv_buf);
        lv_label_set_text(s_lbl_scale_baudrate, baud_buf);
    }
    switch_to_screen(SCR_SCALE_SETTINGS, s_scr_scale_settings);
    group_add_children(s_scr_scale_settings);
}

static void evt_goto_scale_driver(lv_event_t *e)
{
    (void)e;
    rebuild_scale_driver_screen();
    switch_to_screen(SCR_SCALE_DRIVER, s_scr_scale_driver);
    group_add_children(s_scr_scale_driver);
}

static void evt_goto_scale_baudrate(lv_event_t *e)
{
    (void)e;
    rebuild_scale_baudrate_screen();
    switch_to_screen(SCR_SCALE_BAUDRATE, s_scr_scale_baudrate);
    group_add_children(s_scr_scale_baudrate);
}

static void evt_select_driver(lv_event_t *e)
{
    scale_driver_t drv = (scale_driver_t)(uintptr_t)lv_event_get_user_data(e);
    scale_config_t scfg;
    if (scale_get_config(&scfg) == ESP_OK) {
        scfg.scale_driver = drv;
        scale_save_config(&scfg);
    }
    ESP_LOGI(TAG, "Scale driver -> %d", drv);
    evt_goto_scale_settings(e);
}

static void evt_select_baudrate(lv_event_t *e)
{
    scale_baudrate_t baud = (scale_baudrate_t)(uintptr_t)lv_event_get_user_data(e);
    scale_config_t scfg;
    if (scale_get_config(&scfg) == ESP_OK) {
        scfg.scale_baudrate = baud;
        scale_save_config(&scfg);
    }
    ESP_LOGI(TAG, "Scale baudrate -> %d", baud);
    evt_goto_scale_settings(e);
}

static void evt_goto_profile_manager(lv_event_t *e)
{
    (void)e;
    rebuild_profile_view(profile_get_selected_idx());
    switch_to_screen(SCR_PROFILE_VIEW, s_scr_profile_view);
    group_add_children(s_scr_profile_view);
}

static void evt_save_nvs(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Saving NVS...");
    system_control_save_all_nvs();
}

static void evt_erase_nvs(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Erasing NVS (reboot)...");
    system_control_erase_nvs(true);
}

static void evt_reboot(lv_event_t *e)
{
    (void)e;
    system_control_reboot();
}

static void evt_version_info(lv_event_t *e)
{
    (void)e;
    system_info_t info;
    if (system_control_get_info(&info) == ESP_OK) {
        static char ver_buf[128];
        snprintf(ver_buf, sizeof(ver_buf), "FW: %s\nGit: %s\nBuild: %s\nID: %s",
                 info.version_string, info.vcs_hash, info.build_type, info.unique_id);
        lv_label_set_text(s_lbl_ver_text, ver_buf);
    }
    switch_to_screen(SCR_VERSION_INFO, s_scr_version);
    group_add_children(s_scr_version);
}

// =========================================================================
// SCREEN CREATION
// =========================================================================

static void create_main_menu(void)
{
    s_scr_main_menu = create_menu_screen();
    create_menu_btn(s_scr_main_menu, "Start",    evt_start_charge_flow, NULL);
    create_menu_btn(s_scr_main_menu, "Cleanup",  evt_enter_cleanup,     NULL);
    create_menu_btn(s_scr_main_menu, "Wireless", evt_goto_wireless,     NULL);
    create_menu_btn(s_scr_main_menu, "Settings", evt_goto_settings,     NULL);
}

static void rebuild_profile_select(void)
{
    if (s_scr_profile_select) lv_obj_delete(s_scr_profile_select);
    s_scr_profile_select = create_menu_screen();

    char names[MAX_PROFILE_CNT][PROFILE_NAME_MAX_LEN];
    uint8_t count = 0;
    profile_get_all_names(names, &count);

    for (uint8_t i = 0; i < count; i++) {
        if (names[i][0] != '\0') {
            create_menu_btn(s_scr_profile_select, names[i],
                           evt_select_profile, (void*)(uintptr_t)i);
        }
    }
    create_menu_btn(s_scr_profile_select, "< Back", evt_goto_main, NULL);
}

// Rebuild weight input screen with per-digit editable buttons.
// Each digit is an LVGL editable button:
//   - Rotate encoder to navigate between digits / Start / Back
//   - Click on a digit to enter edit mode
//   - In edit mode: rotate to change digit value (0-9 wrapping)
//   - Click again to exit edit mode and navigate to next digit
static void rebuild_weight_input(void)
{
    if (s_scr_weight_input) lv_obj_delete(s_scr_weight_input);
    memset(s_digit_btns, 0, sizeof(s_digit_btns));

    s_scr_weight_input = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_weight_input, lv_color_white(), 0);
    lv_obj_set_style_pad_all(s_scr_weight_input, 0, 0);
    lv_obj_set_style_border_width(s_scr_weight_input, 0, 0);

    int num_digits = 2 + s_weight_num_frac;  // 4 or 5 total

    // Title
    lv_obj_t *title = lv_label_create(s_scr_weight_input);
    lv_obj_set_style_text_font(title, &lv_font_unscii_8, 0);
    lv_label_set_text(title, "Set Weight:");
    lv_obj_set_pos(title, 2, 2);

    // Digit buttons layout: [D0][D1].[D2][D3]  or  [D0][D1].[D2][D3][D4]
    int btn_w = 16, btn_h = 22;
    int dot_w = 8;
    int gr_w = 18;
    int total_w = num_digits * btn_w + dot_w + gr_w;
    int start_x = (128 - total_w) / 2;
    int y = 14;
    int x = start_x;

    for (int i = 0; i < num_digits; i++) {
        // Insert decimal point after the 2 integer digits
        if (i == 2) {
            lv_obj_t *dot = lv_label_create(s_scr_weight_input);
            lv_obj_set_style_text_font(dot, &lv_font_unscii_16, 0);
            lv_label_set_text(dot, ".");
            lv_obj_set_pos(dot, x, y + 3);
            x += dot_w;
        }

        lv_obj_t *btn = lv_button_create(s_scr_weight_input);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_pos(btn, x, y);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);

        // Default: black text on white, thin border
        lv_obj_set_style_bg_color(btn, lv_color_white(), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(btn, lv_color_black(), LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(btn, 1, LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(btn, lv_color_black(), LV_STATE_DEFAULT);

        // Focused: inverted (white on black)
        lv_obj_set_style_bg_color(btn, lv_color_black(), LV_STATE_FOCUSED);
        lv_obj_set_style_text_color(btn, lv_color_white(), LV_STATE_FOCUSED);
        lv_obj_set_style_border_width(btn, 0, LV_STATE_FOCUSED);

        // Digit label
        char d[2] = { '0' + s_wd[i], '\0' };
        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_unscii_16, 0);
        lv_label_set_text(lbl, d);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, evt_digit_click, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
        s_digit_btns[i] = btn;
        x += btn_w;
    }

    // "gr" unit label
    lv_obj_t *unit = lv_label_create(s_scr_weight_input);
    lv_obj_set_style_text_font(unit, &lv_font_unscii_8, 0);
    lv_label_set_text(unit, "gr");
    lv_obj_set_pos(unit, x + 4, y + 7);

    // Hint
    lv_obj_t *hint = lv_label_create(s_scr_weight_input);
    lv_obj_set_style_text_font(hint, &lv_font_unscii_8, 0);
    lv_label_set_text(hint, "Turn:Move Click:+1");
    lv_obj_set_pos(hint, 2, 40);

    // Start and Back buttons
    lv_obj_t *bs = create_inline_btn(s_scr_weight_input, "Start", 56, 12, evt_start_charging);
    lv_obj_align(bs, LV_ALIGN_BOTTOM_LEFT, 2, -2);

    lv_obj_t *bb = create_inline_btn(s_scr_weight_input, "< Back", 56, 12, evt_back_to_profiles);
    lv_obj_align(bb, LV_ALIGN_BOTTOM_RIGHT, -2, -2);
}

static void create_charge_screen(void)
{
    s_scr_charge = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_charge, lv_color_white(), 0);
    lv_obj_set_style_pad_all(s_scr_charge, 2, 0);
    lv_obj_set_style_border_width(s_scr_charge, 0, 0);

    s_lbl_charge_title = lv_label_create(s_scr_charge);
    lv_obj_set_style_text_font(s_lbl_charge_title, &lv_font_unscii_8, 0);
    lv_label_set_text(s_lbl_charge_title, "Wait Zero");
    lv_obj_align(s_lbl_charge_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_lbl_charge_timer = lv_label_create(s_scr_charge);
    lv_obj_set_style_text_font(s_lbl_charge_timer, &lv_font_unscii_8, 0);
    lv_label_set_text(s_lbl_charge_timer, "");
    lv_obj_align(s_lbl_charge_timer, LV_ALIGN_TOP_RIGHT, 0, 0);

    // Target weight (set when entering charge mode)
    s_lbl_charge_target = lv_label_create(s_scr_charge);
    lv_obj_set_style_text_font(s_lbl_charge_target, &lv_font_unscii_8, 0);
    lv_label_set_text(s_lbl_charge_target, "");
    lv_obj_align(s_lbl_charge_target, LV_ALIGN_TOP_LEFT, 0, 12);

    // Current weight (large, centered)
    s_lbl_charge_weight = lv_label_create(s_scr_charge);
    lv_obj_set_style_text_font(s_lbl_charge_weight, &lv_font_unscii_16, 0);
    lv_label_set_text(s_lbl_charge_weight, "---");
    lv_obj_align(s_lbl_charge_weight, LV_ALIGN_CENTER, 0, -2);

    // Over/under charge result (shown only in WAIT_FOR_CUP_REMOVAL)
    s_lbl_charge_result = lv_label_create(s_scr_charge);
    lv_obj_set_style_text_font(s_lbl_charge_result, &lv_font_unscii_8, 0);
    lv_label_set_text(s_lbl_charge_result, "");
    lv_obj_align(s_lbl_charge_result, LV_ALIGN_CENTER, 0, 11);

    s_lbl_charge_profile = lv_label_create(s_scr_charge);
    lv_obj_set_style_text_font(s_lbl_charge_profile, &lv_font_unscii_8, 0);
    lv_label_set_text(s_lbl_charge_profile, "");
    lv_obj_align(s_lbl_charge_profile, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    lv_obj_t *b = create_inline_btn(s_scr_charge, "< Back", 48, 12, evt_exit_charge);
    lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void create_cleanup_screen(void)
{
    s_scr_cleanup = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_cleanup, lv_color_white(), 0);
    lv_obj_set_style_pad_all(s_scr_cleanup, 2, 0);
    lv_obj_set_style_border_width(s_scr_cleanup, 0, 0);

    lv_obj_t *t = lv_label_create(s_scr_cleanup);
    lv_obj_set_style_text_font(t, &lv_font_unscii_8, 0);
    lv_label_set_text(t, "Cleanup Mode");
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

    s_lbl_cleanup_weight = lv_label_create(s_scr_cleanup);
    lv_obj_set_style_text_font(s_lbl_cleanup_weight, &lv_font_unscii_8, 0);
    lv_label_set_text(s_lbl_cleanup_weight, "W: ---");
    lv_obj_align(s_lbl_cleanup_weight, LV_ALIGN_TOP_RIGHT, 0, 0);

    s_lbl_cleanup_speed = lv_label_create(s_scr_cleanup);
    lv_obj_set_style_text_font(s_lbl_cleanup_speed, &lv_font_unscii_16, 0);
    lv_label_set_text(s_lbl_cleanup_speed, "0.0 rps");
    lv_obj_align(s_lbl_cleanup_speed, LV_ALIGN_CENTER, 0, -4);

    lv_obj_t *h = lv_label_create(s_scr_cleanup);
    lv_obj_set_style_text_font(h, &lv_font_unscii_8, 0);
    lv_label_set_text(h, "Turn=Speed");
    lv_obj_align(h, LV_ALIGN_BOTTOM_LEFT, 0, -14);

    lv_obj_t *b = create_inline_btn(s_scr_cleanup, "Stop", 48, 12, evt_stop_cleanup);
    lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void create_wireless_screen(void)
{
    s_scr_wireless = create_menu_screen();

    s_lbl_wifi_status = lv_label_create(s_scr_wireless);
    lv_obj_set_style_text_font(s_lbl_wifi_status, &lv_font_unscii_8, 0);
    lv_obj_set_style_pad_left(s_lbl_wifi_status, 2, 0);
    lv_obj_set_style_pad_top(s_lbl_wifi_status, 2, 0);
    lv_label_set_text(s_lbl_wifi_status, "WiFi: ---");

    s_lbl_wifi_ip = lv_label_create(s_scr_wireless);
    lv_obj_set_style_text_font(s_lbl_wifi_ip, &lv_font_unscii_8, 0);
    lv_obj_set_style_pad_left(s_lbl_wifi_ip, 2, 0);
    lv_label_set_text(s_lbl_wifi_ip, "IP: ---");

    create_menu_btn(s_scr_wireless, "< Back", evt_goto_main, NULL);
}

static void create_settings_screen(void)
{
    s_scr_settings = create_menu_screen();
    create_menu_btn(s_scr_settings, "Scale",     evt_goto_scale_settings,  NULL);
    create_menu_btn(s_scr_settings, "Profiles",  evt_goto_profile_manager, NULL);
    create_menu_btn(s_scr_settings, "Save NVS",  evt_save_nvs,             NULL);
    create_menu_btn(s_scr_settings, "Erase NVS", evt_erase_nvs,            NULL);
    create_menu_btn(s_scr_settings, "Version",   evt_version_info,         NULL);
    create_menu_btn(s_scr_settings, "Reboot",    evt_reboot,               NULL);
    create_menu_btn(s_scr_settings, "< Back",    evt_goto_main,            NULL);
}

static void create_scale_settings_screen(void)
{
    s_scr_scale_settings = create_menu_screen();

    s_lbl_scale_driver = lv_label_create(s_scr_scale_settings);
    lv_obj_set_style_text_font(s_lbl_scale_driver, &lv_font_unscii_8, 0);
    lv_obj_set_style_pad_left(s_lbl_scale_driver, 2, 0);
    lv_obj_set_style_pad_top(s_lbl_scale_driver, 2, 0);
    lv_label_set_text(s_lbl_scale_driver, "Drv: ---");

    s_lbl_scale_baudrate = lv_label_create(s_scr_scale_settings);
    lv_obj_set_style_text_font(s_lbl_scale_baudrate, &lv_font_unscii_8, 0);
    lv_obj_set_style_pad_left(s_lbl_scale_baudrate, 2, 0);
    lv_label_set_text(s_lbl_scale_baudrate, "Baud: ---");

    create_menu_btn(s_scr_scale_settings, "Set Driver",   evt_goto_scale_driver,   NULL);
    create_menu_btn(s_scr_scale_settings, "Set Baudrate", evt_goto_scale_baudrate, NULL);
    create_menu_btn(s_scr_scale_settings, "< Back",       evt_goto_settings,       NULL);
}

static void rebuild_scale_driver_screen(void)
{
    if (s_scr_scale_driver) lv_obj_delete(s_scr_scale_driver);
    s_scr_scale_driver = create_menu_screen();
    for (int i = 0; i < (int)NUM_SCALE_DRIVERS; i++) {
        create_menu_btn(s_scr_scale_driver, scale_driver_names[i],
                       evt_select_driver, (void*)(uintptr_t)i);
    }
    create_menu_btn(s_scr_scale_driver, "< Back", evt_goto_scale_settings, NULL);
}

static void rebuild_scale_baudrate_screen(void)
{
    if (s_scr_scale_baudrate) lv_obj_delete(s_scr_scale_baudrate);
    s_scr_scale_baudrate = create_menu_screen();
    for (int i = 0; i < (int)NUM_SCALE_BAUDRATES; i++) {
        create_menu_btn(s_scr_scale_baudrate, scale_baudrate_names[i],
                       evt_select_baudrate, (void*)(uintptr_t)i);
    }
    create_menu_btn(s_scr_scale_baudrate, "< Back", evt_goto_scale_settings, NULL);
}

static void rebuild_profile_view(uint8_t idx)
{
    if (s_scr_profile_view) lv_obj_delete(s_scr_profile_view);
    s_scr_profile_view = create_menu_screen();

    s_lbl_profile_info = lv_label_create(s_scr_profile_view);
    lv_obj_set_style_text_font(s_lbl_profile_info, &lv_font_unscii_8, 0);
    lv_obj_set_style_pad_all(s_lbl_profile_info, 2, 0);

    profile_t *p = profile_get_by_idx(idx);
    if (p && p->name[0] != '\0') {
        static char info_buf[192];
        snprintf(info_buf, sizeof(info_buf),
                 "%s\n"
                 "C: %.1f/%.1f/%.1f\n"
                 "   %.1f-%.1f rps\n"
                 "F: %.1f/%.1f/%.1f\n"
                 "   %.1f-%.1f rps",
                 p->name,
                 p->coarse_kp, p->coarse_ki, p->coarse_kd,
                 p->coarse_min_flow_speed_rps, p->coarse_max_flow_speed_rps,
                 p->fine_kp, p->fine_ki, p->fine_kd,
                 p->fine_min_flow_speed_rps, p->fine_max_flow_speed_rps);
        lv_label_set_text(s_lbl_profile_info, info_buf);
    } else {
        lv_label_set_text(s_lbl_profile_info, "No profile data");
    }

    create_menu_btn(s_scr_profile_view, "< Back", evt_goto_settings, NULL);
}

static void create_version_screen(void)
{
    s_scr_version = create_menu_screen();
    s_lbl_ver_text = lv_label_create(s_scr_version);
    lv_obj_set_style_text_font(s_lbl_ver_text, &lv_font_unscii_8, 0);
    lv_obj_set_style_pad_all(s_lbl_ver_text, 2, 0);
    lv_label_set_text(s_lbl_ver_text, "Loading...");
    create_menu_btn(s_scr_version, "< Back", evt_goto_settings, NULL);
}

// =========================================================================
// PUBLIC API
// =========================================================================

void ui_screens_init(void)
{
    s_group = lv_group_get_default();
    if (!s_group) {
        s_group = lv_group_create();
        lv_group_set_default(s_group);
    }

    create_main_menu();
    rebuild_weight_input();
    create_charge_screen();
    create_cleanup_screen();
    create_wireless_screen();
    create_settings_screen();
    create_scale_settings_screen();
    create_version_screen();

    switch_to_screen(SCR_MAIN_MENU, s_scr_main_menu);
    group_add_children(s_scr_main_menu);

    ESP_LOGI(TAG, "UI initialized - main menu active");
}

void ui_screens_update(void)
{
    switch (s_active_screen) {
    case SCR_CHARGE_MODE: {
        if (!s_scr_charge) break;

        charge_mode_state_t_runtime rt;
        if (charge_mode_get_runtime_state(&rt) != ESP_OK) break;

        lv_label_set_text(s_lbl_charge_title, charge_state_name(rt.charge_mode_state));

        if (rt.charge_mode_state == CHARGE_MODE_WAIT_FOR_COMPLETE) {
            char timer_buf[16];
            snprintf(timer_buf, sizeof(timer_buf), "%.1fs", rt.elapsed_time_seconds);
            lv_label_set_text(s_lbl_charge_timer, timer_buf);
        } else {
            lv_label_set_text(s_lbl_charge_timer, "");
        }

        float weight = 0.0f;
        bool weight_valid = false;
        if (rt.charge_mode_state == CHARGE_MODE_EXIT) {
            weight_valid = scale_is_measurement_valid();
            if (weight_valid) weight = scale_get_measurement();
        } else {
            weight_valid = true;
            weight = rt.current_weight;
        }

        char weight_buf[16];
        if (!weight_valid) {
            lv_label_set_text(s_lbl_charge_weight, "---");
        } else {
            charge_mode_config_t cfg;
            if (charge_mode_get_config(&cfg) == ESP_OK && cfg.decimal_places == DP_3) {
                snprintf(weight_buf, sizeof(weight_buf), "%.3f", weight);
            } else {
                snprintf(weight_buf, sizeof(weight_buf), "%.2f", weight);
            }
            lv_label_set_text(s_lbl_charge_weight, weight_buf);
        }
        lv_obj_align(s_lbl_charge_weight, LV_ALIGN_CENTER, 0, -2);

        // Show over/under charge only when dispensing is done and cup is still on scale
        if (rt.charge_mode_state == CHARGE_MODE_WAIT_FOR_CUP_REMOVAL && weight_valid) {
            if (weight > rt.target_charge_weight + 0.02f) {
                lv_label_set_text(s_lbl_charge_result, "OVER CHARGE");
            } else if (weight < rt.target_charge_weight - 0.02f) {
                lv_label_set_text(s_lbl_charge_result, "UNDER CHARGE");
            } else {
                lv_label_set_text(s_lbl_charge_result, "OK");
            }
        } else {
            lv_label_set_text(s_lbl_charge_result, "");
        }
        lv_obj_align(s_lbl_charge_result, LV_ALIGN_CENTER, 0, 11);

        if (rt.profile_name[0] != '\0') {
            lv_label_set_text(s_lbl_charge_profile, rt.profile_name);
        }
        break;
    }

    case SCR_CLEANUP_MODE: {
        if (!s_scr_cleanup) break;

        // Track raw encoder position for speed control
        // (bypasses LVGL group - only Stop button is in group)
        int32_t pos = encoder_get_position();
        int32_t delta = pos - s_cleanup_last_enc_pos;
        s_cleanup_last_enc_pos = pos;

        if (delta != 0) {
            // 4 raw transitions per detent -> 0.1 rps per detent
            s_cleanup_speed += (float)delta * 0.025f;
            if (s_cleanup_speed < 0.0f) s_cleanup_speed = 0.0f;
            if (s_cleanup_speed > 10.0f) s_cleanup_speed = 10.0f;
            cleanup_mode_set_speed(s_cleanup_speed);
        }

        char speed_buf[16];
        snprintf(speed_buf, sizeof(speed_buf), "%.1f rps", s_cleanup_speed);
        lv_label_set_text(s_lbl_cleanup_speed, speed_buf);

        if (scale_is_measurement_valid()) {
            char wbuf[16];
            snprintf(wbuf, sizeof(wbuf), "W: %.2f", scale_get_measurement());
            lv_label_set_text(s_lbl_cleanup_weight, wbuf);
        }
        break;
    }

    case SCR_WIRELESS_INFO: {
        if (!s_scr_wireless) break;
        bool connected = wifi_manager_is_connected();
        lv_label_set_text(s_lbl_wifi_status,
                          connected ? "WiFi: Connected" : "WiFi: AP Mode");
        static char ip_buf[32];
        snprintf(ip_buf, sizeof(ip_buf), "IP: %s", wifi_manager_get_ip());
        lv_label_set_text(s_lbl_wifi_ip, ip_buf);
        break;
    }

    default:
        break;
    }
}
