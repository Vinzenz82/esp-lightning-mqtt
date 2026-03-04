# MQTT Schema — esp-lightning-mqtt

## Topic Structure

Base topic: `{CONFIG_MQTT_BASE_TOPIC}/{device_id}`
Default: `esp-lightning-mqtt/c3-aabbccdd`

| Topic                            | Direction | Retained | QoS | Description            |
|----------------------------------|-----------|----------|-----|------------------------|
| `{base}/lightning`               | Publish   | No       | 1   | Lightning event        |
| `{base}/noise`                   | Publish   | No       | 1   | Noise level too high   |
| `{base}/disturber`               | Publish   | No       | 1   | Disturber detected     |
| `{base}/status`                  | Publish   | Yes      | 1   | Online / offline LWT   |
| `{base}/config/set`              | Subscribe | —        | 1   | Runtime config update  |
| `{base}/config/get`              | Publish   | Yes      | 1   | Current config state   |

## Lightning Payload

```json
{
  "event": "lightning",
  "distance_km": 12,
  "energy": 524288,
  "timestamp_iso": "2025-03-15T14:23:01Z",
  "timestamp_us": "1710508981000000",
  "device_id": "c3-aabbccdd",
  "rssi": -67,
  "uptime_s": 3642
}
```

| Field           | Type    | Description                                      |
|-----------------|---------|--------------------------------------------------|
| `event`         | string  | Always `"lightning"`                             |
| `distance_km`   | number  | Estimated distance in km (1 = overhead, 63 = OOR)|
| `energy`        | number  | Relative strike energy (0 – 1 048 575)           |
| `timestamp_iso` | string  | UTC ISO 8601 (requires SNTP sync)                |
| `timestamp_us`  | string  | `esp_timer_get_time()` in microseconds           |
| `device_id`     | string  | MAC-derived device identifier                    |
| `rssi`          | number  | Wi-Fi RSSI in dBm                                |
| `uptime_s`      | number  | Seconds since boot                               |

## Noise / Disturber Payload

Same fields as lightning **minus** `distance_km` and `energy`.
`event` is `"noise"` or `"disturber"` respectively.

## Status Payload (LWT)

```json
{ "state": "online" }
```
```json
{ "state": "offline" }
```

The offline payload is configured as the MQTT Last Will and Testament
(retained, QoS 1). The device publishes `online` upon successful broker
connection.

## Config Set Payload (inbound)

All fields are **optional**; partial updates are accepted.

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

| Field             | Type    | Range          | Default | Description |
|-------------------|---------|----------------|---------|-------------|
| `indoor_mode`     | boolean | true / false   | true    | Switches the AS3935 AFE gain preset. `true` = indoor (higher gain, attenuated for enclosed spaces); `false` = outdoor (lower gain, suited for open environments). Affects sensitivity and false-positive rate. |
| `noise_floor`     | integer | 0 – 7          | 2       | Sets the analog noise floor threshold. Higher values require stronger signals to exceed the floor, reducing sensitivity to weak events but increasing immunity to background noise. Each step is roughly 65 µVrms. |
| `watchdog`        | integer | 1 – 10         | 2       | Controls the watchdog threshold used for signal validation. Higher values make the detector more selective, rejecting shorter or weaker pulses. Increase if the device triggers on non-lightning signals. |
| `spike_rejection` | integer | 1 – 11         | 2       | Sets the spike rejection threshold. The AS3935 uses this to suppress impulsive EMI spikes. Higher values reject more aggressively; too high a value may suppress real lightning events. |
| `min_strikes`     | integer | 1, 5, 9, 16    | 1       | Minimum number of lightning events within the last 15 minutes before the interrupt fires. Use higher values (5, 9, 16) to reduce single-event false positives in noisy environments. |
| `disturber_mask`  | boolean | true / false   | false   | When `true`, the AS3935 suppresses disturber interrupts — the device will not publish to `{base}/disturber`. Useful to reduce MQTT traffic in RF-noisy environments when disturber events are not needed. |

Changes are persisted to NVS and applied immediately. The updated
configuration is echoed to `{base}/config/get`.

## Home Assistant Example

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

    - name: "Lightning Sensor Status"
      state_topic: "esp-lightning-mqtt/c3-aabbccdd/status"
      value_template: "{{ value_json.state }}"
```
