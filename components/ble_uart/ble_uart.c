#include "ble_uart.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>

static const char *TAG = "BLE_UART";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static ble_uart_rx_cb_t s_rx_cb        = NULL;
static uint16_t         s_conn_handle  = BLE_HS_CONN_HANDLE_NONE;
static uint16_t         s_tx_val_handle = 0;
static char             s_dev_name[32] = "OpenTrickler";

// ---------------------------------------------------------------------------
// NUS 128-bit UUIDs  (little-endian byte order as required by NimBLE)
// Service : 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
// RX char : 6E400002-B5A3-F393-E0A9-E50E24DCCA9E  (phone → ESP32, write)
// TX char : 6E400003-B5A3-F393-E0A9-E50E24DCCA9E  (ESP32 → phone, notify)
// ---------------------------------------------------------------------------
static const ble_uuid128_t s_nus_svc_uuid = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E
);
static const ble_uuid128_t s_nus_rx_uuid = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E
);
static const ble_uuid128_t s_nus_tx_uuid = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E
);

// ---------------------------------------------------------------------------
// GATT access callbacks
// ---------------------------------------------------------------------------
static int nus_rx_cb(uint16_t conn_handle, uint16_t attr_handle,
                     struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && s_rx_cb) {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        uint8_t buf[256];
        if (len > sizeof(buf)) {
            len = sizeof(buf);
        }
        ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL);
        s_rx_cb(buf, len);
    }
    return 0;
}

static int nus_tx_cb(uint16_t conn_handle, uint16_t attr_handle,
                     struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    // TX is notify-only; reads return nothing
    return 0;
}

// ---------------------------------------------------------------------------
// GATT service table
// ---------------------------------------------------------------------------
static const struct ble_gatt_svc_def s_nus_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_nus_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid       = &s_nus_rx_uuid.u,
                .access_cb  = nus_rx_cb,
                .flags      = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid        = &s_nus_tx_uuid.u,
                .access_cb   = nus_tx_cb,
                .val_handle  = &s_tx_val_handle,
                .flags       = BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 },
        },
    },
    { 0 },
};

// ---------------------------------------------------------------------------
// Advertising
// ---------------------------------------------------------------------------
static int gap_event_cb(struct ble_gap_event *event, void *arg);

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags              = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name               = (uint8_t *)s_dev_name;
    fields.name_len           = strlen(s_dev_name);
    fields.name_is_complete   = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv set fields failed: %d", rc);
        return;
    }

    // Scan response: NUS service UUID so phone can filter by service UUID
    struct ble_hs_adv_fields rsp = {0};
    rsp.uuids128             = &s_nus_svc_uuid;
    rsp.num_uuids128         = 1;
    rsp.uuids128_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        ESP_LOGW(TAG, "adv rsp set fields failed: %d (continuing)", rc);
    }

    struct ble_gap_adv_params params = {
        .conn_mode  = BLE_GAP_CONN_MODE_UND,
        .disc_mode  = BLE_GAP_DISC_MODE_GEN,
        .itvl_min   = BLE_GAP_ADV_ITVL_MS(2000),
        .itvl_max   = BLE_GAP_ADV_ITVL_MS(2500),
    };

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &params, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv start failed: %d", rc);
    } else {
        ESP_LOGI(TAG, "Advertising as \"%s\"", s_dev_name);
    }
}

// ---------------------------------------------------------------------------
// GAP event handler
// ---------------------------------------------------------------------------
static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "Connected (handle=%d)", s_conn_handle);
        } else {
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ESP_LOGI(TAG, "Disconnected (reason=%d)", event->disconnect.reason);
        start_advertising();
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated: %d", event->mtu.value);
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGD(TAG, "Subscribe: attr=%d cur_notify=%d",
                 event->subscribe.attr_handle,
                 event->subscribe.cur_notify);
        break;

    default:
        break;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// NimBLE host callbacks
// ---------------------------------------------------------------------------
static void on_ble_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ensure_addr failed: %d", rc);
        return;
    }
    start_advertising();
}

static void on_ble_reset(int reason)
{
    ESP_LOGE(TAG, "BLE host reset, reason=%d", reason);
}

static void nimble_host_task(void *arg)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
esp_err_t ble_uart_init(const char *device_name, ble_uart_rx_cb_t rx_cb)
{
    s_rx_cb = rx_cb;
    if (device_name) {
        strncpy(s_dev_name, device_name, sizeof(s_dev_name) - 1);
        s_dev_name[sizeof(s_dev_name) - 1] = '\0';
    }

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ble_hs_cfg.sync_cb  = on_ble_sync;
    ble_hs_cfg.reset_cb = on_ble_reset;

    // Register built-in GAP/GATT services first, then NUS
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(s_nus_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(s_nus_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return ESP_FAIL;
    }

    rc = ble_svc_gap_device_name_set(s_dev_name);
    if (rc != 0) {
        ESP_LOGW(TAG, "device_name_set failed: %d", rc);
    }

    nimble_port_freertos_init(nimble_host_task);

    ESP_LOGI(TAG, "BLE UART (NUS) initialised, device=\"%s\"", s_dev_name);
    return ESP_OK;
}

esp_err_t ble_uart_send(const uint8_t *data, size_t len)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) {
        return ESP_ERR_NO_MEM;
    }
    int rc = ble_gatts_notify_custom(s_conn_handle, s_tx_val_handle, om);
    return (rc == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t ble_uart_send_str(const char *str)
{
    return ble_uart_send((const uint8_t *)str, strlen(str));
}

bool ble_uart_is_connected(void)
{
    return s_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}
