#pragma once

/* -----------------------------------------------------------------------
 * app_config.h — Compile-time configuration (single source of truth)
 * All GPIO / task / queue parameters live here.
 * Runtime-tunable values live in nvs_config.h (app_runtime_config_t).
 * ----------------------------------------------------------------------- */

#include "sdkconfig.h"

/* --- FreeRTOS task stack sizes (bytes) -------------------------------- */
#define LIGHTNING_TASK_STACK 4096
#define MQTT_TASK_STACK      6144
#define WIFI_TASK_STACK      8192

/* --- FreeRTOS task priorities ----------------------------------------- */
#define LIGHTNING_TASK_PRIO 5
#define MQTT_TASK_PRIO      4
#define WIFI_TASK_PRIO      3

/* --- Event queue depth ------------------------------------------------- */
#define LIGHTNING_QUEUE_DEPTH 8

/* --- AS3935 hardware config (from Kconfig) ----------------------------- */
#define AS3935_I2C_PORT CONFIG_AS3935_I2C_PORT
#define AS3935_I2C_ADDR CONFIG_AS3935_I2C_ADDR
#define AS3935_SDA_GPIO CONFIG_AS3935_SDA_GPIO
#define AS3935_SCL_GPIO CONFIG_AS3935_SCL_GPIO
#define AS3935_IRQ_GPIO CONFIG_AS3935_IRQ_GPIO

/* --- I2C bus speed ----------------------------------------------------- */
#define AS3935_I2C_SPEED_HZ 100000 /* 100 kHz standard mode */

/* --- Wi-Fi reconnect strategy ----------------------------------------- */
#define WIFI_RETRY_INITIAL_S  1
#define WIFI_RETRY_MAX_S      60
#define WIFI_RETRY_MAX_COUNT  10

/* --- MQTT topics ------------------------------------------------------- */
#define MQTT_BASE_TOPIC CONFIG_MQTT_BASE_TOPIC

/* --- NVS namespace ----------------------------------------------------- */
#define NVS_NAMESPACE "lightning"

/* --- MQTT payload buffer size ------------------------------------------ */
#define MQTT_PAYLOAD_BUF_SIZE 512
