# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed
- `publish()` no longer takes the instance mutex with `portMAX_DELAY`. It now waits
  at most `MQTT_DEFAULT_TIMEOUT_MS` (5 s) and returns `MQTTError::TIMEOUT` instead of
  blocking the calling task indefinitely. A caller that publishes from its main loop
  could otherwise be pinned past its task watchdog while another task held the mutex
  inside a stalled `esp_mqtt_client_publish()`; on a device with panic-on-timeout this
  rebooted the system. Callers must treat `TIMEOUT` as a dropped message.

## [0.1.0] - 2025-12-04

### Added
- Initial public release
- Singleton MQTT client manager wrapper around ESP32MQTTClient
- Auto-reconnect with exponential backoff
- Event-driven callbacks for message handling
- FreeRTOS event groups for connection state synchronization
- Result<T> based error handling using LibraryCommon
- MQTTConfig builder pattern for clean configuration
- Last will and testament (LWT) support
- QoS level support (0, 1, 2)
- Retained message support
- Thread-safe operations with FreeRTOS mutex
- Topic subscription with callback registration
- Message publishing with error handling
- Connection state monitoring

Platform: ESP32 (Arduino/ESP-IDF)
License: GPL-3
Dependencies: ESP32MQTTClient (external), LibraryCommon

### Notes
- Production-tested for MQTT communication in industrial IoT applications
- Stable connection with broker reconnection over weeks
- Previous internal versions (v1.x) not publicly released
- Reset to v0.1.0 for clean public release start
