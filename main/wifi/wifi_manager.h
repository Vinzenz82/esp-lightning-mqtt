#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

/* -----------------------------------------------------------------------
 * wifi_manager — Wi-Fi STA mode with exponential-backoff reconnect
 *
 * Reconnect strategy:
 *   - Exponential backoff: 1 s → 2 s → 4 s → ... → max 60 s
 *   - After 10 consecutive failures: esp_restart()
 *
 * Synchronisation:
 *   - wifi_manager_get_event_group() returns an EventGroupHandle_t
 *   - WIFI_CONNECTED_BIT is set when IP address obtained
 *   - WIFI_DISCONNECTED_BIT is set when connection is lost
 * ----------------------------------------------------------------------- */

#define WIFI_CONNECTED_BIT    BIT0
#define WIFI_DISCONNECTED_BIT BIT1

/* Initialise TCP/IP stack, create Wi-Fi station, and start connecting.
 * Spawns an internal FreeRTOS task for reconnect logic. */
esp_err_t wifi_manager_start(void);

/* Return the EventGroup used for Wi-Fi status synchronisation. */
EventGroupHandle_t wifi_manager_get_event_group(void);
