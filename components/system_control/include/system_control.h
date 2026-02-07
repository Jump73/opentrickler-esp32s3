#ifndef SYSTEM_CONTROL_H_
#define SYSTEM_CONTROL_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// System information structure
typedef struct {
    char unique_id[32];      // MAC address or chip ID
    char version_string[32]; // Firmware version
    char vcs_hash[16];       // Git commit hash
    char build_type[16];     // Debug or Release
} system_info_t;

/**
 * @brief Initialize system control module
 * @return ESP_OK on success
 */
esp_err_t system_control_init(void);

/**
 * @brief Get system information
 * @param[out] info Pointer to system_info_t structure to fill
 * @return ESP_OK on success
 */
esp_err_t system_control_get_info(system_info_t *info);

/**
 * @brief Save all NVS settings (triggers save on all components)
 * @return ESP_OK on success
 */
esp_err_t system_control_save_all_nvs(void);

/**
 * @brief Erase all NVS settings
 * @param reboot If true, reboot after erasing
 * @return ESP_OK on success
 */
esp_err_t system_control_erase_nvs(bool reboot);

/**
 * @brief Software reboot
 */
void system_control_reboot(void);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_CONTROL_H_
