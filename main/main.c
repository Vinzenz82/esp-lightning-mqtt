#include <stdint.h>

#include "app_config.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lightning/as3935.h"
#include "lightning/lightning_task.h"
#include "mqtt/mqtt_client.h"
#include "nvs_flash.h"
#include "utils/nvs_config.h"
#include "wifi/wifi_manager.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    /* --- NVS flash init ------------------------------------------------ */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition erased and re-initialised");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* --- Load runtime config from NVS (Kconfig defaults on first boot) - */
    static app_runtime_config_t cfg;
    ESP_ERROR_CHECK(nvs_config_load(&cfg));
    ESP_LOGI(TAG, "Config loaded: indoor=%d nf=%u wd=%u srej=%u min_str=%u "
             "distmask=%d topic=%s",
             cfg.indoor_mode, cfg.noise_floor, cfg.watchdog,
             cfg.spike_rejection, cfg.min_strikes, cfg.disturber_mask,
             cfg.mqtt_base_topic);

    /* --- Event queue shared between lightning_task and mqtt_task -------- */
    QueueHandle_t event_queue =
        xQueueCreate(LIGHTNING_QUEUE_DEPTH, sizeof(as3935_data_t));
    if (!event_queue) {
        ESP_LOGE(TAG, "Failed to create event queue");
        abort();
    }

    /* --- Start Wi-Fi --------------------------------------------------- */
    ESP_ERROR_CHECK(wifi_manager_start());

    /* --- Start MQTT ---------------------------------------------------- */
    ESP_ERROR_CHECK(mqtt_client_start(&cfg, event_queue));

    /* --- Start lightning sensor task ------------------------------------ */
    ESP_ERROR_CHECK(lightning_task_start(&cfg, event_queue));

    ESP_LOGI(TAG, "All subsystems started");
    /* app_main returns; scheduler keeps all tasks running */
}
