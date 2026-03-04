#include "lightning_task.h"

#include "app_config.h"
#include "as3935.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "LIGHTNING_TASK";

/* Context passed to the FreeRTOS task */
typedef struct {
    const app_runtime_config_t *cfg;
    QueueHandle_t               event_queue;
} task_ctx_t;

static task_ctx_t s_ctx;

/* -----------------------------------------------------------------------
 * ISR — runs in IRAM, no I2C, no printf
 * ----------------------------------------------------------------------- */
static void IRAM_ATTR lightning_isr_handler(void *arg)
{
    TaskHandle_t   task                 = (TaskHandle_t)arg;
    BaseType_t higher_priority_woken = pdFALSE;
    xTaskNotifyFromISR(task, 0, eNoAction, &higher_priority_woken);
    portYIELD_FROM_ISR(higher_priority_woken);
}

/* -----------------------------------------------------------------------
 * Task body
 * ----------------------------------------------------------------------- */
static void lightning_task(void *pvParameters)
{
    task_ctx_t *ctx = (task_ctx_t *)pvParameters;

    /* Configure AS3935 according to runtime config */
    esp_err_t err;

    err = as3935_reset();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "as3935_reset failed: %s", esp_err_to_name(err));
        
        /* wait 5 secs before reset */
        vTaskDelay(pdMS_TO_TICKS(5000));

        abort(); /* Sensor is mandatory */
    }
    /* Allow reset to settle */
    vTaskDelay(pdMS_TO_TICKS(2));

    err = as3935_calibrate_rco();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RCO calibration issue, continuing with defaults");
    }

    err = as3935_set_indoor_mode(ctx->cfg->indoor_mode);
    if (err != ESP_OK) ESP_LOGW(TAG, "set_indoor_mode: %s", esp_err_to_name(err));

    err = as3935_set_noise_floor((as3935_noise_floor_t)ctx->cfg->noise_floor);
    if (err != ESP_OK) ESP_LOGW(TAG, "set_noise_floor: %s", esp_err_to_name(err));

    err = as3935_set_watchdog((as3935_watchdog_t)ctx->cfg->watchdog);
    if (err != ESP_OK) ESP_LOGW(TAG, "set_watchdog: %s", esp_err_to_name(err));

    err = as3935_set_spike_rejection(
        (as3935_spike_rejection_t)ctx->cfg->spike_rejection);
    if (err != ESP_OK) ESP_LOGW(TAG, "set_spike_rejection: %s", esp_err_to_name(err));

    err = as3935_set_min_strikes(ctx->cfg->min_strikes);
    if (err != ESP_OK) ESP_LOGW(TAG, "set_min_strikes: %s", esp_err_to_name(err));

    err = as3935_set_disturber_mask(ctx->cfg->disturber_mask);
    if (err != ESP_OK) ESP_LOGW(TAG, "set_disturber_mask: %s", esp_err_to_name(err));

    ESP_LOGI(TAG, "AS3935 configured. Waiting for events.");

    while (true) {
        /* Block until ISR notifies us */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Wait ≥2 ms after rising edge before reading (AS3935 requirement) */
        vTaskDelay(pdMS_TO_TICKS(2));

        as3935_data_t data;
        err = as3935_read_event(&data);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "read_event failed: %s", esp_err_to_name(err));
            continue;
        }

        if (data.event == AS3935_EVENT_NONE) {
            continue;
        }

        /* Enqueue; if full, drop oldest and log */
        if (xQueueSend(ctx->event_queue, &data, 0) != pdTRUE) {
            as3935_data_t discarded;
            xQueueReceive(ctx->event_queue, &discarded, 0);
            xQueueSend(ctx->event_queue, &data, 0);
            ESP_LOGW(TAG, "Event queue full — oldest event dropped");
        }
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

esp_err_t lightning_task_start(const app_runtime_config_t *cfg,
                                QueueHandle_t               event_queue)
{
    /* Initialise AS3935 over I2C */
    esp_err_t err = as3935_init((i2c_port_t)AS3935_I2C_PORT, AS3935_I2C_ADDR,
                                 (gpio_num_t)AS3935_IRQ_GPIO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "as3935_init failed: %s", esp_err_to_name(err));
        abort(); /* Sensor is mandatory */
    }

    s_ctx.cfg         = cfg;
    s_ctx.event_queue = event_queue;

    /* Create the FreeRTOS task first so we have its handle for the ISR */
    TaskHandle_t task_handle = NULL;
    BaseType_t   rc = xTaskCreate(lightning_task, "lightning_task",
                                  LIGHTNING_TASK_STACK, &s_ctx,
                                  LIGHTNING_TASK_PRIO, &task_handle);
    if (rc != pdPASS || !task_handle) {
        ESP_LOGE(TAG, "xTaskCreate failed");
        return ESP_ERR_NO_MEM;
    }

    /* Install GPIO ISR service (safe to call multiple times) */
    gpio_install_isr_service(0);

    /* Register ISR for the AS3935 IRQ pin */
    err = gpio_isr_handler_add(as3935_get_irq_gpio(), lightning_isr_handler,
                               (void *)task_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_isr_handler_add failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Lightning task started");
    return ESP_OK;
}
