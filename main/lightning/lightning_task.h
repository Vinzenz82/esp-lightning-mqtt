#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "utils/nvs_config.h"

/* -----------------------------------------------------------------------
 * lightning_task — FreeRTOS task that drives the AS3935 sensor.
 *
 * Architecture:
 *   - Installs GPIO ISR service and registers handler for the AS3935 IRQ
 *   - ISR uses xTaskNotifyFromISR (no I2C in ISR)
 *   - On notification: waits 2 ms, calls as3935_read_event(), enqueues
 *     as3935_data_t on event_queue
 * ----------------------------------------------------------------------- */

/* Create and start the lightning task.
 * event_queue must have depth ≥ LIGHTNING_QUEUE_DEPTH and accept
 * as3935_data_t items. */
esp_err_t lightning_task_start(const app_runtime_config_t *cfg,
                                QueueHandle_t               event_queue);
