# ESP32-MQTTManager

A thread-safe MQTT manager for ESP32 (Arduino framework), built on
[ESP32-MQTTClient](https://github.com/packerlschupfer/ESP32-MQTTClient) and FreeRTOS.

## Overview

`MQTTManager` is a singleton that owns the device's MQTT connection. It wraps ESP32-MQTTClient
(esp-mqtt) and adds:
- configuration through `begin()` or the `MQTTConfig` builder
- auto-reconnect with exponential backoff
- events delivered three ways: a callback, a FreeRTOS event group, or an external event group
- `common::Result`-based error handling from ESP32-LibraryCommon
- bounded mutex waits on the paths that run during a stalled network

## Features

- Singleton access via `MQTTManager::getInstance()`
- `MQTTConfig` builder: client ID, credentials, keep-alive, Last Will, auto-reconnect
- Client ID generated from the chip's eFuse MAC when none is given (`ESP32_<MAC>`)
- `[[nodiscard]] MQTTResult<void>` return values for `begin()`, `connect()`, `publish()` and `subscribe()`
- Subscriptions with lambda callbacks (payload, or topic + payload) or `IMqttMessageHandler` objects
- Tracks broker SUBACKs: `isSubscriptionConfirmed()`, `getSubscriptionQos()`
- Auto-reconnect with exponential backoff (`ReconnectConfig`)
- Event callback plus FreeRTOS event-group bits (internal, or bits you choose in your own group)
- `publish()` waits at most 5 s for the instance mutex and returns `MQTTError::TIMEOUT` instead of blocking
- ESP-IDF logging by default, or ESP32-Logger with `USE_CUSTOM_LOGGER`

## Dependencies

- ESP32 Arduino Core (pioarduino / Arduino core 3.x is what CI builds)
- [ESP32-MQTTClient](https://github.com/packerlschupfer/ESP32-MQTTClient) (declared in `library.json`)
- [ESP32-LibraryCommon](https://github.com/packerlschupfer/ESP32-LibraryCommon) (declared in `library.json`)
- FreeRTOS (included with ESP32)
- Optional: [ESP32-Logger](https://github.com/packerlschupfer/ESP32-Logger), only when building
  with `USE_CUSTOM_LOGGER`. Not declared in `library.json`; the application supplies it (see
  [Using Custom Logger](#using-custom-logger)).

## Installation

### PlatformIO

Add to your `platformio.ini`:

```ini
build_unflags = -std=gnu++11
build_flags = -std=gnu++17
lib_deps =
    https://github.com/packerlschupfer/ESP32-MQTTManager.git
```

The library needs C++17. `examples/SmokeTest` is a minimal, self-contained project that
compiles against the whole public API.

## Usage

All examples assume `#include <MQTTManager.h>` and
`MQTTManager& mqtt = MQTTManager::getInstance();`. Bring up the network (WiFi/Ethernet) first.

### Connecting

`begin()` only stores the configuration. `connect()` starts the client, and the connection
completes asynchronously.

```cpp
auto result = mqtt.begin(
    MQTTConfig("mqtt://broker.local:1883")
        .withClientId("esp32-device")          // optional: default is ESP32_<eFuse MAC>
        .withCredentials("user", "pass")
        .withKeepAlive(60)                     // seconds, default 30
        .withLastWill("device/status", "offline", 0, true)
        .withAutoReconnect(true));             // default true
if (!result) {
    Serial.printf("begin failed: %d\n", static_cast<int>(result.error()));
}

if (mqtt.connect() && mqtt.waitForConnection(5000)) {
    Serial.println("Connected");
}
```

The short form `mqtt.begin("mqtt://broker.local", "clientId", "user", "pass")` is also
available; every argument after the URI is optional.

### Publishing

```cpp
auto r = mqtt.publish("sensor/temperature", "25.5");          // QoS 0, not retained
mqtt.publish("sensor/status", "online", 1, true).isOk();      // QoS 1, retained
mqtt.publish("test/message", String("Hello"));                 // Arduino String overload

if (r.isError() && r.error() == MQTTError::TIMEOUT) {
    // Instance mutex not available within 5 s: the message was dropped.
}
```

`publish()` never blocks for longer than `MQTT_DEFAULT_TIMEOUT_MS` (5 s) on the instance mutex.
On `MQTTError::TIMEOUT` the message was **not** sent, so the caller should treat it as dropped.

### Subscribing

```cpp
// Payload only
mqtt.subscribe("device/command", [](const String& payload) {
    Serial.println(payload);
}, 1).isOk();

// Topic and payload (useful with wildcards)
mqtt.subscribe("sensor/+/data", [](const String& topic, const String& payload) {
    Serial.printf("%s: %s\n", topic.c_str(), payload.c_str());
}).isOk();

// Did the broker accept it?
if (mqtt.isSubscriptionConfirmed("device/command")) {
    int qos = mqtt.getSubscriptionQos("device/command");  // 0-2, 0x80 = rejected
}
```

`subscribe()` returns `MQTTError::SUBSCRIBE_FAILED` when the SUBSCRIBE could not be sent
(transport down, outbox full). Callbacks run in the MQTT client's task context, so keep them
short and hand longer work off to your own task.

### Interface-based handlers

```cpp
class MyHandler : public IMqttMessageHandler {
public:
    void handleMessage(const std::string& topic, const std::string& payload) override {
        // ...
    }
};

MyHandler handler;
mqtt.registerTopicHandler("legacy/topic", &handler);   // registers and subscribes (QoS 0)
```

The handler is called directly when a message arrives. The message is also copied into the
message queue (topic up to 99 chars, payload up to 255 chars, 10 entries). That queue is fed
only by `registerTopicHandler()` / `subscribe(topic)` subscriptions, not by the callback
overloads.

### Events

```cpp
mqtt.registerEventCallback([](MQTTManager::MQTTEvent event, void* data) {
    switch (event) {
        case MQTTManager::MQTTEvent::CONNECTED:    break;
        case MQTTManager::MQTTEvent::DISCONNECTED: break;
        case MQTTManager::MQTTEvent::MESSAGE_RECEIVED: {
            auto* m = static_cast<MQTTManager::MessageEventData*>(data);  // topic, payload
            break;
        }
        case MQTTManager::MQTTEvent::SUBSCRIBE_COMPLETE: {
            auto* s = static_cast<MQTTManager::SubscribeAckEventData*>(data);  // msgId, topic, grantedQos
            break;
        }
        case MQTTManager::MQTTEvent::ERROR: {
            auto* e = static_cast<MQTTManager::ErrorEventData*>(data);  // error, message
            break;
        }
        default: break;   // PUBLISH_COMPLETE is declared but not emitted yet
    }
});
```

`data` points to stack data that is only valid during the callback; copy anything you need.
`ERROR` is emitted when auto-reconnect gives up after `maxAttempts`.

Alternatively, wait on the manager's own event group (`MQTT_CONNECTED_BIT`,
`MQTT_DISCONNECTED_BIT`, `MQTT_MESSAGE_RECEIVED_BIT`):

```cpp
EventBits_t bits = xEventGroupWaitBits(mqtt.getEventGroup(),
    MQTTManager::MQTT_CONNECTED_BIT | MQTTManager::MQTT_MESSAGE_RECEIVED_BIT,
    pdTRUE, pdFALSE, portMAX_DELAY);
```

You can also have it set bits in an event group you own:

```cpp
mqtt.setEventGroup(myEventGroup);
mqtt.setEventBits(MY_MQTT_UP_BIT, MY_MQTT_DOWN_BIT, MY_MQTT_MSG_BIT);   // 0 = don't set
```

### Reconnecting

Auto-reconnect is on by default. To tune it:

```cpp
MQTTManager::ReconnectConfig rc;
rc.minInterval = 1000;        // ms
rc.maxInterval = 30000;       // ms
rc.maxAttempts = 10;
rc.exponentialBackoff = true;
mqtt.setAutoReconnect(true, rc);
```

### Disconnecting

```cpp
if (mqtt.disconnect()) {
    // A live client was torn down and its resources released (e.g. before an OTA transfer).
}
```

`disconnect()` returns `false` when there was nothing to tear down, or when the instance
mutex could not be taken within 5 s. Use its return value rather than `isConnected()` to know
whether resources were freed. esp-mqtt's network timeout is set to 5 s
(`MQTT_NETWORK_TIMEOUT_MS`), so a teardown fits inside espota's 10 s connect-back window.

## API Reference

### Setup and connection

| Method | Returns | Notes |
|---|---|---|
| `getInstance()` | `MQTTManager&` | The singleton |
| `begin(uri, clientId, user, pass)` | `MQTTResult<void>` | Stores config; `clientId`/`user`/`pass` optional |
| `begin(const MQTTConfig&)` | `MQTTResult<void>` | Builder form |
| `connect()` | `MQTTResult<void>` | `NOT_INITIALIZED` before `begin()`, `ALREADY_CONNECTED` |
| `disconnect()` | `bool` | `true` only if a live client was torn down |
| `isConnected()` / `isInitialized()` | `bool` | |
| `waitForConnection(timeoutMs = 5000)` | `bool` | Blocks on `MQTT_CONNECTED_BIT` |

### Messaging

| Method | Returns | Notes |
|---|---|---|
| `publish(topic, payload, qos = 0, retain = false)` | `MQTTResult<void>` | `const char*` and `String` payloads; `TIMEOUT` = dropped |
| `subscribe(topic, cb(payload), qos = 0)` | `MQTTResult<void>` | |
| `subscribe(topic, cb(topic, payload), qos = 0)` | `MQTTResult<void>` | |
| `subscribe(topic)` | `MQTTResult<void>` | Interface-handler/queue path, QoS 0 |
| `registerTopicHandler(topic, IMqttMessageHandler*)` | `bool` | Registers and subscribes |
| `unsubscribe(topic)` | `bool` | |
| `isSubscriptionConfirmed(topic)` | `bool` | SUBACK received |
| `getSubscriptionQos(topic)` | `int` | 0-2 granted, -1 pending, 0x80 rejected, -2 unknown |
| `processMessages(max = 1, timeout = 0)` | `bool` | Drains the queue into registered handlers |
| `getMessageQueue()` / `clearMessageQueue()` | | Queue of `MqttMessage` |

### Configuration and events

| Method | Notes |
|---|---|
| `setKeepAlive(seconds)` | Same as `MQTTConfig::withKeepAlive` |
| `setLastWill(topic, payload, qos, retain)` | QoS is ignored; ESP32-MQTTClient sends LWT at QoS 0 |
| `setAutoReconnect(enable[, ReconnectConfig])` | |
| `enableDebugging(enable)` | Turns on ESP32-MQTTClient's debug messages |
| `registerEventCallback(cb)` | `void(MQTTEvent, void* data)` |
| `getEventGroup()` | Internal group with `MQTT_*_BIT` |
| `setEventGroup(group)` / `setEventBits(conn, disc, msg)` | Mirror events into your own group |

### Errors (`MQTTError`)

`OK`, `NOT_INITIALIZED`, `ALREADY_CONNECTED`, `CONNECTION_FAILED`, `BROKER_UNREACHABLE`,
`PUBLISH_FAILED`, `SUBSCRIBE_FAILED`, `INVALID_PARAMETER`, `MEMORY_ALLOCATION_FAILED`,
`TIMEOUT`, `UNKNOWN_ERROR`.

### Tunables (`MQTTManager.h`)

| Constant | Value | Meaning |
|---|---|---|
| `MQTT_DEFAULT_TIMEOUT_MS` | 5000 | Max mutex wait in `publish()` / `disconnect()` |
| `MQTT_NETWORK_TIMEOUT_MS` | 5000 | esp-mqtt network operation timeout |
| `MQTT_EVENT_HANDLER_TIMEOUT_MS` | 100 | Mutex wait inside connect/disconnect handlers |
| `MQTT_DEFAULT_KEEPALIVE_S` | 30 | Default keep-alive |
| `MQTT_MAX_TOPIC_LENGTH` / `MQTT_MAX_PAYLOAD_LENGTH` | 100 / 256 | Queue entry buffer sizes |
| `MQTT_QUEUE_SIZE` | 10 | Message queue depth |

## Logging Configuration

This library supports flexible logging configuration:

### Using ESP-IDF Logging (Default)

No configuration needed. The library will use ESP-IDF logging by default.

```cpp
#include <MQTTManager.h>
// Automatically uses ESP_LOGE, ESP_LOGW, ESP_LOGI
// Debug/Verbose logs are suppressed in release builds
```

### Using Custom Logger

To use the custom Logger library, define `USE_CUSTOM_LOGGER` in your build flags:

```ini
build_flags = -DUSE_CUSTOM_LOGGER
lib_deps =
    https://github.com/packerlschupfer/ESP32-Logger.git        ; required with USE_CUSTOM_LOGGER
    https://github.com/packerlschupfer/ESP32-MQTTManager.git
```

ESP32-Logger is deliberately **not** listed in this library's `library.json`. That follows
the convention the other ESP32-* libraries use: the application decides whether to use the
custom logger, and supplies it. Builds that use ESP-IDF logging never have to download it.
With `USE_CUSTOM_LOGGER` defined, `MQTTManagerLogging.h` includes `<LogInterface.h>`, so
ESP32-Logger has to be in the application's `lib_deps`. Otherwise the build fails with
`fatal error: LogInterface.h: No such file or directory`.

The same error can also appear when ESP32-Logger *is* in `lib_deps` but this library got
into the build unintentionally. For example, another library includes `IMqttMessageHandler.h`
behind an `#ifdef`, and PlatformIO's dependency finder (which ignores `#ifdef`s in its default
`chain` mode) finds MQTTManager in the global library dir (`~/.platformio/lib`). MQTTManager is
then compiled without its own dependencies resolved. It doesn't show up under "Dependency
Graph" in the build output, and Logger's include path is missing for it. If the application
doesn't use MQTT, exclude it with `lib_ignore = ESP32-MQTTManager`.

Initialize the Logger once in the application. ESP32-Logger compiles its own
`LogInterface` implementation, so nothing else needs including:

```cpp
#include <Logger.h>
#include <MQTTManager.h>

void setup() {
    // Initialize Logger once for all libraries
    Logger::getInstance().init(1024);

    // MQTTManager now logs through the custom Logger
    MQTTManager::getInstance().begin("mqtt://broker.example.com").isOk();
}
```

### Debug Logging

To enable debug/verbose logging for this library, define `MQTTMANAGER_DEBUG`:

```ini
build_flags = -DMQTTMANAGER_DEBUG
```

### Complete Example

```ini
[env:esp32dev]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
board = esp32dev
framework = arduino

# For production (no debug logs)
build_flags = 
    ; -DMQTTMANAGER_DEBUG  ; Commented out for production

[env:esp32dev_debug]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
board = esp32dev
framework = arduino

# For development with debug logs
build_flags = 
    -DMQTTMANAGER_DEBUG     ; Enable debug logs for MQTTManager
    -DUSE_CUSTOM_LOGGER     ; Optional: Use custom logger
lib_deps =
    https://github.com/packerlschupfer/ESP32-Logger.git       ; Required only when USE_CUSTOM_LOGGER is defined
    https://github.com/packerlschupfer/ESP32-MQTTManager.git
```

### Advanced Debug Options

For fine-grained debugging, you can enable specific debug aspects:

```ini
build_flags = 
    -DMQTTMANAGER_DEBUG       ; General debug logs
    -DMQTTMANAGER_DEBUG_CONN  ; Connection state debugging
    -DMQTTMANAGER_DEBUG_QUEUE ; Message queue debugging
```

### Benefits

1. **Production Ready**: No debug overhead in release builds
2. **Zero Dependencies**: Works without Logger library by default
3. **Flexible Backend**: Seamlessly switches between ESP-IDF and custom logger
4. **Targeted Debugging**: Enable debug for specific libraries only

## Thread Safety

- Configuration, connection, publish and subscribe methods take an internal FreeRTOS mutex.
- `publish()` and `disconnect()` wait at most 5 s for it and give up with `TIMEOUT` / `false`.
- The connect/disconnect handlers that esp-mqtt runs inline (possibly inside a task that is
  already in `publish()`) wait at most 100 ms and never block on it. The connection flag is a
  `std::atomic<bool>` updated outside the mutex. This removes a self-deadlock where a stalled
  publish could hang its own task until the task watchdog fired.
- Subscription callbacks, interface handlers and the event callback run in the MQTT client's
  task context.

## License

This library is released under the **GNU General Public License v3.0**. See the [LICENSE](LICENSE) file for the full text.

Copyright (C) 2025-2026 packerlschupfer

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version. It is distributed WITHOUT ANY WARRANTY; without even the implied
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

