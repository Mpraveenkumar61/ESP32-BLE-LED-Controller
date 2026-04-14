#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "driver/gpio.h"

/* ── CONFIG ─────────────────────────────────────────── */
#define DEVICE_NAME   "ESP32-praveen"
#define LED_PIN       GPIO_NUM_2
/* ───────────────────────────────────────────────────── */

/* Nordic UART Service UUIDs (industry standard — works with LightBlue) */
#define NUS_SERVICE_UUID        0x0001
#define NUS_CHAR_RX_UUID        0x0002   /* Phone → ESP32 (write) */
#define NUS_CHAR_TX_UUID        0x0003   /* ESP32 → Phone (notify) */

#define PROFILE_NUM             1
#define PROFILE_APP_ID          0
#define GATTS_NUM_HANDLE        8

static const char *TAG = "BLE_LED";

static bool     led_state    = false;
static uint16_t conn_id      = 0xFFFF;   /* 0xFFFF = not connected */
static uint16_t gatts_if_saved = 0xFF;
static uint16_t tx_char_handle = 0;
static uint16_t tx_cccd_handle = 0;
static bool     notify_enabled = false;

/* ── Send text to phone via BLE notify ──────────────── */
static void ble_send(const char *msg)
{
    if (conn_id == 0xFFFF || !notify_enabled) return;

    esp_ble_gatts_send_indicate(
        gatts_if_saved, conn_id, tx_char_handle,
        strlen(msg), (uint8_t *)msg, false);
}

/* ── Process command from phone ─────────────────────── */
static void process_cmd(uint8_t *data, uint16_t len)
{
    char cmd[32] = {0};
    strncpy(cmd, (char *)data, len < 31 ? len : 31);

    /* Strip trailing \r \n space */
    int i = strlen(cmd) - 1;
    while (i >= 0 && (cmd[i] == '\r' || cmd[i] == '\n' || cmd[i] == ' '))
        cmd[i--] = '\0';

    ESP_LOGI(TAG, "CMD: [%s]", cmd);

    if (strcmp(cmd, "ON") == 0) {
        led_state = true;
        gpio_set_level(LED_PIN, 1);
        ble_send("ACK:ON\r\n");
    }
    else if (strcmp(cmd, "OFF") == 0) {
        led_state = false;
        gpio_set_level(LED_PIN, 0);
        ble_send("ACK:OFF\r\n");
    }
    else if (strcmp(cmd, "STATUS") == 0) {
        char resp[32];
        snprintf(resp, sizeof(resp), "LED:%s\r\n", led_state ? "ON" : "OFF");
        ble_send(resp);
    }
    else if (strcmp(cmd, "HELLO") == 0) {
        ble_send("Hi from ESP32-praveen!\r\n");
    }
    else if (strcmp(cmd, "BLINK") == 0) {
        for (int b = 0; b < 3; b++) {
            gpio_set_level(LED_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(200));
            gpio_set_level(LED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        gpio_set_level(LED_PIN, led_state ? 1 : 0);
        ble_send("ACK:BLINK\r\n");
    }
    else if (strcmp(cmd, "UPTIME") == 0) {
        char resp[40];
        uint32_t secs = xTaskGetTickCount() / configTICK_RATE_HZ;
        snprintf(resp, sizeof(resp), "UPTIME:%lus\r\n", (unsigned long)secs);
        ble_send(resp);
    }
    else {
        ble_send("ERR:UNKNOWN\r\n");
    }
}

/* ── BLE Advertisement data ─────────────────────────── */
static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp        = false,
    .include_name        = true,
    .include_txpower     = false,
    .min_interval        = 0x0006,
    .max_interval        = 0x0010,
    .appearance          = 0x00,
    .manufacturer_len    = 0,
    .p_manufacturer_data = NULL,
    .service_data_len    = 0,
    .p_service_data      = NULL,
    .service_uuid_len    = 0,
    .p_service_uuid      = NULL,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min       = 0x20,
    .adv_int_max       = 0x40,
    .adv_type          = ADV_TYPE_IND,
    .own_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .channel_map       = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

/* ── GATT attribute table ────────────────────────────── */
/* These MUST be variables — macros cannot be addressed with & */
static uint16_t pri_svc_uuid   = ESP_GATT_UUID_PRI_SERVICE;
static uint16_t chr_decl_uuid  = ESP_GATT_UUID_CHAR_DECLARE;

/* Service UUID */
static uint16_t svc_uuid       = NUS_SERVICE_UUID;

/* RX characteristic (phone writes to ESP32) */
static uint16_t rx_uuid        = NUS_CHAR_RX_UUID;
static uint8_t  rx_prop        = ESP_GATT_CHAR_PROP_BIT_WRITE |
                                  ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
static uint8_t  rx_val[32]     = {0};

/* TX characteristic (ESP32 notifies phone) */
static uint16_t tx_uuid        = NUS_CHAR_TX_UUID;
static uint8_t  tx_prop        = ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static uint8_t  tx_val[32]     = {0};

/* CCCD for TX (enables notifications) */
static uint16_t cccd_uuid      = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static uint8_t  cccd_val[2]    = {0x00, 0x00};
static const esp_gatts_attr_db_t gatt_db[] = {
    /* Service declaration */
    [0] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&pri_svc_uuid,       /* ← was &ESP_GATT_UUID_PRI_SERVICE */
         ESP_GATT_PERM_READ, sizeof(uint16_t), sizeof(svc_uuid),
         (uint8_t *)&svc_uuid}
    },
    /* RX char declaration */
    [1] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&chr_decl_uuid,      /* ← was &ESP_GATT_UUID_CHAR_DECLARE */
         ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(rx_prop),
         (uint8_t *)&rx_prop}
    },
    /* RX char value */
    [2] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&rx_uuid,
         ESP_GATT_PERM_WRITE, sizeof(rx_val), 0, rx_val}
    },
    /* TX char declaration */
    [3] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&chr_decl_uuid,      /* ← was &ESP_GATT_UUID_CHAR_DECLARE */
         ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(tx_prop),
         (uint8_t *)&tx_prop}
    },
    /* TX char value */
    [4] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&tx_uuid,
         ESP_GATT_PERM_READ, sizeof(tx_val), 0, tx_val}
    },
    /* TX CCCD (client enables notifications here) */
    [5] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&cccd_uuid,
         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
         sizeof(cccd_val), sizeof(cccd_val), cccd_val}
    },
};
#define GATT_DB_SIZE (sizeof(gatt_db) / sizeof(gatt_db[0]))
static uint16_t handle_table[GATT_DB_SIZE];

