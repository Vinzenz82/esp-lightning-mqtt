# esp-lightning-mqtt — Project Specification

> **Coding-Agent Optimized Spec** | Version 1.0 | ESP-IDF / ESP32-C3 / AS3935 / MQTT

---

## 1. Project Overview

| Field | Value |
|---|---|
| **Name** | `esp-lightning-mqtt` |
| **MCU** | ESP32-C3 (RISC-V, single-core) |
| **Sensor** | AMS AS3935 Franklin Lightning Sensor |
| **Bus** | I2C |
| **Connectivity** | Wi-Fi → MQTT |
| **Framework** | ESP-IDF v5.x (no Arduino) |
| **License** | MIT |
| **Language** | C (C17) |

**Goal:** Detect lightning events (strike, distance, energy) with the AS3935 and publish structured JSON payloads via MQTT. Designed for home automation integration (Home Assistant, Node-RED, etc.).

---

## 2. Repository Layout

```
esp-lightning-mqtt/
├── CMakeLists.txt               # Top-level IDF build
├── sdkconfig.defaults           # Pre-configured defaults
├── main/
│   ├── CMakeLists.txt
│   ├── main.c                   # App entry, task orchestration
│   ├── app_config.h             # Central compile-time config (single source of truth)
│   ├── lightning/
│   │   ├── as3935.c / .h        # AS3935 driver (pure I2C, no side effects)
│   │   └── lightning_task.c/.h  # FreeRTOS task, interrupt handling
│   ├── mqtt/
│   │   ├── mqtt_client.c / .h   # MQTT wrapper (esp-mqtt component)
│   │   └── mqtt_payload.c / .h  # JSON payload builder
│   ├── wifi/
│   │   └── wifi_manager.c / .h  # Wi-Fi STA, reconnect logic
│   └── utils/
│       └── nvs_config.c / .h    # NVS read/write for runtime config
├── components/                  # (empty, reserved for custom components)
├── test/
│   └── test_as3935/             # Unity-based unit tests (IDF test runner)
├── docs/
│   ├── wiring.md
│   └── mqtt_schema.md
├── .github/
│   └── workflows/
│       └── build.yml            # CI: idf.py build + clang-format check
├── Kconfig.projbuild            # Menuconfig entries for project settings
├── README.md
└── CHANGELOG.md
```

---

## 3. Hardware Configuration

### 3.1 I2C Wiring (ESP32-C3)

| Signal | ESP32-C3 GPIO | AS3935 Pin | Notes |
|---|---|---|---|
| SDA | GPIO 6 | SDA | 4.7 kΩ pull-up to 3.3 V |
| SCL | GPIO 7 | SCL | 4.7 kΩ pull-up to 3.3 V |
| INT | GPIO 4 | IRQ | Active-high interrupt |
| GND | GND | GND | — |
| 3V3 | 3.3 V | VCC | Max 3.6 V |

> **Configurable:** All GPIOs are `#define`-based in `app_config.h` and exposed via Kconfig.

### 3.2 AS3935 I2C Address

Default: `0x03` (ADD pins to GND). Configurable via Kconfig (`CONFIG_AS3935_I2C_ADDR`).

---

## 4. AS3935 Driver (`as3935.h`)

### 4.1 Data Types

```c
typedef enum {
    AS3935_NOISE_FLOOR_390UV  = 0,
    AS3935_NOISE_FLOOR_630UV  = 1,
    // ... up to 7
} as3935_noise_floor_t;

typedef enum {
    AS3935_WATCHDOG_SENSITIVITY_1 = 1,
    // ... up to 10
} as3935_watchdog_t;

typedef enum {
    AS3935_SPIKE_REJECTION_1 = 1,
    // ... up to 11
} as3935_spike_rejection_t;

typedef enum {
    AS3935_EVENT_NONE        = 0,
    AS3935_EVENT_NOISE       = 1,
    AS3935_EVENT_DISTURBER   = 2,
    AS3935_EVENT_LIGHTNING   = 3,
} as3935_event_t;

typedef struct {
    as3935_event_t  event;
    uint8_t         distance_km;    // 1 = overhead, 63 = out of range
    uint32_t        energy;         // relative, no unit
    int64_t         timestamp_us;   // esp_timer_get_time()
} as3935_data_t;
```

### 4.2 API Contract

