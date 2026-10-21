# MQTTManager - CLAUDE.md

## Overview
Singleton MQTT client manager for ESP32 (Arduino framework, C++17) on top of ESP32-MQTTClient
(esp-mqtt). Thread-safe, auto-reconnect with exponential backoff, event callback + FreeRTOS
event groups, `common::Result<T, MQTTError>` error handling (ESP32-LibraryCommon).

## Layout
- `src/MQTTManager.h/.cpp` - the manager, `MQTTConfig` builder, `MQTTError`, tunables
- `src/IMqttMessageHandler.h` - interface for `registerTopicHandler()`
- `src/MQTTManagerLogging.h` - `MQTTM_LOG_*` macros (ESP-IDF log, or ESP32-Logger with `USE_CUSTOM_LOGGER`)
- `examples/SmokeTest` - compile-only example covering the public API; built by CI (`.github/workflows/ci.yml`, pioarduino / core 3.3.x)
- `logger_submodule` - ESP32-Logger gitlink kept for IntelliSense only; do not remove

## Usage
```cpp
auto& mqtt = MQTTManager::getInstance();
auto r = mqtt.begin(MQTTConfig("mqtt://broker.local")
                        .withClientId("esp32-device")
                        .withCredentials("user", "pass")
                        .withKeepAlive(60)
                        .withLastWill("status", "offline"));
if (r && mqtt.connect()) { /* connection completes asynchronously */ }
mqtt.subscribe("topic", [](const String& msg) { Serial.println(msg); }).isOk();
mqtt.publish("topic", "message").isOk();
```
`begin()`, `connect()`, `publish()`, `subscribe()` are `[[nodiscard]]`.

## Concurrency invariants (do not regress)
- esp-mqtt runs with no event-loop task: `MQTT_EVENT_CONNECTED/DISCONNECTED` can be dispatched
  inline in whichever task triggered them, including a task already inside `publish()` holding
  the instance mutex (stalled write -> `esp_mqtt_abort_connection()`).
- Therefore `onConnect()`/`onDisconnect()` take the mutex with `MQTT_EVENT_HANDLER_TIMEOUT_MS`
  (100 ms) for bookkeeping only, and `connected` is `std::atomic<bool>` written outside it.
- `publish()` waits at most `MQTT_DEFAULT_TIMEOUT_MS` (5 s) -> `MQTTError::TIMEOUT` (message dropped).
- `disconnect()` is bounded (5 s) and returns `bool`: true only if a live client was torn down.
- `MQTT_NETWORK_TIMEOUT_MS` = 5 s: keeps OTA teardown inside espota's 10 s window; shorter makes
  the inline-abort path above easier to hit.
- Other methods (`begin`, `connect`, `subscribe`, setters) still take the mutex with `portMAX_DELAY`.

## Dependencies
- Declared: ESP32-MQTTClient, ESP32-LibraryCommon.
- ESP32-Logger is deliberately NOT declared (fleet convention: the app supplies it when it
  defines `USE_CUSTOM_LOGGER`). See README "Using Custom Logger".

## Build flags
- `USE_CUSTOM_LOGGER` - route logs to ESP32-Logger
- `MQTTMANAGER_DEBUG` - enable debug/verbose logs (`MQTTMANAGER_DEBUG_CONN`, `MQTTMANAGER_DEBUG_QUEUE` for finer detail)

## Conventions
Version stays 0.1.0 (CHANGELOG [0.1.0] is recut instead of bumping). Conventional commits
(`fix:`, `docs:`, `ci:`, `chore:`) with a why-body; no amend/force-push.
