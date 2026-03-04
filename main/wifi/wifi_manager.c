#include "wifi_manager.h"

#include "app_config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "WIFI_MGR";

static EventGroupHandle_t s_wifi_event_group;
static int                s_retry_count = 0;

/* -----------------------------------------------------------------------
 * SNTP initialisation (called once after first IP address obtained)
 * ----------------------------------------------------------------------- */
static void start_sntp(void)
{
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP initialised");
}

/* -----------------------------------------------------------------------
 * Event handlers
 * ----------------------------------------------------------------------- */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi started, connecting to %s", CONFIG_WIFI_SSID);
        esp_wifi_connect();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT);
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        s_retry_count++;
        if (s_retry_count > WIFI_RETRY_MAX_COUNT) {
            ESP_LOGE(TAG, "Too many Wi-Fi failures (%d), rebooting",
                     s_retry_count);
            esp_restart();
        }

        /* Exponential backoff clamped to WIFI_RETRY_MAX_S */
        int delay_s = WIFI_RETRY_INITIAL_S;
        for (int i = 1; i < s_retry_count; i++) {
            delay_s *= 2;
            if (delay_s > WIFI_RETRY_MAX_S) {
                delay_s = WIFI_RETRY_MAX_S;
                break;
            }
        }
        ESP_LOGI(TAG, "Disconnected — retry %d/%d in %d s", s_retry_count,
                 WIFI_RETRY_MAX_COUNT, delay_s);
        vTaskDelay(pdMS_TO_TICKS(delay_s * 1000));
        esp_wifi_connect();
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        xEventGroupClearBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        static bool sntp_started = false;
        if (!sntp_started) {
            start_sntp();
            sntp_started = true;
        }
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */
esp_err_t wifi_manager_start(void)
{
    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, NULL, NULL));

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid     = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi manager started, connecting to SSID: %s",
             CONFIG_WIFI_SSID);
    return ESP_OK;
}

EventGroupHandle_t wifi_manager_get_event_group(void)
{
    return s_wifi_event_group;
}
