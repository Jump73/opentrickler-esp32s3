#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/inet.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "WiFi_Manager";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_netif = NULL;
static char s_ip_addr[16] = "0.0.0.0";
static bool s_is_connected = false;
static int s_retry_num = 0;
static const int MAX_RETRY = 5;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station connected, AID=%d", event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station disconnected, AID=%d", event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"Connect to the AP fail");
        s_is_connected = false;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        snprintf(s_ip_addr, sizeof(s_ip_addr), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Got IP: %s", s_ip_addr);
        s_retry_num = 0;
        s_is_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi manager...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "NVS flash needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "NVS initialized");

    // Initialize TCP/IP stack
    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "netif init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "TCP/IP stack initialized");

    // Create default event loop
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Event loop creation failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Event loop created");

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "WiFi driver initialized");

    s_wifi_event_group = xEventGroupCreate();

    ESP_LOGI(TAG, "WiFi manager initialized successfully");
    return ESP_OK;
}

esp_err_t wifi_manager_start_ap(const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "Starting WiFi AP mode...");
    ESP_LOGI(TAG, "  SSID: %s", ssid);
    ESP_LOGI(TAG, "  Password: %s", password);

    // Create AP netif
    s_netif = esp_netif_create_default_wifi_ap();
    if (!s_netif) {
        ESP_LOGE(TAG, "Failed to create AP netif");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "AP netif created");

    // Register event handlers
    esp_err_t ret = esp_event_handler_instance_register(WIFI_EVENT,
                                                         ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler,
                                                         NULL,
                                                         NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register event handler: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Event handlers registered");

    // Configure AP
    wifi_config_t wifi_config = {0};
    wifi_config.ap.ssid_len = strlen(ssid);
    wifi_config.ap.channel = 1;
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.ap.pmf_cfg.required = false;

    strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid) - 1);
    strncpy((char *)wifi_config.ap.password, password, sizeof(wifi_config.ap.password) - 1);

    if (strlen(password) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_LOGI(TAG, "Setting WiFi mode to AP...");
    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP mode: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Configuring WiFi AP...");
    ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Starting WiFi...");
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    // Wait a bit for AP to start
    vTaskDelay(pdMS_TO_TICKS(500));

    // Get AP IP address
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(s_netif, &ip_info);
    snprintf(s_ip_addr, sizeof(s_ip_addr), IPSTR, IP2STR(&ip_info.ip));

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "WiFi AP started successfully!");
    ESP_LOGI(TAG, "  SSID: %s", ssid);
    ESP_LOGI(TAG, "  Password: %s", password);
    ESP_LOGI(TAG, "  IP Address: %s", s_ip_addr);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    s_is_connected = true;

    return ESP_OK;
}

esp_err_t wifi_manager_start_sta(const char *ssid, const char *password, uint32_t timeout_ms)
{
    // Create STA netif
    s_netif = esp_netif_create_default_wifi_sta();

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                         ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler,
                                                         NULL,
                                                         NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                         IP_EVENT_STA_GOT_IP,
                                                         &wifi_event_handler,
                                                         NULL,
                                                         NULL));

    // Configure STA
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi STA started. Connecting to SSID:%s", ssid);

    // Wait for connection or timeout
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(timeout_ms));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP SSID:%s", ssid);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", ssid);
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "Connection timeout");
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t wifi_manager_stop(void)
{
    s_is_connected = false;
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());

    if (s_netif) {
        esp_netif_destroy(s_netif);
        s_netif = NULL;
    }

    ESP_LOGI(TAG, "WiFi stopped");
    return ESP_OK;
}

const char* wifi_manager_get_ip(void)
{
    return s_ip_addr;
}

bool wifi_manager_is_connected(void)
{
    return s_is_connected;
}

bool wifi_manager_is_ap_mode(void)
{
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) != ESP_OK) return false;
    return (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);
}

// NVS Configuration Management
#define NVS_NAMESPACE "wifi_config"
#define NVS_KEY_CONFIG "config"
#define CONFIG_VERSION 1

static wifi_config_data_t s_wifi_config = {0};

esp_err_t wifi_manager_save_config(const wifi_config_data_t *config)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ESP_LOGI(TAG, "Saving WiFi config to NVS...");
    ESP_LOGI(TAG, "  SSID: %s", config->ssid);
    ESP_LOGI(TAG, "  Auth: %d", config->auth);
    ESP_LOGI(TAG, "  Timeout: %lu ms", config->timeout_ms);
    ESP_LOGI(TAG, "  Enable: %d", config->enable);

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }

    // Save config with version
    wifi_config_data_t config_to_save = *config;
    config_to_save.config_version = CONFIG_VERSION;

    ret = nvs_set_blob(nvs_handle, NVS_KEY_CONFIG, &config_to_save, sizeof(wifi_config_data_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error saving config: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }

    ret = nvs_commit(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "WiFi config saved successfully");
        // Update local copy
        s_wifi_config = config_to_save;
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t wifi_manager_load_config(wifi_config_data_t *config)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No saved WiFi config found");
        return ret;
    }

    size_t required_size = sizeof(wifi_config_data_t);
    ret = nvs_get_blob(nvs_handle, NVS_KEY_CONFIG, config, &required_size);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Loaded WiFi config from NVS");
        ESP_LOGI(TAG, "  SSID: %s", config->ssid);
        ESP_LOGI(TAG, "  Auth: %d", config->auth);
        ESP_LOGI(TAG, "  Enable: %d", config->enable);

        // Update local copy
        s_wifi_config = *config;
    } else {
        ESP_LOGW(TAG, "Failed to load config: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t wifi_manager_get_config(wifi_config_data_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    *config = s_wifi_config;
    return ESP_OK;
}

bool wifi_manager_has_saved_config(void)
{
    wifi_config_data_t config;
    esp_err_t ret = wifi_manager_load_config(&config);
    return (ret == ESP_OK && config.enable && strlen(config.ssid) > 0);
}

esp_err_t wifi_manager_auto_start(const char *ap_ssid, const char *ap_password)
{
    ESP_LOGI(TAG, "Auto-starting WiFi...");

    // Try to load saved config
    wifi_config_data_t config;
    esp_err_t ret = wifi_manager_load_config(&config);

    if (ret == ESP_OK && config.enable && strlen(config.ssid) > 0) {
        // Have saved config and it's enabled - try STA mode
        ESP_LOGI(TAG, "Found saved WiFi config, attempting to connect to: %s", config.ssid);

        ret = wifi_manager_start_sta(config.ssid, config.password,
                                      config.timeout_ms > 0 ? config.timeout_ms : 10000);

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Successfully connected to saved WiFi network");
            return ESP_OK;
        } else {
            ESP_LOGW(TAG, "Failed to connect to saved network, starting AP mode");
        }
    } else {
        ESP_LOGI(TAG, "No saved WiFi config or WiFi disabled, starting AP mode");
    }

    // Fall back to AP mode
    return wifi_manager_start_ap(ap_ssid, ap_password);
}
