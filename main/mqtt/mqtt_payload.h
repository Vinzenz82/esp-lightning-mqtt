#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lightning/as3935.h"

/* -----------------------------------------------------------------------
 * mqtt_payload — JSON payload builder using cJSON
 *
 * All functions write into caller-supplied buffers.
 * Returns the number of bytes written (excluding NUL), or -1 on error.
 * ----------------------------------------------------------------------- */

/* Build lightning event payload.
 * device_id: "c3-aabbccdd" style string
 * rssi:      current Wi-Fi RSSI in dBm */
int mqtt_payload_build_lightning(const as3935_data_t *data,
                                  const char          *device_id,
                                  int rssi, char *buf, size_t buf_len);

/* Build noise event payload. */
int mqtt_payload_build_noise(const as3935_data_t *data,
                              const char *device_id, int rssi,
                              char *buf, size_t buf_len);

/* Build disturber event payload. */
int mqtt_payload_build_disturber(const as3935_data_t *data,
                                  const char *device_id, int rssi,
                                  char *buf, size_t buf_len);

/* Build status payload: {"state":"online"} or {"state":"offline"} */
int mqtt_payload_build_status(bool online, char *buf, size_t buf_len);

/* Build config/get payload from runtime config. */
int mqtt_payload_build_config(bool indoor_mode, uint8_t noise_floor,
                               uint8_t watchdog, uint8_t spike_rejection,
                               uint8_t min_strikes, bool disturber_mask,
                               char *buf, size_t buf_len);
