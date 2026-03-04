#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* -----------------------------------------------------------------------
 * app_runtime_config_t — all AS3935 tunable parameters + MQTT base topic.
 * Persisted in NVS; Kconfig defaults used on first boot.
 * ----------------------------------------------------------------------- */
typedef struct {
    bool    indoor_mode;
    uint8_t noise_floor;     /* 0–7 */
    uint8_t watchdog;        /* 1–10 */
    uint8_t spike_rejection; /* 1–11 */
    uint8_t min_strikes;     /* 1, 5, 9, or 16 */
    bool    disturber_mask;
    char    mqtt_base_topic[64];
} app_runtime_config_t;

/* Load config from NVS.  On first boot (NVS empty) writes Kconfig
 * defaults and returns ESP_OK.  On NVS read error logs a warning and
 * fills *cfg with compile-time defaults. */
esp_err_t nvs_config_load(app_runtime_config_t *cfg);

/* Persist *cfg to NVS.  Returns ESP_OK on success. */
esp_err_t nvs_config_save(const app_runtime_config_t *cfg);
