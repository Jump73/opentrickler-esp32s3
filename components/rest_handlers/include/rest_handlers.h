#ifndef REST_HANDLERS_H_
#define REST_HANDLERS_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// REST handler function signature (compatible with http_server_ot)
typedef char* (*rest_handler_t)(int num_params, char *params[], char *values[]);

// WiFi configuration endpoint
// Parameters: w0=ssid, w1=password, w2=auth, w3=timeout_ms, w4=enable, ee=save
char* rest_wireless_config_handler(int num_params, char *params[], char *values[]);

// System control endpoint
// Parameters: s0=unique_id(ro), s1=version_string(ro), s2=vcs_hash(ro), s3=build_type(ro),
//             s4=save_to_nvs(bool), s5=software_reset(bool), s6=erase_nvs(bool)
char* rest_system_control_handler(int num_params, char *params[], char *values[]);

// Motor configuration endpoints
// Parameters: m0=angular_accel, m1=full_steps, m2=current_ma, m3=microsteps,
//             m4=max_speed_rps, m5=r_sense, m6=min_speed_rps, m7=gear_ratio,
//             m8=inverted_enable, m9=inverted_direction, ee=save
char* rest_coarse_motor_config_handler(int num_params, char *params[], char *values[]);
char* rest_fine_motor_config_handler(int num_params, char *params[], char *values[]);

// Scale configuration and action endpoints
// Configuration parameters: s0=driver, s1=baudrate, ee=save
// Action parameters: a0=action_type
char* rest_scale_config_handler(int num_params, char *params[], char *values[]);
char* rest_scale_action_handler(int num_params, char *params[], char *values[]);

// Charge mode configuration and state endpoints
// Configuration parameters: c1-c4=LED_colors(hex), c5-c8=thresholds/margins(float),
//                          c9=decimal_places(int), c10=precharge_enable(bool),
//                          c11=precharge_time_ms(int), c12=precharge_speed_rps(float), ee=save
// State parameters: s0=target_weight(float), s2=state(int) [control]
//                   Returns: s0-s5 (target, current_weight, state, event, profile, elapsed_time)
char* rest_charge_mode_config_handler(int num_params, char *params[], char *values[]);
char* rest_charge_mode_state_handler(int num_params, char *params[], char *values[]);

// Profile configuration and summary endpoints
// Configuration parameters: pf=profile_index(int), p0=rev, p1=compatibility, p2=name(str),
//                          p3-p5=coarse_kp/ki/kd, p6-p7=coarse_min/max_flow_speed_rps,
//                          p8-p10=fine_kp/ki/kd, p11-p12=fine_min/max_flow_speed_rps, ee=save
// Summary: Returns s0={profile_dict}, s1=current_profile_idx
char* rest_profile_config_handler(int num_params, char *params[], char *values[]);
char* rest_profile_summary_handler(int num_params, char *params[], char *values[]);

// Coarse motor autotuning endpoint
// Parameters: a0=start(bool), a1=coarse_target_weight(float), a2=coarse_target_time_s(float),
//            a3=fine_target_weight(float), a4=fine_target_time_s(float), a5=max_runs_per_stage(int),
//            a6=weight_tolerance(float), a7=time_tolerance_s(float), a8=auto_apply(bool),
//            ee=save(bool), ca=cancel(bool)
char* rest_autotune_coarse_handler(int num_params, char *params[], char *values[]);

// Autotune trial history endpoint - returns JSON array of all trials for current stage
char* rest_autotune_trials_handler(int num_params, char *params[], char *values[]);
char* rest_autotune_telemetry_handler(int num_params, char *params[], char *values[]);

// Cleanup mode state endpoint
// State parameters: s0=state(int), s1=trickler_speed(float)
// Returns: s0=state, s1=trickler_speed
char* rest_cleanup_mode_state_handler(int num_params, char *params[], char *values[]);

// NeoPixel LED configuration endpoint
// Configuration parameters: bl=mini12864_backlight(hex), l1=led1(hex), l2=led2(hex),
//                          l3=pwm_out_chain_count(int), l4=pwm_out_is_rgbw(bool),
//                          l5=pwm_out_colour_order(int), ee=save
char* rest_neopixel_led_config_handler(int num_params, char *params[], char *values[]);

// Helper: Parse boolean from string ("true"/"false" or "1"/"0")
bool string_to_boolean(const char *str);

// Helper: Convert boolean to string
const char* boolean_to_string(bool value);

#ifdef __cplusplus
}
#endif

#endif  // REST_HANDLERS_H_
