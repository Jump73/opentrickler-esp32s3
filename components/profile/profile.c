#include "profile.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "Profile";

#define NVS_NAMESPACE "profile"
#define NVS_KEY_DATA "data"
#define PROFILE_DATA_REV 1

// Default profiles from original (powder types)
static const profile_t default_ar_2208_profile = {
    .rev = 0,
    .compatibility = 0,
    .name = "AR2208,gr",
    .coarse_kp = 0.025f,
    .coarse_ki = 0.0f,
    .coarse_kd = 0.25f,
    .coarse_min_flow_speed_rps = 0.1f,
    .coarse_max_flow_speed_rps = 5.0f,
    .fine_kp = 2.0f,
    .fine_ki = 0.0f,
    .fine_kd = 10.0f,
    .fine_min_flow_speed_rps = 0.1f,
    .fine_max_flow_speed_rps = 3.0f,
};

static const profile_t default_ar_2209_profile = {
    .rev = 0,
    .compatibility = 0,
    .name = "AR2209,gr",
    .coarse_kp = 0.02f,
    .coarse_ki = 0.0f,
    .coarse_kd = 0.30f,
    .coarse_min_flow_speed_rps = 0.1f,
    .coarse_max_flow_speed_rps = 5.0f,
    .fine_kp = 2.0f,
    .fine_ki = 0.0f,
    .fine_kd = 10.0f,
    .fine_min_flow_speed_rps = 0.1f,
    .fine_max_flow_speed_rps = 3.0f,
};

// Global profile data
static profile_data_t profile_data = {0};

esp_err_t profile_init(void)
{
    ESP_LOGI(TAG, "Initializing profile system");

    // Try to load saved profile data
    if (profile_load(&profile_data) == ESP_OK) {
        ESP_LOGI(TAG, "Loaded profile data from NVS");
        ESP_LOGI(TAG, "Current profile: %d - %s",
                 profile_data.current_profile_idx,
                 profile_data.profiles[profile_data.current_profile_idx].name);
    } else {
        ESP_LOGI(TAG, "No saved profile data, creating defaults");

        // Initialize with defaults
        profile_data.profile_data_rev = PROFILE_DATA_REV;
        profile_data.current_profile_idx = 0;

        // Copy two default profiles
        memcpy(&profile_data.profiles[0], &default_ar_2208_profile, sizeof(profile_t));
        memcpy(&profile_data.profiles[1], &default_ar_2209_profile, sizeof(profile_t));

        // Create empty profiles for slots 2-7
        for (uint8_t idx = 2; idx < MAX_PROFILE_CNT; idx++) {
            profile_t *p = &profile_data.profiles[idx];
            memset(p, 0, sizeof(profile_t));
            snprintf(p->name, PROFILE_NAME_MAX_LEN, "NewProfile%d", idx);
            // Set some reasonable defaults
            p->coarse_kp = 0.025f;
            p->coarse_kd = 0.25f;
            p->coarse_min_flow_speed_rps = 0.1f;
            p->coarse_max_flow_speed_rps = 5.0f;
            p->fine_kp = 2.0f;
            p->fine_kd = 10.0f;
            p->fine_min_flow_speed_rps = 0.1f;
            p->fine_max_flow_speed_rps = 3.0f;
        }

        // Save defaults
        profile_save();
    }

    return ESP_OK;
}

esp_err_t profile_save(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ESP_LOGI(TAG, "Saving profile data to NVS");

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }

    profile_data.profile_data_rev = PROFILE_DATA_REV;

    ret = nvs_set_blob(nvs_handle, NVS_KEY_DATA, &profile_data, sizeof(profile_data_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error saving profile data: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }

    ret = nvs_commit(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Profile data saved successfully");
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t profile_load(profile_data_t *data)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No saved profile data found");
        return ret;
    }

    size_t required_size = sizeof(profile_data_t);
    ret = nvs_get_blob(nvs_handle, NVS_KEY_DATA, data, &required_size);

    if (ret == ESP_OK) {
        // Check version
        if (data->profile_data_rev != PROFILE_DATA_REV) {
            ESP_LOGW(TAG, "Profile data version mismatch");
            nvs_close(nvs_handle);
            return ESP_ERR_INVALID_VERSION;
        }
        ESP_LOGI(TAG, "Loaded profile data from NVS");
    } else {
        ESP_LOGW(TAG, "Failed to load profile data: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t profile_select(uint8_t idx)
{
    if (idx >= MAX_PROFILE_CNT) {
        ESP_LOGE(TAG, "Invalid profile index: %d", idx);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Selected profile %d: %s", idx, profile_data.profiles[idx].name);
    profile_data.current_profile_idx = idx;

    return ESP_OK;
}

uint16_t profile_get_selected_idx(void)
{
    return profile_data.current_profile_idx;
}

profile_t* profile_get_selected(void)
{
    return &profile_data.profiles[profile_data.current_profile_idx];
}

profile_t* profile_get_by_idx(uint8_t idx)
{
    if (idx >= MAX_PROFILE_CNT) {
        return NULL;
    }
    return &profile_data.profiles[idx];
}

esp_err_t profile_get_all_names(char names[][PROFILE_NAME_MAX_LEN], uint8_t *count)
{
    if (!names || !count) {
        return ESP_ERR_INVALID_ARG;
    }

    for (uint8_t i = 0; i < MAX_PROFILE_CNT; i++) {
        strncpy(names[i], profile_data.profiles[i].name, PROFILE_NAME_MAX_LEN);
        names[i][PROFILE_NAME_MAX_LEN - 1] = '\0';
    }

    *count = MAX_PROFILE_CNT;
    return ESP_OK;
}
