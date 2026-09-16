# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed
- **Self-deadlock in the publish path removed.** `onDisconnect()` and `onConnect()` took the
  instance mutex with `portMAX_DELAY`. esp-mqtt calls `esp_mqtt_abort_connection()` when a
  transport write stalls past the network timeout, and that dispatches
  `MQTT_EVENT_DISCONNECTED` **inline in the publishing task** (the client uses a no-task
  event loop). Since `publish()` holds the same non-recursive mutex across the call into
  esp-mqtt, the task re-entered the mutex it already held and blocked forever - after which
  it never fed its task watchdog again, producing exactly the panic-reboot the bounded
  publish mutex was added to prevent. The connection flag is now `std::atomic<bool>` written
  outside the guard, and both handlers take the mutex with a short bounded wait
  (`MQTT_EVENT_HANDLER_TIMEOUT_MS`, 100 ms) for bookkeeping only.
- `disconnect()` now returns `bool` - whether a live client was actually torn down. Callers
  that need to know resources were released (an OTA handler freeing DRAM, say) could not
  tell before: `isConnected()` reads the event-group bit while the teardown is guarded by
  the client's own `_mqttConnected` flag, and the two diverge briefly during disconnect
  handling, so a caller could log a successful teardown while nothing was freed. It is also
  bounded now, and returns false rather than blocking if the mutex is unavailable.
- Network timeout raised from 3 s to 5 s. It also decides how quickly a stalled write makes
  esp-mqtt abort the connection, so 3 s made the deadlock path above easier to reach while
  buying little; 5 s still leaves an OTA teardown inside espota's 10 s connect-back window.
- `configureClient()` now sets the esp-mqtt network timeout to `MQTT_NETWORK_TIMEOUT_MS`
  (3 s) instead of leaving esp-mqtt's 10 s default. That default is far longer than a
  broker on the same LAN needs, and it also bounds how long `esp_mqtt_client_stop()`
  blocks while tearing the client down: a measured teardown took ~10.4 s, which
  overran the 10 s window espota allows a device to answer an OTA invitation.
  Requires `ESP32MQTTClient::setNetworkTimeout()`.
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
