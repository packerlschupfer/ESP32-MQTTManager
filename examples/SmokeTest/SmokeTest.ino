// SmokeTest: minimal compile-smoke example for ESP32-MQTTManager.
// Not meant to run against a real broker -- it just exercises the public API
// of MQTTManager.h so CI verifies the library compiles end-to-end.

#include <Arduino.h>
#include "MQTTManager.h"

void setup() {
    Serial.begin(115200);

    MQTTManager& mqtt = MQTTManager::getInstance();

    // Builder-pattern config + begin()
    auto config = MQTTConfig("mqtt://broker.local")
                      .withClientId("esp32-smoke")
                      .withCredentials("user", "pass")
                      .withKeepAlive(60)
                      .withLastWill("status/offline", "disconnected")
                      .withAutoReconnect(true);

    MQTTResult<void> beginResult = mqtt.begin(config);
    if (!beginResult) {
        Serial.println("begin() failed");
    }

    // Connection state queries
    Serial.printf("initialized=%d connected=%d\n",
                  static_cast<int>(mqtt.isInitialized()),
                  static_cast<int>(mqtt.isConnected()));

    // Connection control
    MQTTResult<void> connResult = mqtt.connect();
    (void)connResult;

    // Publish (both overloads)
    MQTTResult<void> pubA = mqtt.publish("test/topic", "hello", 0, false);
    MQTTResult<void> pubB = mqtt.publish("test/topic", String("world"), 1, true);
    (void)pubA;
    (void)pubB;

    // Subscribe (callback overloads + legacy)
    MQTTResult<void> subA = mqtt.subscribe("test/topic",
        [](const String& payload) { Serial.println(payload); }, 0);
    MQTTResult<void> subB = mqtt.subscribe("test/+/data",
        [](const String& topic, const String& payload) {
            Serial.print(topic);
            Serial.println(payload);
        }, 1);
    MQTTResult<void> subC = mqtt.subscribe("legacy/topic");
    (void)subA;
    (void)subB;
    (void)subC;

    // Misc configuration / accessors
    mqtt.setKeepAlive(30);
    mqtt.enableDebugging(false);
    mqtt.registerEventCallback(
        [](MQTTManager::MQTTEvent event, void* data) {
            (void)event;
            (void)data;
        });

    (void)mqtt.getEventGroup();
    (void)mqtt.getMessageQueue();
    (void)mqtt.isSubscriptionConfirmed("test/topic");
    (void)mqtt.getSubscriptionQos("test/topic");
}

void loop() {
    MQTTManager& mqtt = MQTTManager::getInstance();
    mqtt.processMessages(1, 0);
    if (mqtt.isConnected()) {
        MQTTResult<void> r = mqtt.publish("heartbeat", "alive");
        (void)r;
    }
    delay(1000);
}
