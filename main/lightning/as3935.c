#include "as3935.h"

#include <inttypes.h>
#include <string.h>

#include "app_config.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "AS3935";

/* --- AS3935 register addresses ---------------------------------------- */
#define REG_AFE_GAIN      0x00
#define REG_THRESHOLD     0x01
#define REG_LIGHTNING_REG 0x02
#define REG_INT_MASK_ANT  0x03
#define REG_ENERGY_1      0x04
#define REG_ENERGY_2      0x05
#define REG_ENERGY_3      0x06
#define REG_DISTANCE      0x07
#define REG_DISP_LCO      0x08
#define REG_CALIB_TRCO    0x3A
#define REG_CALIB_SRCO    0x3B
#define REG_PRESET_DEFAULT 0x3C
#define REG_CALIB_RCO     0x3D

/* Register field masks */
#define AFE_GAIN_PWD_MASK   0x01
#define AFE_GAIN_GB_MASK    0x3E /* bits [5:1] */
#define AFE_GAIN_INDOOR     (0x12 << 1) /* AFE_GB = 0b10010 */
#define AFE_GAIN_OUTDOOR    (0x0E << 1) /* AFE_GB = 0b01110 */

#define THRESHOLD_WDTH_MASK  0x0F /* bits [3:0] */
#define THRESHOLD_NFLEV_MASK 0x70 /* bits [6:4] */
#define THRESHOLD_NFLEV_SHIFT 4

#define LIGHTNING_SREJ_MASK   0x0F /* bits [3:0] */
#define LIGHTNING_MINNUM_MASK 0x30 /* bits [5:4] */
#define LIGHTNING_MINNUM_SHIFT 4

#define INT_MASK_ANT_INT_MASK  0x0F /* bits [3:0] */
#define INT_MASK_ANT_MASKD_BIT 0x20 /* bit 5 */

#define INT_NOISE     0x01
#define INT_DISTURBER 0x04
#define INT_LIGHTNING 0x08

#define DISTANCE_MASK 0x3F /* bits [5:0] */

#define CALIB_DONE_BIT 0x40 /* bit 6 */
#define CALIB_NOK_BIT  0x80 /* bit 7 */

#define PRESET_RESET_CMD  0x96
#define CALIB_TRIGGER_CMD 0x96

#define I2C_TIMEOUT_MS 50

/* --- Module state ----------------------------------------------------- */
static i2c_master_bus_handle_t s_bus_handle;
static i2c_master_dev_handle_t s_dev_handle;
static gpio_num_t              s_irq_gpio;
static SemaphoreHandle_t       s_mutex;

/* -----------------------------------------------------------------------
 * Private: low-level register I/O
 * ----------------------------------------------------------------------- */

static esp_err_t as3935_reg_read(uint8_t reg, uint8_t *out)
{
    return i2c_master_transmit_receive(s_dev_handle, &reg, 1, out, 1,
                                       I2C_TIMEOUT_MS);
}

/* Write value to register with retry on transient I2C errors.
 * Readback verification is skipped for write-only registers (0x3C, 0x3D). */
static esp_err_t as3935_reg_write(uint8_t reg, uint8_t value)
{
    uint8_t   buf[2] = {reg, value};
    esp_err_t err;

    for (int attempt = 0; attempt < 3; attempt++) {
        err = i2c_master_transmit(s_dev_handle, buf, sizeof(buf),
                                  I2C_TIMEOUT_MS);
        if (err == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "i2c write reg=0x%02X attempt %d failed: %s", reg,
                 attempt + 1, esp_err_to_name(err));
    }
    return err;
}