/* ── GATTS event callback ────────────────────────────── */
static void gatts_event_handler(esp_gatts_cb_event_t event,
                                 esp_gatt_if_t gatts_if,
                                 esp_ble_gatts_cb_param_t *param)
{
    switch (event) {

        case ESP_GATTS_REG_EVT:
            esp_ble_gap_set_device_name(DEVICE_NAME);
            esp_ble_gap_config_adv_data(&adv_data);
            esp_ble_gatts_create_attr_tab(gatt_db, gatts_if,
                                          GATT_DB_SIZE, 0);
            gatts_if_saved = gatts_if;
            break;

        case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            if (param->add_attr_tab.status == ESP_GATT_OK &&
                param->add_attr_tab.num_handle == GATT_DB_SIZE) {
                memcpy(handle_table, param->add_attr_tab.handles,
                       sizeof(handle_table));
                tx_char_handle = handle_table[4];
                tx_cccd_handle = handle_table[5];
                esp_ble_gatts_start_service(handle_table[0]);
                ESP_LOGI(TAG, "GATT table created, service started");
            }
            break;

        case ESP_GATTS_CONNECT_EVT:
            conn_id = param->connect.conn_id;
            ESP_LOGI(TAG, "Phone connected! conn_id=%d", conn_id);
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            conn_id        = 0xFFFF;
            notify_enabled = false;
            ESP_LOGI(TAG, "Phone disconnected — restarting advertising");
            esp_ble_gap_start_advertising(&adv_params);
            break;

        case ESP_GATTS_WRITE_EVT:
            if (param->write.handle == tx_cccd_handle) {
                /* Phone enabling/disabling notifications */
                notify_enabled = (param->write.value[0] == 0x01);
                ESP_LOGI(TAG, "Notifications %s",
                         notify_enabled ? "enabled" : "disabled");
                if (notify_enabled)
                    ble_send("ESP32-praveen Connected!\r\nCmds: LED_ON LED_OFF STATUS HELLO BLINK UPTIME\r\n");
            } else if (param->write.handle == handle_table[2]) {
                /* Data from phone */
                process_cmd(param->write.value, param->write.len);
            }
            break;

        default:
            break;
    }
}

/* ── GAP event callback ──────────────────────────────── */
static void gap_event_handler(esp_gap_ble_cb_event_t event,
                               esp_ble_gap_cb_param_t *param)
{
    if (event == ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT)
        esp_ble_gap_start_advertising(&adv_params);
}
static void uptime_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));

        if (conn_id == 0xFFFF || !notify_enabled)
            continue;

        char msg[40];
        uint32_t secs = xTaskGetTickCount() / configTICK_RATE_HZ;
        snprintf(msg, sizeof(msg), "UPTIME:%lus\r\n", (unsigned long)secs);
        ble_send(msg);
    }
}
/* ── app_main ───────────────────────────────────────── */
void app_main(void)
{
    printf("\n========================================\n");
    printf("  ESP32 BLE UART — iOS Compatible\n");
    printf("  Device: %s\n", DEVICE_NAME);
    printf("  App:    LightBlue (App Store, free)\n");
    printf("========================================\n\n");

    /* LED GPIO init */
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_PIN, 0);

    /* NVS init */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* Release Classic BT memory — BLE only */
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    /* BT controller init */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);

    /* Bluedroid init */
    esp_bluedroid_init();
    esp_bluedroid_enable();

    /* Register callbacks */
    esp_ble_gap_register_callback(gap_event_handler);
    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gatts_app_register(PROFILE_APP_ID);
    esp_ble_gatt_set_local_mtu(512);
    xTaskCreate(uptime_task, "uptime_task", 2048, NULL, 4, NULL);

    ESP_LOGI(TAG, "BLE advertising started — open LightBlue on iPhone");
}