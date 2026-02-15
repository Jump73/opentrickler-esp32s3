#ifndef WIFI_MANAGER_H_
#define WIFI_MANAGER_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// WiFi authentication types (compatible with original OpenTrickler)
typedef enum {
    WIFI_AUTH_TYPE_OPEN = 0,
    WIFI_AUTH_TYPE_WPA_PSK = 1,
    WIFI_AUTH_TYPE_WPA2_PSK = 2,
    WIFI_AUTH_TYPE_WPA_WPA2_PSK = 3,
} wifi_auth_type_t;

// WiFi configuration structure (stored in NVS)
typedef struct {
    uint16_t config_version;    // Config version for compatibility
    char ssid[32];              // WiFi SSID
    char password[64];          // WiFi password
    wifi_auth_type_t auth;      // Authentication type
    uint32_t timeout_ms;        // Connection timeout
    bool enable;                // Enable WiFi STA mode
} wifi_config_data_t;

// WiFi initialization
esp_err_t wifi_manager_init(void);

// Start WiFi in Access Point mode
esp_err_t wifi_manager_start_ap(const char *ssid, const char *password);

// Start WiFi in Station mode (connect to existing network)
esp_err_t wifi_manager_start_sta(const char *ssid, const char *password, uint32_t timeout_ms);

// Auto-start WiFi (tries STA from NVS, falls back to AP if not configured)
esp_err_t wifi_manager_auto_start(const char *ap_ssid, const char *ap_password);

// Stop WiFi
esp_err_t wifi_manager_stop(void);

// Get IP address as string
const char* wifi_manager_get_ip(void);

// Get connection status
bool wifi_manager_is_connected(void);

// Check if running in AP mode
bool wifi_manager_is_ap_mode(void);

// Configuration management (NVS)
esp_err_t wifi_manager_save_config(const wifi_config_data_t *config);
esp_err_t wifi_manager_load_config(wifi_config_data_t *config);
esp_err_t wifi_manager_get_config(wifi_config_data_t *config);
bool wifi_manager_has_saved_config(void);

#ifdef __cplusplus
}
#endif

#endif  // WIFI_MANAGER_H_