/* Read-modify-write with mask; verifies readback unless skip_verify=true. */
static esp_err_t as3935_reg_write_masked(uint8_t reg, uint8_t mask,
                                          uint8_t value,
                                          bool    skip_verify)
{
    uint8_t   current = 0;
    esp_err_t err;

    for (int attempt = 0; attempt < 3; attempt++) {
        err = as3935_reg_read(reg, &current);
        if (err == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG, "reg_read 0x%02X attempt %d: %s", reg, attempt + 1,
                 esp_err_to_name(err));
    }
    if (err != ESP_OK) {
        return err;
    }

    uint8_t new_val = (current & ~mask) | (value & mask);
    err = as3935_reg_write(reg, new_val);
    if (err != ESP_OK) {
        return err;
    }

    if (!skip_verify) {
        uint8_t readback = 0;
        err = as3935_reg_read(reg, &readback);
        if (err != ESP_OK) {
            return err;
        }
        if ((readback & mask) != (new_val & mask)) {
            ESP_LOGE(TAG,
                     "Readback mismatch reg=0x%02X mask=0x%02X "
                     "wrote=0x%02X got=0x%02X",
                     reg, mask, new_val, readback);
            return ESP_ERR_INVALID_RESPONSE;
        }
    }

    return ESP_OK;
}

/* -----------------------------------------------------------------------
 * Lifecycle
 * ----------------------------------------------------------------------- */

esp_err_t as3935_init(i2c_port_t port, uint8_t addr, gpio_num_t irq_gpio)
{
    s_irq_gpio = irq_gpio;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Initialise I2C master bus */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port        = port,
        .sda_io_num      = AS3935_SDA_GPIO,
        .scl_io_num      = AS3935_SCL_GPIO,
        .clk_source      = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false, /* external 4.7 kΩ pull-ups */
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Add AS3935 device */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = AS3935_I2C_SPEED_HZ,
    };
    err = i2c_master_bus_add_device(s_bus_handle, &dev_cfg, &s_dev_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    /* Reset the bus to release any slave stuck mid-transaction from boot-time
     * GPIO toggling, then wait 2 ms for slaves to recover. */
    i2c_master_bus_reset(s_bus_handle);
    vTaskDelay(pdMS_TO_TICKS(2));

    /* Verify device is present before proceeding — gives a clear diagnostic
     * instead of a confusing NACK buried in the reset retry loop. */
    err = i2c_master_probe(s_bus_handle, addr, I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "AS3935 not found at 0x%02X — check wiring and ADD pins "
                 "(%s)",
                 addr, esp_err_to_name(err));
        return ESP_ERR_NOT_FOUND;
    }

    /* Configure IRQ GPIO as input, rising-edge trigger, no internal pull */
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << irq_gpio),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_POSEDGE,
    };
    err = gpio_config(&io_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config IRQ failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Initialised: port=%d addr=0x%02X irq=%d", port, addr,
             irq_gpio);
    return ESP_OK;
}

esp_err_t as3935_reset(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    /* Write-only register — no readback verify */
    esp_err_t err = as3935_reg_write(REG_PRESET_DEFAULT, PRESET_RESET_CMD);
    xSemaphoreGive(s_mutex);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "reset failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t as3935_calibrate_rco(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* Trigger calibration */
    esp_err_t err = as3935_reg_write(REG_CALIB_RCO, CALIB_TRIGGER_CMD);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "calibrate_rco trigger failed: %s",
                 esp_err_to_name(err));
        xSemaphoreGive(s_mutex);
        return err;
    }

    /* Wait ≥2 ms for calibration to complete (busy-wait; init only) */
    esp_rom_delay_us(3000);

    /* Verify TRCO calibration done */
    uint8_t trco = 0;
    err = as3935_reg_read(REG_CALIB_TRCO, &trco);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "read CALIB_TRCO failed: %s", esp_err_to_name(err));
    } else if (!(trco & CALIB_DONE_BIT)) {
        ESP_LOGW(TAG, "TRCO calibration not done (reg=0x%02X)", trco);
        err = ESP_ERR_TIMEOUT;
    } else if (trco & CALIB_NOK_BIT) {
        ESP_LOGW(TAG, "TRCO calibration failed (NOK set, reg=0x%02X)", trco);
        err = ESP_FAIL;
    }

    /* Verify SRCO calibration done */
    uint8_t srco = 0;
    esp_err_t srco_err = as3935_reg_read(REG_CALIB_SRCO, &srco);
    if (srco_err != ESP_OK) {
        ESP_LOGW(TAG, "read CALIB_SRCO failed: %s",
                 esp_err_to_name(srco_err));
    } else if (!(srco & CALIB_DONE_BIT)) {
        ESP_LOGW(TAG, "SRCO calibration not done (reg=0x%02X)", srco);
    }

    xSemaphoreGive(s_mutex);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "RCO calibration OK");
    } else {
        ESP_LOGW(TAG, "RCO calibration completed with warnings, continuing");
        err = ESP_OK; /* Non-fatal per spec */
    }
    return err;
}

