#include "scale.h"
#include "board_pins.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

static const char *TAG = "Scale";

#define NVS_NAMESPACE "scale"
#define NVS_KEY_CONFIG "config"
#define CONFIG_VERSION 1

#define SCALE_UART_BUF_SIZE 256
#define SCALE_TASK_STACK    4096
#define SCALE_TASK_PRIORITY 9

// Default configuration
static scale_config_t scale_config = {
    .config_version = CONFIG_VERSION,
    .scale_driver = SCALE_DRIVER_AND_FXI,
    .scale_baudrate = BAUDRATE_19200  // Match original firmware default
};

// Current measurement
static float current_measurement = 0.0f;
static bool measurement_valid = false;

// Synchronization
static SemaphoreHandle_t scale_measurement_sem = NULL;
static SemaphoreHandle_t scale_write_mutex = NULL;

// AND FXi frame (17 bytes): ST,+    0.00 GN\r\n
typedef union {
    struct __attribute__((__packed__)) {
        char header[2];      // Status (e.g. "ST")
        char comma;          // ','
        char data[9];        // Weight string (right-aligned, e.g. "  123.456")
        char unit[3];        // "GN " or "g  "
        char terminator[2];  // "\r\n"
    };
    char bytes[17];
} and_fxi_frame_t;

// Steinberg SBS frame (16 bytes): S  +    0.00GN\r\n
typedef union {
    struct __attribute__((__packed__)) {
        char header[2];
        char data[10];
        char unit[2];
        char terminator[2];
    };
    char bytes[16];
} steinberg_frame_t;

// G&G JJB frame (14 bytes): +  1.234 GN\r\n (requires polling)
typedef union {
    struct __attribute__((__packed__)) {
        char header[2];
        char data[7];
        char unit[3];
        char terminator[2];
    };
    char bytes[14];
} gng_frame_t;

// US Solid JFDBS frame (15 bytes)
typedef union {
    struct __attribute__((__packed__)) {
        char header[2];
        char data[8];
        char unit[3];
        char terminator[2];
    };
    char bytes[15];
} ussolid_frame_t;

// Creedmoor frame (14 bytes)
typedef union {
    struct __attribute__((__packed__)) {
        char header[1];
        char data[7];
        char space;
        char unit[2];
        char space2;
        char terminator[2];
    };
    char bytes[14];
} creedmoor_frame_t;

// Radwag PS R2 frame (21 bytes): SUI <stability><12-char mass>gr \r\n
typedef union {
    struct __attribute__((__packed__)) {
        char command[3];
        char stability;
        char mass[12];
        char unit[3];
        char terminator[2];
    };
    char bytes[21];
} radwag_frame_t;

// JM Science frame (19 bytes)
typedef union {
    struct __attribute__((__packed__)) {
        char header;
        char space;
        char stable_state;
        char symbol;
        char data[9];
        char space2;
        char unit[3];
        char terminator[2];
    };
    char bytes[19];
} jm_science_frame_t;

static int get_baudrate_value(scale_baudrate_t baud)
{
    switch (baud) {
        case BAUDRATE_4800:  return 4800;
        case BAUDRATE_9600:  return 9600;
        case BAUDRATE_19200: return 19200;
        default:             return 9600;
    }
}

static void scale_write(const char *cmd, size_t len)
{
    if (scale_write_mutex) {
        xSemaphoreTake(scale_write_mutex, portMAX_DELAY);
    }
    uart_write_bytes(SCALE_UART_NUM, cmd, len);
    if (scale_write_mutex) {
        xSemaphoreGive(scale_write_mutex);
    }
}

static void update_measurement(float weight)
{
    current_measurement = weight;
    measurement_valid = !isnanf(weight);
    if (scale_measurement_sem && measurement_valid) {
        xSemaphoreGive(scale_measurement_sem);
    }
}

// Generic/line-buffer approach used for all drivers
// Accumulates bytes into line buffer, parses on \n
#define LINE_BUF_SIZE 64
static char line_buf[LINE_BUF_SIZE];
static int line_buf_idx = 0;

static void scale_line_feed(void)
{
    if (line_buf_idx == 0) {
        return;
    }
    line_buf[line_buf_idx] = '\0';

    // Scan every position for a valid float (handles G&G "+ 1.234" format
    // where sign and number are separated by spaces)
    char *endptr;
    for (char *p = line_buf; *p; p++) {
        float w = strtof(p, &endptr);
        if (endptr != p) {
            ESP_LOGI(TAG, "Parsed weight: %.4f from '%s'", w, line_buf);
            update_measurement(w);
            break;
        }
    }

    line_buf_idx = 0;
}

