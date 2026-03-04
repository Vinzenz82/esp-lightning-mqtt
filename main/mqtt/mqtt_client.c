#include "mqtt_client.h"

#include <inttypes.h>
#include <string.h>

#include <mqtt_client.h> /* IDF esp-mqtt — angle brackets skip current dir */

#include "app_config.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lightning/as3935.h"
#include "mqtt_payload.h"
#include "utils/nvs_config.h"
#include "wifi/wifi_manager.h"

static const char *TAG = "MQTT_CLIENT";

/* Module state */
static esp_mqtt_client_handle_t s_client;
static app_runtime_config_t    *s_cfg;
static QueueHandle_t            s_event_queue;
static char                     s_device_id[16];
static char                     s_base_topic[128]; /* {cfg.mqtt_base_topic}/{device_id} */

/* Derived topic strings */
static char s_topic_lightning[160];
static char s_topic_noise[160];
static char s_topic_disturber[160];
static char s_topic_status[160];
static char s_topic_config_set[160];
static char s_topic_config_get[160];

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

static void build_device_id(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_device_id, sizeof(s_device_id), "c3-%02x%02x%02x%02x",
             mac[2], mac[3], mac[4], mac[5]);
}

static void build_topics(void)
{
    snprintf(s_base_topic, sizeof(s_base_topic), "%s/%s",
             s_cfg->mqtt_base_topic, s_device_id);

    snprintf(s_topic_lightning,  sizeof(s_topic_lightning),  "%s/lightning",   s_base_topic);
    snprintf(s_topic_noise,      sizeof(s_topic_noise),      "%s/noise",       s_base_topic);
    snprintf(s_topic_disturber,  sizeof(s_topic_disturber),  "%s/disturber",   s_base_topic);
    snprintf(s_topic_status,     sizeof(s_topic_status),     "%s/status",      s_base_topic);
    snprintf(s_topic_config_set, sizeof(s_topic_config_set), "%s/config/set",  s_base_topic);
    snprintf(s_topic_config_get, sizeof(s_topic_config_get), "%s/config/get",  s_base_topic);
}

static int get_rssi(void)
{
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return 0;
}

static void publish_status(bool online)
{
    char buf[64];
    int  len = mqtt_payload_build_status(online, buf, sizeof(buf));
    if (len > 0) {
        esp_mqtt_client_publish(s_client, s_topic_status, buf, len,
                                1 /* QoS */, 1 /* retain */);
    }
}

static void publish_config(void)
{
    char buf[MQTT_PAYLOAD_BUF_SIZE];
    int  len = mqtt_payload_build_config(
        s_cfg->indoor_mode, s_cfg->noise_floor, s_cfg->watchdog,
        s_cfg->spike_rejection, s_cfg->min_strikes, s_cfg->disturber_mask,
        buf, sizeof(buf));
    if (len > 0) {
        esp_mqtt_client_publish(s_client, s_topic_config_get, buf, len,
                                1 /* QoS */, 1 /* retain */);
    }
}

/* Apply a partial JSON config/set payload to the runtime config and
 * re-configure the AS3935 for changed fields. */
static void handle_config_set(const char *data, int data_len)
{
    char *json_str = malloc(data_len + 1);
    if (!json_str) {
        ESP_LOGE(TAG, "OOM in handle_config_set");
        return;
    }
    memcpy(json_str, data, data_len);
    json_str[data_len] = '\0';

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) {
        ESP_LOGW(TAG, "config/set: invalid JSON");
        return;
    }

    bool changed = false;

    cJSON *item;

    item = cJSON_GetObjectItemCaseSensitive(root, "indoor_mode");
    if (cJSON_IsBool(item)) {
        s_cfg->indoor_mode = cJSON_IsTrue(item);
        as3935_set_indoor_mode(s_cfg->indoor_mode);
        changed = true;
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "noise_floor");
    if (cJSON_IsNumber(item)) {
        s_cfg->noise_floor = (uint8_t)item->valueint;
        as3935_set_noise_floor((as3935_noise_floor_t)s_cfg->noise_floor);
        changed = true;
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "watchdog");
    if (cJSON_IsNumber(item)) {
        s_cfg->watchdog = (uint8_t)item->valueint;
        as3935_set_watchdog((as3935_watchdog_t)s_cfg->watchdog);
        changed = true;
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "spike_rejection");
    if (cJSON_IsNumber(item)) {
        s_cfg->spike_rejection = (uint8_t)item->valueint;
        as3935_set_spike_rejection(
            (as3935_spike_rejection_t)s_cfg->spike_rejection);
        changed = true;
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "min_strikes");
    if (cJSON_IsNumber(item)) {
        s_cfg->min_strikes = (uint8_t)item->valueint;
        as3935_set_min_strikes(s_cfg->min_strikes);
        changed = true;
    }

    item = cJSON_GetObjectItemCaseSensitive(root, "disturber_mask");
    if (cJSON_IsBool(item)) {
        s_cfg->disturber_mask = cJSON_IsTrue(item);
        as3935_set_disturber_mask(s_cfg->disturber_mask);
        changed = true;
    }

    cJSON_Delete(root);

    if (changed) {
        nvs_config_save(s_cfg);
        publish_config();
        ESP_LOGI(TAG, "config/set applied and persisted");
    }
}