/* -----------------------------------------------------------------------
 * Configuration
 * ----------------------------------------------------------------------- */

esp_err_t as3935_set_indoor_mode(bool indoor)
{
    uint8_t gain = indoor ? AFE_GAIN_INDOOR : AFE_GAIN_OUTDOOR;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = as3935_reg_write_masked(REG_AFE_GAIN, AFE_GAIN_GB_MASK,
                                            gain, false);
    xSemaphoreGive(s_mutex);
    return err;
}

esp_err_t as3935_set_noise_floor(as3935_noise_floor_t level)
{
    uint8_t val = ((uint8_t)level << THRESHOLD_NFLEV_SHIFT)
                  & THRESHOLD_NFLEV_MASK;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = as3935_reg_write_masked(REG_THRESHOLD,
                                            THRESHOLD_NFLEV_MASK, val, false);
    xSemaphoreGive(s_mutex);
    return err;
}

esp_err_t as3935_set_watchdog(as3935_watchdog_t threshold)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = as3935_reg_write_masked(REG_THRESHOLD,
                                            THRESHOLD_WDTH_MASK,
                                            (uint8_t)threshold, false);
    xSemaphoreGive(s_mutex);
    return err;
}

esp_err_t as3935_set_spike_rejection(as3935_spike_rejection_t level)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = as3935_reg_write_masked(REG_LIGHTNING_REG,
                                            LIGHTNING_SREJ_MASK,
                                            (uint8_t)level, false);
    xSemaphoreGive(s_mutex);
    return err;
}

esp_err_t as3935_set_min_strikes(uint8_t count)
{
    uint8_t reg_val;
    switch (count) {
        case 1:  reg_val = 0; break;
        case 5:  reg_val = 1; break;
        case 9:  reg_val = 2; break;
        case 16: reg_val = 3; break;
        default:
            ESP_LOGE(TAG, "set_min_strikes: invalid count %u (must be 1,5,9,16)",
                     count);
            return ESP_ERR_INVALID_ARG;
    }
    uint8_t val = (reg_val << LIGHTNING_MINNUM_SHIFT) & LIGHTNING_MINNUM_MASK;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = as3935_reg_write_masked(REG_LIGHTNING_REG,
                                            LIGHTNING_MINNUM_MASK, val, false);
    xSemaphoreGive(s_mutex);
    return err;
}

esp_err_t as3935_set_disturber_mask(bool masked)
{
    uint8_t val = masked ? INT_MASK_ANT_MASKD_BIT : 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = as3935_reg_write_masked(REG_INT_MASK_ANT,
                                            INT_MASK_ANT_MASKD_BIT, val,
                                            false);
    xSemaphoreGive(s_mutex);
    return err;
}

/* -----------------------------------------------------------------------
 * Runtime
 * ----------------------------------------------------------------------- */