static void process_rx_byte(uint8_t rx_byte)
{
    if (scale_config.scale_driver == SCALE_DRIVER_JM_SCIENCE && rx_byte == 'E') {
        line_buf_idx = 0;
    }
    if (rx_byte == '\n') {
        scale_line_feed();
    } else if (rx_byte != '\r') {
        if (line_buf_idx < LINE_BUF_SIZE - 1) {
            line_buf[line_buf_idx++] = (char)rx_byte;
        } else {
            line_buf_idx = 0;
        }
    }
}

static void scale_task(void *pvParameters)
{
    uint8_t rx_byte;

    ESP_LOGI(TAG, "Scale task started, driver: %d", scale_config.scale_driver);

    while (1) {
        if (scale_config.scale_driver == SCALE_DRIVER_GNG_JJB) {
            // G&G JJB: polling mode - request weight every ~250ms
            scale_write("!p\r\n", 4);
            while (uart_read_bytes(SCALE_UART_NUM, &rx_byte, 1,
                                   pdMS_TO_TICKS(100)) > 0) {
                process_rx_byte(rx_byte);
            }
            vTaskDelay(pdMS_TO_TICKS(150));
        } else {
            // Other drivers: continuous output, read bytes as they arrive
            if (uart_read_bytes(SCALE_UART_NUM, &rx_byte, 1,
                                pdMS_TO_TICKS(10)) > 0) {
                process_rx_byte(rx_byte);
            } else {
                vTaskDelay(pdMS_TO_TICKS(5));
            }
        }
    }
}

static bool uart_initialized = false;

