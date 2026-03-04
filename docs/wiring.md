# Wiring Guide — ESP32-C3 ↔ AS3935

## I2C Connections

| Signal | ESP32-C3 GPIO | AS3935 Pin | Notes                         |
|--------|---------------|------------|-------------------------------|
| SDA    | GPIO 6        | SDA        | 4.7 kΩ pull-up to 3.3 V      |
| SCL    | GPIO 7        | SCL        | 4.7 kΩ pull-up to 3.3 V      |
| INT    | GPIO 4        | IRQ        | Active-high interrupt          |
| GND    | GND           | GND        | Common ground                  |
| 3V3    | 3.3 V         | VCC        | Max 3.6 V — do **not** use 5 V |

All GPIO numbers are configurable via `idf.py menuconfig` → **ESP Lightning MQTT → AS3935 Sensor**.

## AS3935 Address Pins (ADD)

| ADD[1] | ADD[0] | I2C Address |
|--------|--------|-------------|
| GND    | GND    | 0x03 (default) |
| GND    | VCC    | 0x02           |
| VCC    | GND    | 0x01           |

Set `CONFIG_AS3935_I2C_ADDR` to match your hardware configuration.

## Antenna

The AS3935 requires a 500 kHz resonant LC antenna on the ANT pin.
Use the SparkFun AS3935 breakout (SEN-15276) or equivalent, which
ships with the antenna pre-assembled.

## Power Supply

- Supply: 2.4 V – 3.6 V (use 3.3 V rail of the ESP32-C3)
- Current: ~2 mA active, ~1 µA power-down
- Decouple with 100 nF + 10 µF capacitor close to VCC pin