esp_err_t as3935_read_event(as3935_data_t *out)
{
    memset(out, 0, sizeof(*out));
    out->timestamp_us = esp_timer_get_time();

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    uint8_t   int_reg = 0;
    esp_err_t err;

    for (int attempt = 0; attempt < 3; attempt++) {
        err = as3935_reg_read(REG_INT_MASK_ANT, &int_reg);
        if (err == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG, "read_event: INT reg read attempt %d failed: %s",
                 attempt + 1, esp_err_to_name(err));
    }
    if (err != ESP_OK) {
        xSemaphoreGive(s_mutex);
        return err;
    }

    uint8_t int_src = int_reg & INT_MASK_ANT_INT_MASK;

    switch (int_src) {
        case INT_NOISE:
            out->event = AS3935_EVENT_NOISE;
            ESP_LOGI(TAG, "event=noise");
            break;

        case INT_DISTURBER:
            out->event = AS3935_EVENT_DISTURBER;
            ESP_LOGI(TAG, "event=disturber");
            break;

        case INT_LIGHTNING: {
            out->event = AS3935_EVENT_LIGHTNING;

            uint8_t e1 = 0, e2 = 0, e3 = 0, dist = 0;
            esp_err_t e_err = ESP_OK;

            for (int attempt = 0; attempt < 3; attempt++) {
                e_err = as3935_reg_read(REG_ENERGY_1, &e1);
                if (e_err == ESP_OK) e_err = as3935_reg_read(REG_ENERGY_2, &e2);
                if (e_err == ESP_OK) e_err = as3935_reg_read(REG_ENERGY_3, &e3);
                if (e_err == ESP_OK) e_err = as3935_reg_read(REG_DISTANCE, &dist);
                if (e_err == ESP_OK) break;
                ESP_LOGW(TAG, "lightning data read attempt %d: %s",
                         attempt + 1, esp_err_to_name(e_err));
            }

            if (e_err != ESP_OK) {
                ESP_LOGW(TAG, "read_event: data read failed, skipping");
                err = e_err;
                break;
            }

            out->energy      = ((uint32_t)(e3 & 0x1F) << 16)
                               | ((uint32_t)e2 << 8) | e1;
            out->distance_km = dist & DISTANCE_MASK;

            ESP_LOGI(TAG, "event=lightning dist=%ukm energy=%" PRIu32,
                     out->distance_km, out->energy);
            break;
        }

        default:
            out->event = AS3935_EVENT_NONE;
            ESP_LOGD(TAG, "event=none (INT=0x%02X)", int_src);
            break;
    }

    xSemaphoreGive(s_mutex);
    return err;
}

/* Noise floor µV table (indoor values per AS3935 datasheet) */
static const uint16_t s_nf_uv_table[8] = {390, 630, 860, 1100,
                                            1140, 1570, 1800, 2000};

esp_err_t as3935_get_noise_floor_actual(uint8_t *out_uv)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint8_t   reg = 0;
    esp_err_t err = as3935_reg_read(REG_THRESHOLD, &reg);
    xSemaphoreGive(s_mutex);

    if (err != ESP_OK) {
        return err;
    }
    uint8_t level = (reg & THRESHOLD_NFLEV_MASK) >> THRESHOLD_NFLEV_SHIFT;
    /* Table values > 255; cast to uint8_t truncates for very large values.
     * Caller receives the index (0-7) scaled by 10 as a compact uint8_t,
     * or use a uint16_t out parameter if full precision is needed. */
    *out_uv = (uint8_t)(s_nf_uv_table[level & 0x07] / 10);
    return ESP_OK;
}

esp_err_t as3935_power_down(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = as3935_reg_write_masked(REG_AFE_GAIN, AFE_GAIN_PWD_MASK,
                                            0x01, true);
    xSemaphoreGive(s_mutex);
    return err;
}

esp_err_t as3935_power_up(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = as3935_reg_write_masked(REG_AFE_GAIN, AFE_GAIN_PWD_MASK,
                                            0x00, true);
    xSemaphoreGive(s_mutex);
    return err;
}

gpio_num_t as3935_get_irq_gpio(void)
{
    return s_irq_gpio;
}