```c
// Lifecycle
esp_err_t as3935_init(i2c_port_t port, uint8_t addr, gpio_num_t irq_gpio);
esp_err_t as3935_reset(void);
esp_err_t as3935_calibrate_rco(void);

// Configuration
esp_err_t as3935_set_indoor_mode(bool indoor);
esp_err_t as3935_set_noise_floor(as3935_noise_floor_t level);
esp_err_t as3935_set_watchdog(as3935_watchdog_t threshold);
esp_err_t as3935_set_spike_rejection(as3935_spike_rejection_t level);
esp_err_t as3935_set_min_strikes(uint8_t count);   // 1, 5, 9, or 16
esp_err_t as3935_set_disturber_mask(bool masked);

// Runtime
esp_err_t as3935_read_event(as3935_data_t *out);   // call after IRQ fires
esp_err_t as3935_get_noise_floor_actual(uint8_t *out_uv);
esp_err_t as3935_power_down(void);
esp_err_t as3935_power_up(void);
```

**Driver rules:**
- All register access through two private functions: `as3935_reg_read()` / `as3935_reg_write_masked()`
- No `vTaskDelay` inside driver; caller is responsible for timing
- Thread-safe: caller must hold I2C bus mutex before calling
- Return `ESP_ERR_INVALID_RESPONSE` if register readback does not match write

---

## 5. MQTT Payload Schema

### 5.1 Topics

| Topic | Direction | Retained | Description |
|---|---|---|---|
| `{base}/lightning` | Publish | No | Lightning event |
| `{base}/noise` | Publish | No | Noise event |
| `{base}/disturber` | Publish | No | Disturber event |
| `{base}/status` | Publish | Yes | Online/offline LWT |
| `{base}/config/set` | Subscribe | — | Runtime config update |
| `{base}/config/get` | Publish | Yes | Current config state |

`base` default: `esp-lightning-mqtt/{device_id}` — configurable via NVS.

### 5.2 Lightning Payload

```json
{
  "event": "lightning",
  "distance_km": 12,
  "energy": 524288,
  "timestamp_iso": "2025-03-15T14:23:01Z",
  "timestamp_us": 1710508981000000,
  "device_id": "c3-aabbccdd",
  "rssi": -67,
  "uptime_s": 3642
}
```

### 5.3 Status Payload (LWT)

```json
{ "state": "online" }   // or "offline"
```

### 5.4 Config Set Payload (inbound)

```json
{
  "indoor_mode": true,
  "noise_floor": 2,
  "watchdog": 3,
  "spike_rejection": 2,
  "min_strikes": 1,
  "disturber_mask": false
}
```

All fields optional; partial updates accepted.

---

## 6. FreeRTOS Task Architecture

```
┌──────────────────────────────────────────────────────┐
│  app_main()                                           │
│   ├─ nvs_config_init()                               │
│   ├─ wifi_manager_start()   ──► wifi_task (8KB)      │
│   ├─ mqtt_client_start()    ──► mqtt_task (6KB)      │
│   └─ lightning_task_start() ──► lightning_task (4KB) │
│                                      │               │
│                             IRQ GPIO interrupt       │
│                             (IRAM ISR → notify task) │
└──────────────────────────────────────────────────────┘
```

**Synchronization:**
- IRQ ISR uses `xTaskNotifyFromISR()` — no I2C in ISR
- `lightning_task` calls `as3935_read_event()`, then enqueues `as3935_data_t` to a `QueueHandle_t` (depth: 8)
- `mqtt_task` dequeues and publishes — decoupled from sensor timing

**Stack sizes and priorities** defined in `app_config.h`:
```c
#define LIGHTNING_TASK_STACK   4096
#define LIGHTNING_TASK_PRIO    5
#define MQTT_TASK_STACK        6144
#define MQTT_TASK_PRIO         4
#define WIFI_TASK_STACK        8192
#define WIFI_TASK_PRIO         3
```

---

## 7. Wi-Fi & MQTT Configuration

### 7.1 Kconfig / Menuconfig entries (`Kconfig.projbuild`)

```
CONFIG_WIFI_SSID          (string, max 32)
CONFIG_WIFI_PASSWORD      (string, max 64)
CONFIG_MQTT_BROKER_URL    (string, e.g. "mqtt://192.168.1.10:1883")
CONFIG_MQTT_USERNAME      (string, optional)
CONFIG_MQTT_PASSWORD      (string, optional)
CONFIG_MQTT_BASE_TOPIC    (string, default "esp-lightning-mqtt")
CONFIG_AS3935_I2C_ADDR    (hex, default 0x03)
CONFIG_AS3935_I2C_PORT    (int, 0 or 1, default 0)
CONFIG_AS3935_SDA_GPIO    (int, default 6)
CONFIG_AS3935_SCL_GPIO    (int, default 7)
CONFIG_AS3935_IRQ_GPIO    (int, default 4)
CONFIG_AS3935_INDOOR_MODE (bool, default y)
```

Runtime overrides stored in NVS partition `nvs` under namespace `lightning`.

### 7.2 Wi-Fi Reconnect Strategy

