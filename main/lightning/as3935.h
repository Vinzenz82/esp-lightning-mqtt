#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

/* -----------------------------------------------------------------------
 * AS3935 Franklin Lightning Sensor — I2C driver
 *
 * Rules:
 *   - All register access through as3935_reg_read / as3935_reg_write_masked
 *   - No vTaskDelay inside driver; caller is responsible for timing
 *   - Thread-safe via internal mutex; caller need not serialize calls
 *   - Returns ESP_ERR_INVALID_RESPONSE if readback does not match write
 * ----------------------------------------------------------------------- */

/* --- Enumerations ------------------------------------------------------ */

typedef enum {
    AS3935_NOISE_FLOOR_390UV = 0,
    AS3935_NOISE_FLOOR_630UV = 1,
    AS3935_NOISE_FLOOR_860UV = 2,
    AS3935_NOISE_FLOOR_1100UV = 3,
    AS3935_NOISE_FLOOR_1140UV = 4,
    AS3935_NOISE_FLOOR_1570UV = 5,
    AS3935_NOISE_FLOOR_1800UV = 6,
    AS3935_NOISE_FLOOR_2000UV = 7,
} as3935_noise_floor_t;

typedef enum {
    AS3935_WATCHDOG_SENSITIVITY_1  = 1,
    AS3935_WATCHDOG_SENSITIVITY_2  = 2,
    AS3935_WATCHDOG_SENSITIVITY_3  = 3,
    AS3935_WATCHDOG_SENSITIVITY_4  = 4,
    AS3935_WATCHDOG_SENSITIVITY_5  = 5,
    AS3935_WATCHDOG_SENSITIVITY_6  = 6,
    AS3935_WATCHDOG_SENSITIVITY_7  = 7,
    AS3935_WATCHDOG_SENSITIVITY_8  = 8,
    AS3935_WATCHDOG_SENSITIVITY_9  = 9,
    AS3935_WATCHDOG_SENSITIVITY_10 = 10,
} as3935_watchdog_t;

typedef enum {
    AS3935_SPIKE_REJECTION_1  = 1,
    AS3935_SPIKE_REJECTION_2  = 2,
    AS3935_SPIKE_REJECTION_3  = 3,
    AS3935_SPIKE_REJECTION_4  = 4,
    AS3935_SPIKE_REJECTION_5  = 5,
    AS3935_SPIKE_REJECTION_6  = 6,
    AS3935_SPIKE_REJECTION_7  = 7,
    AS3935_SPIKE_REJECTION_8  = 8,
    AS3935_SPIKE_REJECTION_9  = 9,
    AS3935_SPIKE_REJECTION_10 = 10,
    AS3935_SPIKE_REJECTION_11 = 11,
} as3935_spike_rejection_t;

typedef enum {
    AS3935_EVENT_NONE      = 0,
    AS3935_EVENT_NOISE     = 1,
    AS3935_EVENT_DISTURBER = 2,
    AS3935_EVENT_LIGHTNING = 3,
} as3935_event_t;

/* --- Data types -------------------------------------------------------- */

typedef struct {
    as3935_event_t event;
    uint8_t        distance_km; /* 1 = overhead, 63 = out of range */
    uint32_t       energy;      /* relative, no unit */
    int64_t        timestamp_us; /* esp_timer_get_time() */
} as3935_data_t;

/* --- Lifecycle --------------------------------------------------------- */

/* Initialise I2C bus on the given port (uses CONFIG_AS3935_SDA/SCL_GPIO)
 * and configure the IRQ GPIO as input (rising-edge trigger).
 * Does NOT install the ISR handler — that is done by lightning_task. */
esp_err_t as3935_init(i2c_port_t port, uint8_t addr, gpio_num_t irq_gpio);

/* Send factory-reset command (write 0x96 to reg 0x3C). */
esp_err_t as3935_reset(void);

/* Trigger RCO calibration and verify DONE bits.
 * Uses a 3 ms busy-wait internally (acceptable during init). */
esp_err_t as3935_calibrate_rco(void);

/* --- Configuration ----------------------------------------------------- */

esp_err_t as3935_set_indoor_mode(bool indoor);
esp_err_t as3935_set_noise_floor(as3935_noise_floor_t level);
esp_err_t as3935_set_watchdog(as3935_watchdog_t threshold);
esp_err_t as3935_set_spike_rejection(as3935_spike_rejection_t level);

/* count must be 1, 5, 9, or 16.  Returns ESP_ERR_INVALID_ARG otherwise. */
esp_err_t as3935_set_min_strikes(uint8_t count);

esp_err_t as3935_set_disturber_mask(bool masked);

/* --- Runtime ----------------------------------------------------------- */

/* Read interrupt source and event data.  Call after IRQ fires (≥2 ms
 * after the rising edge to let the sensor latch data). */
esp_err_t as3935_read_event(as3935_data_t *out);

/* Return actual noise floor threshold in µV for the current setting. */
esp_err_t as3935_get_noise_floor_actual(uint8_t *out_uv);

esp_err_t as3935_power_down(void);
esp_err_t as3935_power_up(void);

/* Return the stored IRQ GPIO number (for ISR handler registration). */
gpio_num_t as3935_get_irq_gpio(void);
