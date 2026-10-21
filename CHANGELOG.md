# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

Platform: ESP32 (Arduino framework)
License: GPL-3
Dependencies: ESP32MQTTClient (external), LibraryCommon


### Fixed (since the initial publication, folded into 0.1.0)
- **Self-deadlock in the publish path removed.** `onDisconnect()` and `onConnect()` took the
  instance mutex with `portMAX_DELAY`. esp-mqtt calls `esp_mqtt_abort_connection()` when a
  transport write stalls past the network timeout, and that dispatches
  `MQTT_EVENT_DISCONNECTED` **inline in the publishing task** (the client uses a no-task
  event loop). Since `publish()` holds the same non-recursive mutex across the call into
  esp-mqtt, the task re-entered the mutex it already held and blocked forever, then never fed
  its task watchdog again and caused a panic-reboot. The connection flag is now
  `std::atomic<bool>` written outside the guard, and both handlers take the mutex with a
  short bounded wait (`MQTT_EVENT_HANDLER_TIMEOUT_MS`, 100 ms) for bookkeeping only.
- `publish()` no longer takes the instance mutex with `portMAX_DELAY`. It waits at most
  `MQTT_DEFAULT_TIMEOUT_MS` (5 s) and returns `MQTTError::TIMEOUT` instead of blocking the
  calling task indefinitely; a caller publishing from its main loop could otherwise be pinned
  past its task watchdog while another task held the mutex inside a stalled
  `esp_mqtt_client_publish()`. Callers must treat `TIMEOUT` as a dropped message.
- `disconnect()` now returns `bool` - whether a live client was actually torn down - and is
  bounded (returns false rather than blocking if the mutex is unavailable). Callers that need
  to know resources were released (an OTA handler freeing DRAM, say) could not tell before:
  `isConnected()` reads the event-group bit while the teardown is guarded by the client's own
  flag, and the two diverge briefly during disconnect handling.
- `configureClient()` sets the esp-mqtt network timeout to `MQTT_NETWORK_TIMEOUT_MS` (5 s)
  instead of esp-mqtt's 10 s default, which also bounds how long `esp_mqtt_client_stop()`
  blocks during teardown (a measured teardown took ~10.4 s, overrunning espota's 10 s OTA
  connect-back window). Not shorter than 5 s, because it also decides how quickly a stalled
  write triggers the inline abort above. Requires `ESP32MQTTClient::setNetworkTimeout()`.
- printf format specifiers at `MQTTM_LOG_*` call sites corrected (now type-checked through
  ESP32-Logger's `format(printf)` attribute).

### Changed (documentation)
- README rewritten against the actual API (singleton, `MQTTConfig`, `MQTTResult` return
  values, events, reconnect, bounded publish/disconnect, tunables); the stale "Suggested
  Improvements" list was removed.
- Documented that ESP32-Logger is deliberately not declared in `library.json`: a
  `USE_CUSTOM_LOGGER` build must list it in the application's `lib_deps`, otherwise it fails
  with `LogInterface.h: No such file or directory`. Also documented the global-lib-dir LDF
  case that produces the same error (workaround: `lib_ignore = ESP32-MQTTManager`).

### Notes
- Production-tested for MQTT communication in industrial IoT applications
- Stable connection with broker reconnection over weeks
- Previous internal versions (v1.x) not publicly released
- Reset to v0.1.0 for clean public release start
