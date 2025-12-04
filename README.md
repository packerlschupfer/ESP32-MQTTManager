# MQTTManager Library

A thread-safe MQTT manager library for ESP32 using FreeRTOS and ESP32MQTTClient.

## Overview

MQTTManager provides a high-level interface for MQTT communication on ESP32 devices. It wraps the ESP32MQTTClient library with additional features including event-driven architecture, message queuing, and legacy callback support.

## Features

- Thread-safe MQTT operations with FreeRTOS integration
- Event-driven architecture using FreeRTOS event groups
- Message queuing for asynchronous processing
- Multiple subscription callback styles (lambda and interface-based)
- Automatic client ID generation from ESP32 chip ID
- Last Will and Testament support
- Debug logging with conditional compilation
- Connection state management and monitoring

## Dependencies

- ESP32 Arduino Core
- ESP32MQTTClient library
- FreeRTOS (included with ESP32)

## Installation

### PlatformIO

Add to your `platformio.ini`:

```ini
lib_deps = 
    https://github.com/your-username/MQTTManager
```

### Arduino IDE

1. Download the library as a ZIP file
2. In Arduino IDE: Sketch → Include Library → Add .ZIP Library
3. Select the downloaded file

## Usage Examples

### Basic Connection

```cpp
#include <MQTTManager.h>

MQTTManager mqtt;

void setup() {
    // Basic connection
    mqtt.begin("mqtt://broker.hivemq.com:1883");
    mqtt.connect();
    
    // With authentication
    mqtt.begin("mqtt://broker.example.com:1883", "client_id", "username", "password");
    mqtt.connect();
}
```

### Publishing Messages

```cpp
// Publish with default QoS 0
mqtt.publish("sensor/temperature", "25.5");

// Publish with QoS and retain
mqtt.publish("sensor/status", "online", 1, true);

// Publish String object
String payload = "Hello World";
mqtt.publish("test/message", payload);
```

### Subscribing to Topics

```cpp
// Lambda callback with payload only
mqtt.subscribe("sensor/command", [](const String& payload) {
    Serial.println("Received: " + payload);
});

// Lambda callback with topic and payload
mqtt.subscribe("sensor/+/data", [](const String& topic, const String& payload) {
    Serial.printf("Topic: %s, Payload: %s\n", topic.c_str(), payload.c_str());
});

// Interface-based handler (legacy support)
class MyHandler : public IMqttMessageHandler {
    void handleMessage(const std::string& topic, const std::string& payload) override {
        // Handle message
    }
};

MyHandler handler;
mqtt.registerTopicHandler("legacy/topic", &handler);
```

### Event-Driven Processing

```cpp
void processTask(void* param) {
    MQTTManager* mqtt = (MQTTManager*)param;
    EventGroupHandle_t events = mqtt->getEventGroup();
    
    while (true) {
        EventBits_t bits = xEventGroupWaitBits(events,
            MQTTManager::MQTT_CONNECTED_BIT | MQTTManager::MQTT_MESSAGE_RECEIVED_BIT,
            pdTRUE, pdFALSE, portMAX_DELAY);
            
        if (bits & MQTTManager::MQTT_CONNECTED_BIT) {
            // Handle connection
        }
        
        if (bits & MQTTManager::MQTT_MESSAGE_RECEIVED_BIT) {
            MqttMessage msg;
            if (xQueueReceive(mqtt->getMessageQueue(), &msg, 0)) {
                // Process message
            }
        }
    }
}
```

### Configuration Options

```cpp
// Set keep-alive interval
mqtt.setKeepAlive(60);  // 60 seconds

// Configure Last Will
mqtt.setLastWill("device/status", "offline", 0, true);

// Enable debug output (requires MQTTMANAGER_DEBUG defined)
mqtt.enableDebugging(true);

// Wait for connection with timeout
if (mqtt.waitForConnection(5000)) {
    Serial.println("Connected!");
}
```

## API Reference

### Core Methods

- `begin(uri, clientID, username, password)` - Initialize MQTT connection parameters
- `connect()` - Initiate connection to broker
- `disconnect()` - Disconnect from broker
- `isConnected()` - Check connection status
- `publish(topic, payload, qos, retain)` - Publish message
- `subscribe(topic, callback, qos)` - Subscribe with callback
- `unsubscribe(topic)` - Unsubscribe from topic

### Configuration Methods

- `setKeepAlive(seconds)` - Set keep-alive interval
- `setLastWill(topic, payload, qos, retain)` - Configure Last Will
- `enableDebugging(enable)` - Enable/disable debug output

### Event Methods

- `waitForConnection(timeoutMs)` - Block until connected
- `getEventGroup()` - Get FreeRTOS event group handle
- `getMessageQueue()` - Get message queue handle

### Event Bits

- `MQTT_CONNECTED_BIT` - Set when connected
- `MQTT_DISCONNECTED_BIT` - Set when disconnected  
- `MQTT_MESSAGE_RECEIVED_BIT` - Set when message received

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
    Logger  ; Must include Logger library when using custom logger
    MQTTManager
```

Your application must include LogInterfaceImpl.h:

```cpp
// In your main application file
#include <Logger.h>
#include <LogInterfaceImpl.h>
#include <MQTTManager.h>

void setup() {
    // Initialize Logger once for all libraries
    Logger::getInstance().init(1024);
    
    // MQTTManager will now use your custom Logger
    MQTTManager mqtt;
    mqtt.begin("mqtt://broker.example.com");
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
platform = espressif32
board = esp32dev
framework = arduino

# For production (no debug logs)
build_flags = 
    ; -DMQTTMANAGER_DEBUG  ; Commented out for production

[env:esp32dev_debug]
platform = espressif32
board = esp32dev
framework = arduino

# For development with debug logs
build_flags = 
    -DMQTTMANAGER_DEBUG     ; Enable debug logs for MQTTManager
    -DUSE_CUSTOM_LOGGER     ; Optional: Use custom logger
lib_deps = 
    Logger                  ; Required only when USE_CUSTOM_LOGGER is defined
    MQTTManager
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

All public methods are designed to be thread-safe. The library uses:
- FreeRTOS event groups for signaling
- Queue for message passing
- Internal state protection

## Suggested Improvements

1. **Add mutex protection for thread safety** - Currently missing mutex protection for shared state
2. **Implement proper null pointer validation** - Add comprehensive null checks throughout
3. **Add const correctness** - Many methods should take const parameters
4. **Fix memory leak risks** - Queue messages may accumulate if not processed
5. **Add connection retry mechanism** - Auto-reconnect with backoff
6. **Implement proper error handling** - Return error codes instead of void
7. **Add parameter validation** - Validate topic/payload lengths
8. **Fix global state issues** - Remove static instance dependency
9. **Add timeout parameters** - Methods like publish() should support timeouts
10. **Implement proper cleanup** - Ensure all resources freed on destruction
11. **Add SSL/TLS configuration** - Support for secure connections
12. **Fix callback lifetime issues** - Lambdas may reference destroyed objects
13. **Add message buffer size configuration** - Currently hardcoded limits
14. **Implement QoS 2 support** - Currently limited by ESP32MQTTClient
15. **Add connection event callbacks** - User-defined connect/disconnect handlers

## License

GPL-3 License - See LICENSE file for details