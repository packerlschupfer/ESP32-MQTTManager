# MQTTManager - CLAUDE.md

## Overview
Singleton MQTT client manager for ESP32 providing thread-safe MQTT communication with auto-reconnect, event groups, and message queuing.

## Key Features
- Meyer's singleton pattern
- Auto-reconnect with exponential backoff
- Event-driven callbacks
- FreeRTOS event groups and queues
- Result type using `common::Result<T, MQTTError>`
- Builder pattern for configuration (MQTTConfig)

## Architecture

### Components
- `MQTTManager` - Singleton manager
- `MQTTConfig` - Builder for configuration
- `ESP32MQTTClient` - Underlying transport
- Event groups for synchronization

## Usage

### Basic
```cpp
auto& mqtt = MQTTManager::getInstance();
mqtt.begin("mqtt://broker.local", "clientId", "user", "pass");
mqtt.subscribe("topic", [](const String& msg) {
    Serial.println(msg);
});
mqtt.publish("topic", "message");
```

### With Builder Pattern
```cpp
auto config = MQTTConfig("mqtt://broker.local")
    .withClientId("esp32-device")
    .withCredentials("user", "pass")
    .withKeepAlive(60)
    .withLastWill("status", "offline");

MQTTManager::getInstance().begin(config);
```

## Event Bits
```cpp
MQTT_CONNECTED_BIT     // BIT0
MQTT_DISCONNECTED_BIT  // BIT1
MQTT_MESSAGE_RECEIVED_BIT  // BIT2
```

## Error Codes
```cpp
enum class MQTTError {
    OK,
    NOT_INITIALIZED,
    ALREADY_CONNECTED,
    CONNECTION_FAILED,
    PUBLISH_FAILED,
    SUBSCRIBE_FAILED,
    TIMEOUT,
    // ...
};
```

## Thread Safety
- All public methods are mutex-protected
- Event groups for task synchronization
- Queue for message passing

## Build Configuration
```ini
build_flags =
    -DMQTTMANAGER_DEBUG  ; Enable debug logging
```