/* -----------------------------------------------------------------------
 * MQTT event handler (runs in esp-mqtt internal task)
 * ----------------------------------------------------------------------- */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected to broker");
            esp_mqtt_client_subscribe(s_client, s_topic_config_set, 1);
            publish_status(true);
            publish_config();
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected from broker");
            break;

        case MQTT_EVENT_DATA:
            if (event->topic_len > 0 &&
                strncmp(event->topic, s_topic_config_set,
                        event->topic_len) == 0) {
                handle_config_set(event->data, event->data_len);
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error type=%d",
                     event->error_handle->error_type);
            break;

        default:
            break;
    }
}

/* -----------------------------------------------------------------------
 * Publish task — dequeues as3935_data_t and publishes JSON
 * ----------------------------------------------------------------------- */
static void mqtt_publish_task(void *pvParameters)
{
    /* Wait for Wi-Fi before doing anything */
    EventGroupHandle_t wifi_eg = wifi_manager_get_event_group();
    xEventGroupWaitBits(wifi_eg, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
                        portMAX_DELAY);

    /* Start the MQTT client after Wi-Fi is up */
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_client));

    as3935_data_t event;

    while (true) {
        if (xQueueReceive(s_event_queue, &event,
                          pdMS_TO_TICKS(1000)) != pdTRUE) {
            continue;
        }

        char buf[MQTT_PAYLOAD_BUF_SIZE];
        int  rssi = get_rssi();
        int  len  = -1;
        const char *topic = NULL;

        switch (event.event) {
            case AS3935_EVENT_LIGHTNING:
                len   = mqtt_payload_build_lightning(&event, s_device_id,
                                                      rssi, buf, sizeof(buf));
                topic = s_topic_lightning;
                break;
            case AS3935_EVENT_NOISE:
                len   = mqtt_payload_build_noise(&event, s_device_id, rssi,
                                                  buf, sizeof(buf));
                topic = s_topic_noise;
                break;
            case AS3935_EVENT_DISTURBER:
                len   = mqtt_payload_build_disturber(&event, s_device_id,
                                                      rssi, buf, sizeof(buf));
                topic = s_topic_disturber;
                break;
            default:
                continue;
        }

        if (len > 0 && topic) {
            int msg_id = esp_mqtt_client_publish(s_client, topic, buf, len,
                                                  1 /* QoS 1 */,
                                                  0 /* not retained */);
            if (msg_id < 0) {
                ESP_LOGW(TAG, "publish failed for topic %s", topic);
            }
        }
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

esp_err_t mqtt_client_start(app_runtime_config_t *cfg,
                             QueueHandle_t          event_queue)
{
    s_cfg         = cfg;
    s_event_queue = event_queue;

    build_device_id();
    build_topics();

    ESP_LOGI(TAG, "Device ID: %s  Base topic: %s", s_device_id, s_base_topic);

    /* Build LWT offline payload */
    char lwt_buf[64];
    mqtt_payload_build_status(false, lwt_buf, sizeof(lwt_buf));

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_MQTT_BROKER_URL,
        .credentials = {
            .username = CONFIG_MQTT_USERNAME[0] ? CONFIG_MQTT_USERNAME : NULL,
            .authentication.password =
                CONFIG_MQTT_PASSWORD[0] ? CONFIG_MQTT_PASSWORD : NULL,
        },
        .session = {
            .last_will = {
                .topic   = s_topic_status,
                .msg     = lwt_buf,
                .qos     = 1,
                .retain  = 1,
            },
        },
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));

    /* Spawn publish task — it waits for Wi-Fi internally */
    BaseType_t rc = xTaskCreate(mqtt_publish_task, "mqtt_task",
                                 MQTT_TASK_STACK, NULL, MQTT_TASK_PRIO, NULL);
    if (rc != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate mqtt_task failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "MQTT client initialised");
    return ESP_OK;
}
