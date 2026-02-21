#include "system_control.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "SystemControl";

// Version information - can be overridden by build system
#ifndef PROJECT_VERSION
#define PROJECT_VERSION "1.0.0"
#endif

#ifndef PROJECT_VCS_HASH
#define PROJECT_VCS_HASH "unknown"
#endif

#ifndef PROJECT_BUILD_TYPE
#ifdef NDEBUG
#define PROJECT_BUILD_TYPE "Release"
#else
#define PROJECT_BUILD_TYPE "Debug"
#endif
#endif

static system_info_t system_info;

esp_err_t system_control_init(void)
{
    ESP_LOGI(TAG, "Initializing System Control module");

    // Get MAC address as unique ID
    uint8_t mac[6];
    esp_err_t ret = esp_efuse_mac_get_default(mac);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get MAC address: %s", esp_err_to_name(ret));
        snprintf(system_info.unique_id, sizeof(system_info.unique_id), "unknown");
    } else {
        snprintf(system_info.unique_id, sizeof(system_info.unique_id),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    // Set version information
    strncpy(system_info.version_string, PROJECT_VERSION, sizeof(system_info.version_string) - 1);
    system_info.version_string[sizeof(system_info.version_string) - 1] = '\0';
    strncpy(system_info.vcs_hash, PROJECT_VCS_HASH, sizeof(system_info.vcs_hash) - 1);
    system_info.vcs_hash[sizeof(system_info.vcs_hash) - 1] = '\0';
    strncpy(system_info.build_type, PROJECT_BUILD_TYPE, sizeof(system_info.build_type) - 1);
    system_info.build_type[sizeof(system_info.build_type) - 1] = '\0';

    ESP_LOGI(TAG, "System Info:");
    ESP_LOGI(TAG, "  Unique ID: %s", system_info.unique_id);
    ESP_LOGI(TAG, "  Version: %s", system_info.version_string);
    ESP_LOGI(TAG, "  VCS Hash: %s", system_info.vcs_hash);
    ESP_LOGI(TAG, "  Build Type: %s", system_info.build_type);

    return ESP_OK;
}

esp_err_t system_control_get_info(system_info_t *info)
{
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }

    *info = system_info;
    return ESP_OK;
}

esp_err_t system_control_save_all_nvs(void)
{
    ESP_LOGI(TAG, "Saving all NVS settings");

    // Note: Individual components handle their own NVS saves
    // This function is here for compatibility with the original API
    // In the future, we could register save handlers from each component

    ESP_LOGI(TAG, "NVS save complete (components save individually)");
    return ESP_OK;
}

esp_err_t system_control_erase_nvs(bool reboot)
{
    ESP_LOGW(TAG, "Erasing all NVS settings!");

    esp_err_t ret = nvs_flash_erase();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "NVS erased successfully");

    if (reboot) {
        ESP_LOGI(TAG, "Rebooting in 1 second...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        system_control_reboot();
    } else {
        // Keep system usable without reboot after erase
        ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ret = nvs_flash_erase();
            if (ret == ESP_OK) {
                ret = nvs_flash_init();
            }
        }
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reinitialize NVS after erase: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    return ESP_OK;
}

void system_control_reboot(void)
{
    ESP_LOGI(TAG, "Rebooting system...");
    vTaskDelay(pdMS_TO_TICKS(100)); // Give time for log to flush
    esp_restart();
}