static esp_err_t scale_uart_init(void)
{
    if (uart_initialized) {
        uart_driver_delete(SCALE_UART_NUM);
        uart_initialized = false;
    }

    uart_config_t uart_config = {
        .baud_rate  = get_baudrate_value(scale_config.scale_baudrate),
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    };

    esp_err_t ret;

    ret = uart_param_config(SCALE_UART_NUM, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_set_pin(SCALE_UART_NUM,
                       SCALE_UART_TX_PIN, SCALE_UART_RX_PIN,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_driver_install(SCALE_UART_NUM, SCALE_UART_BUF_SIZE, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    uart_initialized = true;
    ESP_LOGI(TAG, "Scale UART initialized: baud=%d, 7N1, TX=%d, RX=%d",
             get_baudrate_value(scale_config.scale_baudrate),
             SCALE_UART_TX_PIN, SCALE_UART_RX_PIN);
    return ESP_OK;
}

esp_err_t scale_init(void)
{
    ESP_LOGI(TAG, "Initializing scale module");

    // Try to load saved configuration
    scale_config_t temp_config;
    if (scale_load_config(&temp_config) == ESP_OK) {
        scale_config = temp_config;
        ESP_LOGI(TAG, "Loaded scale config from NVS");
    } else {
        ESP_LOGI(TAG, "Using default scale config");
    }

    ESP_LOGI(TAG, "Scale driver: %d, baudrate: %d",
             scale_config.scale_driver, scale_config.scale_baudrate);

    // Initialize synchronization primitives
    scale_measurement_sem = xSemaphoreCreateBinary();
    scale_write_mutex = xSemaphoreCreateMutex();

    if (!scale_measurement_sem || !scale_write_mutex) {
        ESP_LOGE(TAG, "Failed to create semaphores");
        return ESP_ERR_NO_MEM;
    }

    // Initialize UART
    esp_err_t ret = scale_uart_init();
    if (ret != ESP_OK) {
        return ret;
    }

    // Start scale reading task
    BaseType_t task_ret = xTaskCreate(scale_task, "scale_task",
                                       SCALE_TASK_STACK, NULL,
                                       SCALE_TASK_PRIORITY, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create scale task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Scale module initialized");
    return ESP_OK;
}

esp_err_t scale_save_config(const scale_config_t *config)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ESP_LOGI(TAG, "Saving scale config to NVS");
    ESP_LOGI(TAG, "  Driver: %d", config->scale_driver);
    ESP_LOGI(TAG, "  Baudrate: %d", config->scale_baudrate);

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }

    // Save config with version
    scale_config_t config_to_save = *config;
    config_to_save.config_version = CONFIG_VERSION;

    ret = nvs_set_blob(nvs_handle, NVS_KEY_CONFIG, &config_to_save, sizeof(scale_config_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error saving config: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }

    ret = nvs_commit(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Scale config saved successfully");
        bool baudrate_changed = (scale_config.scale_baudrate != config_to_save.scale_baudrate);
        // Update local copy
        scale_config = config_to_save;
        // Reinitialize UART if baudrate changed
        if (baudrate_changed) {
            ESP_LOGI(TAG, "Baudrate changed, reinitializing UART at %d baud",
                     get_baudrate_value(scale_config.scale_baudrate));
            if (scale_write_mutex) {
                xSemaphoreTake(scale_write_mutex, portMAX_DELAY);
            }
            scale_uart_init();
            if (scale_write_mutex) {
                xSemaphoreGive(scale_write_mutex);
            }
        }
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t scale_load_config(scale_config_t *config)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No saved scale config found");
        return ret;
    }

    size_t required_size = sizeof(scale_config_t);
    ret = nvs_get_blob(nvs_handle, NVS_KEY_CONFIG, config, &required_size);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Loaded scale config from NVS");
    } else {
        ESP_LOGW(TAG, "Failed to load config: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t scale_get_config(scale_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    *config = scale_config;
    return ESP_OK;
}

static scale_baudrate_t get_default_baudrate_for_driver(scale_driver_t driver)
{
    switch (driver) {
        case SCALE_DRIVER_AND_FXI:
            return BAUDRATE_19200;
        case SCALE_DRIVER_GNG_JJB:
        case SCALE_DRIVER_STEINBERG_SBS:
        case SCALE_DRIVER_USSOLID_JFDBS:
        case SCALE_DRIVER_JM_SCIENCE:
        case SCALE_DRIVER_CREEDMOOR:
        case SCALE_DRIVER_RADWAG_PS_R2:
        case SCALE_DRIVER_SARTORIUS:
        case SCALE_DRIVER_GENERIC_DRV:
        default:
            return BAUDRATE_9600;
    }
}

esp_err_t scale_set_driver(scale_driver_t driver)
{
    ESP_LOGI(TAG, "Setting scale driver to: %d", driver);
    scale_config.scale_driver = driver;

    // Auto-set default baudrate for this driver
    scale_baudrate_t new_baud = get_default_baudrate_for_driver(driver);
    if (scale_config.scale_baudrate != new_baud) {
        scale_config.scale_baudrate = new_baud;
        ESP_LOGI(TAG, "Auto-set baudrate to %d for driver %d",
                 get_baudrate_value(new_baud), driver);
        if (scale_write_mutex) {
            xSemaphoreTake(scale_write_mutex, portMAX_DELAY);
        }
        scale_uart_init();
        if (scale_write_mutex) {
            xSemaphoreGive(scale_write_mutex);
        }
    }

    // Reset line buffer on driver change
    line_buf_idx = 0;
    measurement_valid = false;
    return ESP_OK;
}

esp_err_t scale_perform_action(scale_action_t action)
{
    switch (action) {
        case SCALE_ACTION_FORCE_ZERO:
            ESP_LOGI(TAG, "Performing force zero");
            // Send tare command based on driver
            switch (scale_config.scale_driver) {
                case SCALE_DRIVER_AND_FXI:
                    scale_write("Z\r\n", 3);
                    break;
                case SCALE_DRIVER_GNG_JJB:
                    scale_write("!t\r\n", 4);
                    break;
                case SCALE_DRIVER_RADWAG_PS_R2:
                    scale_write("T\r\n", 3);
                    break;
                default:
                    ESP_LOGW(TAG, "Force zero not supported for this driver");
                    break;
            }
            break;
        case SCALE_ACTION_NO_ACTION:
        default:
            ESP_LOGD(TAG, "No action requested");
            break;
    }
    return ESP_OK;
}

float scale_get_measurement(void)
{
    if (!measurement_valid) {
        return nanf("");
    }
    return current_measurement;
}

bool scale_is_measurement_valid(void)
{
    return measurement_valid;
}

bool scale_block_wait_for_measurement(uint32_t timeout_ms, float *measurement)
{
    TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

    if (xSemaphoreTake(scale_measurement_sem, ticks) == pdTRUE) {
        *measurement = current_measurement;
        return true;
    }
    return false;
}
