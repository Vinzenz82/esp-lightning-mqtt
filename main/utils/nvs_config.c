#include "nvs_config.h"

#include <string.h>

#include "app_config.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "NVS_CONFIG";

/* NVS keys */
#define KEY_INDOOR_MODE    "indoor_mode"
#define KEY_NOISE_FLOOR    "noise_floor"
#define KEY_WATCHDOG       "watchdog"
#define KEY_SPIKE_REJ      "spike_rej"
#define KEY_MIN_STRIKES    "min_strikes"
#define KEY_DIST_MASK      "dist_mask"
#define KEY_BASE_TOPIC     "base_topic"

static void fill_defaults(app_runtime_config_t *cfg)
{
    cfg->indoor_mode     = CONFIG_AS3935_INDOOR_MODE;
    cfg->noise_floor     = 2;
    cfg->watchdog        = 2;
    cfg->spike_rejection = 2;
    cfg->min_strikes     = 1;
    cfg->disturber_mask  = false;
    strncpy(cfg->mqtt_base_topic, CONFIG_MQTT_BASE_TOPIC,
            sizeof(cfg->mqtt_base_topic) - 1);
    cfg->mqtt_base_topic[sizeof(cfg->mqtt_base_topic) - 1] = '\0';
}

esp_err_t nvs_config_load(app_runtime_config_t *cfg)
{
    fill_defaults(cfg);

    nvs_handle_t handle;
    esp_err_t    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed (%s), using compile-time defaults",
                 esp_err_to_name(err));
        return ESP_OK;
    }

    uint8_t u8;
    size_t  len;
    bool    any_missing = false;

#define NVS_LOAD_U8(key, field)                                            \
    do {                                                                   \
        err = nvs_get_u8(handle, key, &u8);                               \
        if (err == ESP_OK) {                                               \
            cfg->field = u8;                                               \
        } else if (err == ESP_ERR_NVS_NOT_FOUND) {                        \
            any_missing = true;                                            \
        } else {                                                           \
            ESP_LOGW(TAG, "nvs_get_u8(%s) failed: %s", key,               \
                     esp_err_to_name(err));                                \
        }                                                                  \
    } while (0)

    NVS_LOAD_U8(KEY_INDOOR_MODE, indoor_mode);
    NVS_LOAD_U8(KEY_NOISE_FLOOR, noise_floor);
    NVS_LOAD_U8(KEY_WATCHDOG, watchdog);
    NVS_LOAD_U8(KEY_SPIKE_REJ, spike_rejection);
    NVS_LOAD_U8(KEY_MIN_STRIKES, min_strikes);
    NVS_LOAD_U8(KEY_DIST_MASK, disturber_mask);
#undef NVS_LOAD_U8

    len = sizeof(cfg->mqtt_base_topic);
    err = nvs_get_str(handle, KEY_BASE_TOPIC, cfg->mqtt_base_topic, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        any_missing = true;
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_get_str(%s) failed: %s", KEY_BASE_TOPIC,
                 esp_err_to_name(err));
    }

    nvs_close(handle);

    if (any_missing) {
        ESP_LOGI(TAG, "First boot — writing defaults to NVS");
        nvs_config_save(cfg);
    }

    return ESP_OK;
}

esp_err_t nvs_config_save(const app_runtime_config_t *cfg)
{
    nvs_handle_t handle;
    esp_err_t    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

#define NVS_SAVE_U8(key, val)                                              \
    do {                                                                   \
        err = nvs_set_u8(handle, key, (uint8_t)(val));                    \
        if (err != ESP_OK) {                                               \
            ESP_LOGW(TAG, "nvs_set_u8(%s) failed: %s", key,               \
                     esp_err_to_name(err));                                \
        }                                                                  \
    } while (0)

    NVS_SAVE_U8(KEY_INDOOR_MODE, cfg->indoor_mode);
    NVS_SAVE_U8(KEY_NOISE_FLOOR, cfg->noise_floor);
    NVS_SAVE_U8(KEY_WATCHDOG, cfg->watchdog);
    NVS_SAVE_U8(KEY_SPIKE_REJ, cfg->spike_rejection);
    NVS_SAVE_U8(KEY_MIN_STRIKES, cfg->min_strikes);
    NVS_SAVE_U8(KEY_DIST_MASK, cfg->disturber_mask);
#undef NVS_SAVE_U8

    err = nvs_set_str(handle, KEY_BASE_TOPIC, cfg->mqtt_base_topic);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_set_str(%s) failed: %s", KEY_BASE_TOPIC,
                 esp_err_to_name(err));
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
    }
    return err;
}