- Exponential backoff: 1 s → 2 s → 4 s → … → max 60 s
- After 10 consecutive failures: reboot via `esp_restart()`
- Event bits via `EventGroupHandle_t` for MQTT to wait on Wi-Fi

### 7.3 MQTT QoS & Reliability

- Lightning/noise/disturber events: QoS 1
- LWT: QoS 1, retained
- Outbound queue: 8 messages; if full, oldest is dropped and a counter metric is published

---

## 8. NVS Runtime Config

Module `nvs_config` provides:
```c
esp_err_t nvs_config_load(app_runtime_config_t *cfg);
esp_err_t nvs_config_save(const app_runtime_config_t *cfg);
```

`app_runtime_config_t` mirrors all AS3935 tunable parameters + MQTT base topic. On first boot (NVS empty), Kconfig defaults are used and written to NVS.

---

## 9. Error Handling Policy

| Condition | Action |
|---|---|
| I2C init fails | Log error, halt (`abort()`) — sensor is mandatory |
| I2C read/write fails (transient) | Retry 3×, then log warning and skip event |
| Wi-Fi connect fails | Reconnect loop (see §7.2) |
| MQTT disconnect | esp-mqtt handles auto-reconnect |
| AS3935 calibration fails | Log error, continue with defaults |
| NVS read fails | Use compile-time defaults, log warning |

All errors logged with `ESP_LOGE` / `ESP_LOGW` using per-module TAG constants.

---

## 10. Logging

- Level `INFO` in production builds
- Level `DEBUG` available via menuconfig (`CONFIG_LOG_DEFAULT_LEVEL_DEBUG`)
- Tags: `AS3935`, `MQTT_CLIENT`, `WIFI_MGR`, `LIGHTNING_TASK`, `NVS_CONFIG`
- Structured log lines: `[AS3935] event=lightning dist=12km energy=524288`

---

## 11. Build & Flash

```bash
# Setup
. $IDF_PATH/export.sh

# Configure
idf.py menuconfig

# Build
idf.py build

# Flash & monitor
idf.py -p /dev/ttyUSB0 flash monitor

# Run unit tests
idf.py -T test/test_as3935 build flash monitor
```

`sdkconfig.defaults` ships with sensible production defaults (log level INFO, PSRAM off, optimized for size).

---

## 12. CI Pipeline (`.github/workflows/build.yml`)

Steps:
1. `actions/checkout`
2. `espressif/esp-idf-ci-action` — `idf.py build` for target `esp32c3`
3. `clang-format --dry-run --Werror` on `main/**/*.c main/**/*.h`
4. (optional) `idf.py size` artifact upload

---

## 13. AS3935 Register Map Reference

| Register | Addr | Key Bits | Notes |
|---|---|---|---|
| AFE_GAIN | 0x00 | [5:1] PWD, [5:1] GAIN | Power & indoor/outdoor |
| THRESHOLD | 0x01 | [3:0] WDTH, [6:4] NF_LEV | Noise & watchdog |
| LIGHTNING_REG | 0x02 | [3:0] SREJ, [5:4] MIN_NUM_LIGH | Spike rej, min strikes |
| INT_MASK_ANT | 0x03 | [3:0] INT, [5] MASK_DIST | Interrupt source |
| ENERGY_1 | 0x04 | [7:0] | Energy LSB |
| ENERGY_2 | 0x05 | [7:0] | Energy MSB |
| ENERGY_3 | 0x06 | [4:0] | Energy MMSB |
| DISTANCE | 0x07 | [5:0] | Distance estimation |
| DISP_LCO | 0x08 | [5:0] TUN_CAP, [7] DISP_LCO | Antenna tuning |
| CALIB_TRCO | 0x3A | [6] TRCO_CALIB_DONE | RCO calibration status |
| CALIB_SRCO | 0x3B | [6] SRCO_CALIB_DONE | — |
| PRESET_DEFAULT | 0x3C | write 0x96 | Factory reset |
| CALIB_RCO | 0x3D | write 0x96 | Trigger calibration |

---

## 14. Home Assistant Integration Example

```yaml
# configuration.yaml
mqtt:
  sensor:
    - name: "Lightning Distance"
      state_topic: "esp-lightning-mqtt/c3-aabbccdd/lightning"
      value_template: "{{ value_json.distance_km }}"
      unit_of_measurement: "km"
      device_class: distance

    - name: "Lightning Energy"
      state_topic: "esp-lightning-mqtt/c3-aabbccdd/lightning"
      value_template: "{{ value_json.energy }}"
```

---

## 15. Out of Scope (v1.0)

- TLS/MQTTS (planned v2.0 — cert provisioning via NVS)
- OTA updates
- SPI mode for AS3935
- Multiple sensors per device
- Web UI for config

---

*Generated for `esp-lightning-mqtt` v1.0 — ready for coding agent ingestion.*
