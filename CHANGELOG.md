# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

### Added
- Initial implementation of esp-lightning-mqtt v1.0
- AS3935 I2C driver (`main/lightning/as3935.c`) with full register map support
- FreeRTOS lightning task with ISR-based interrupt handling
- Wi-Fi manager with exponential-backoff reconnect (1 s → 60 s, reboot after 10 failures)
- MQTT client wrapper using esp-mqtt with LWT support
- JSON payload builder for lightning, noise, disturber, status, and config events
- NVS-backed runtime configuration with partial-update config/set handler
- Kconfig menuconfig entries for all hardware and network parameters
- SNTP time synchronisation for ISO 8601 timestamps
- Unity-based unit tests for driver enums and payload builders
- GitHub Actions CI: ESP-IDF build + clang-format lint
- Wiring and MQTT schema documentation
