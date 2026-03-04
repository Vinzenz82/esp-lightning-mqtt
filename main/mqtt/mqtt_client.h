#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "utils/nvs_config.h"

/* -----------------------------------------------------------------------
 * mqtt_client — MQTT wrapper around esp-mqtt
 *
 * - Waits for WIFI_CONNECTED_BIT before connecting to the broker
 * - Sets LWT: {base}/{device_id}/status  {"state":"offline"} retained QoS 1
 * - Publishes {"state":"online"} on connect
 * - Subscribes to {base}/{device_id}/config/set
 * - Spawns mqtt_publish_task that dequeues as3935_data_t and publishes
 *   lightning / noise / disturber events at QoS 1
 * - Publishes {base}/{device_id}/config/get (retained) on connect and
 *   after each successful config/set update
 * ----------------------------------------------------------------------- */

/* Initialise and start the MQTT subsystem.
 * cfg is used to build topics and apply config/set updates;
 * event_queue carries as3935_data_t items from lightning_task. */
esp_err_t mqtt_client_start(app_runtime_config_t *cfg,
                             QueueHandle_t          event_queue);
