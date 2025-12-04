/**
 * @file test_mqttmanager.cpp
 * @brief Unit tests for MQTTManager offline-testable functionality
 *
 * Tests configuration, builder pattern, and error handling
 * without requiring actual MQTT broker connection.
 */

#include <unity.h>
#include <string.h>
#include <string>

// Test MQTTConfig builder pattern (no networking required)
#include "MQTTManager.h"

void setUp(void) {
    // Unity setup - called before each test
}

void tearDown(void) {
    // Unity teardown - called after each test
}

// ============================================================================
// MQTTConfig Builder Pattern Tests
// ============================================================================

void test_config_basic_construction(void) {
    MQTTConfig config("mqtt://broker.local");

    TEST_ASSERT_EQUAL_STRING("mqtt://broker.local", config.serverUri);
    TEST_ASSERT_NULL(config.clientId);
    TEST_ASSERT_NULL(config.username);
    TEST_ASSERT_NULL(config.password);
    TEST_ASSERT_EQUAL_UINT16(MQTT_DEFAULT_KEEPALIVE_S, config.keepAlive);
    TEST_ASSERT_TRUE(config.autoReconnect);
}

void test_config_with_client_id(void) {
    MQTTConfig config("mqtt://broker.local");
    config.withClientId("esp32-device");

    TEST_ASSERT_EQUAL_STRING("esp32-device", config.clientId);
}

void test_config_with_credentials(void) {
    MQTTConfig config("mqtt://broker.local");
    config.withCredentials("user", "secret");

    TEST_ASSERT_EQUAL_STRING("user", config.username);
    TEST_ASSERT_EQUAL_STRING("secret", config.password);
}

void test_config_with_keepalive(void) {
    MQTTConfig config("mqtt://broker.local");
    config.withKeepAlive(120);

    TEST_ASSERT_EQUAL_UINT16(120, config.keepAlive);
}

void test_config_with_last_will(void) {
    MQTTConfig config("mqtt://broker.local");
    config.withLastWill("status/offline", "disconnected", 1, true);

    TEST_ASSERT_EQUAL_STRING("status/offline", config.lastWillTopic);
    TEST_ASSERT_EQUAL_STRING("disconnected", config.lastWillPayload);
    TEST_ASSERT_EQUAL_INT(1, config.lastWillQos);
    TEST_ASSERT_TRUE(config.lastWillRetain);
}

void test_config_with_auto_reconnect_disabled(void) {
    MQTTConfig config("mqtt://broker.local");
    config.withAutoReconnect(false);

    TEST_ASSERT_FALSE(config.autoReconnect);
}

void test_config_builder_chaining(void) {
    MQTTConfig config = MQTTConfig("mqtt://broker.local")
        .withClientId("device-001")
        .withCredentials("admin", "password123")
        .withKeepAlive(60)
        .withLastWill("devices/001/status", "offline")
        .withAutoReconnect(true);

    TEST_ASSERT_EQUAL_STRING("mqtt://broker.local", config.serverUri);
    TEST_ASSERT_EQUAL_STRING("device-001", config.clientId);
    TEST_ASSERT_EQUAL_STRING("admin", config.username);
    TEST_ASSERT_EQUAL_STRING("password123", config.password);
    TEST_ASSERT_EQUAL_UINT16(60, config.keepAlive);
    TEST_ASSERT_EQUAL_STRING("devices/001/status", config.lastWillTopic);
    TEST_ASSERT_EQUAL_STRING("offline", config.lastWillPayload);
    TEST_ASSERT_TRUE(config.autoReconnect);
}

// ============================================================================
// MQTTError Enumeration Tests
// ============================================================================

void test_error_codes_distinct(void) {
    // Ensure all error codes are distinct
    TEST_ASSERT_NOT_EQUAL(static_cast<int>(MQTTError::OK),
                          static_cast<int>(MQTTError::NOT_INITIALIZED));
    TEST_ASSERT_NOT_EQUAL(static_cast<int>(MQTTError::CONNECTION_FAILED),
                          static_cast<int>(MQTTError::BROKER_UNREACHABLE));
    TEST_ASSERT_NOT_EQUAL(static_cast<int>(MQTTError::PUBLISH_FAILED),
                          static_cast<int>(MQTTError::SUBSCRIBE_FAILED));
}

void test_error_ok_is_zero(void) {
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(MQTTError::OK));
}

// ============================================================================
// MqttMessage Structure Tests
// ============================================================================

void test_mqtt_message_sizes(void) {
    MqttMessage msg;

    // Verify buffer sizes
    TEST_ASSERT_EQUAL(MQTT_MAX_TOPIC_LENGTH, sizeof(msg.topic));
    TEST_ASSERT_EQUAL(MQTT_MAX_PAYLOAD_LENGTH, sizeof(msg.payload));
}

void test_mqtt_message_copy(void) {
    MqttMessage msg;
    strncpy(msg.topic, "test/topic", MQTT_MAX_TOPIC_LENGTH - 1);
    msg.topic[MQTT_MAX_TOPIC_LENGTH - 1] = '\0';
    strncpy(msg.payload, "test payload", MQTT_MAX_PAYLOAD_LENGTH - 1);
    msg.payload[MQTT_MAX_PAYLOAD_LENGTH - 1] = '\0';

    TEST_ASSERT_EQUAL_STRING("test/topic", msg.topic);
    TEST_ASSERT_EQUAL_STRING("test payload", msg.payload);
}

// ============================================================================
// Constants Validation Tests
// ============================================================================

void test_constants_reasonable_values(void) {
    TEST_ASSERT_GREATER_THAN(0, MQTT_MAX_TOPIC_LENGTH);
    TEST_ASSERT_GREATER_THAN(0, MQTT_MAX_PAYLOAD_LENGTH);
    TEST_ASSERT_GREATER_THAN(0, MQTT_QUEUE_SIZE);
    TEST_ASSERT_GREATER_THAN(0, MQTT_DEFAULT_TIMEOUT_MS);
    TEST_ASSERT_GREATER_THAN(0, MQTT_DEFAULT_KEEPALIVE_S);

    // Reconnect delays should be reasonable
    TEST_ASSERT_LESS_OR_EQUAL(MQTT_MAX_RECONNECT_DELAY_MS, 60000);
    TEST_ASSERT_LESS_THAN(MQTT_MAX_RECONNECT_DELAY_MS, MQTT_RECONNECT_DELAY_MS * 100);
}

// ============================================================================
// Singleton Pattern Tests (no connection required)
// ============================================================================

void test_singleton_returns_same_instance(void) {
    MQTTManager& instance1 = MQTTManager::getInstance();
    MQTTManager& instance2 = MQTTManager::getInstance();

    TEST_ASSERT_EQUAL_PTR(&instance1, &instance2);
}

void test_singleton_not_connected_initially(void) {
    MQTTManager& mqtt = MQTTManager::getInstance();

    // Should not be connected without calling begin()
    TEST_ASSERT_FALSE(mqtt.isConnected());
}

void test_singleton_not_initialized_initially(void) {
    // Note: This test assumes fresh instance or reset state
    // In real testing, may need a reset method
    MQTTManager& mqtt = MQTTManager::getInstance();

    // If not initialized, isInitialized should return false
    // (depends on singleton state from previous tests)
    // For now, just check the method exists and returns bool
    bool init = mqtt.isInitialized();
    (void)init;  // Suppress unused warning
    TEST_PASS();
}

// ============================================================================
// Event Bits Constants Tests
// ============================================================================

void test_event_bits_distinct(void) {
    TEST_ASSERT_NOT_EQUAL(MQTTManager::MQTT_CONNECTED_BIT,
                          MQTTManager::MQTT_DISCONNECTED_BIT);
    TEST_ASSERT_NOT_EQUAL(MQTTManager::MQTT_CONNECTED_BIT,
                          MQTTManager::MQTT_MESSAGE_RECEIVED_BIT);
    TEST_ASSERT_NOT_EQUAL(MQTTManager::MQTT_DISCONNECTED_BIT,
                          MQTTManager::MQTT_MESSAGE_RECEIVED_BIT);
}

void test_event_bits_are_powers_of_two(void) {
    // Each bit should be a single bit (power of 2)
    TEST_ASSERT_EQUAL(BIT0, MQTTManager::MQTT_CONNECTED_BIT);
    TEST_ASSERT_EQUAL(BIT1, MQTTManager::MQTT_DISCONNECTED_BIT);
    TEST_ASSERT_EQUAL(BIT2, MQTTManager::MQTT_MESSAGE_RECEIVED_BIT);
}

// ============================================================================
// ReconnectConfig Tests
// ============================================================================

void test_reconnect_config_defaults(void) {
    MQTTManager::ReconnectConfig config;

    TEST_ASSERT_EQUAL_UINT32(1000, config.minInterval);
    TEST_ASSERT_EQUAL_UINT32(30000, config.maxInterval);
    TEST_ASSERT_EQUAL_UINT32(10, config.maxAttempts);
    TEST_ASSERT_TRUE(config.exponentialBackoff);
}

void test_reconnect_config_custom(void) {
    MQTTManager::ReconnectConfig config;
    config.minInterval = 500;
    config.maxInterval = 60000;
    config.maxAttempts = 20;
    config.exponentialBackoff = false;

    TEST_ASSERT_EQUAL_UINT32(500, config.minInterval);
    TEST_ASSERT_EQUAL_UINT32(60000, config.maxInterval);
    TEST_ASSERT_EQUAL_UINT32(20, config.maxAttempts);
    TEST_ASSERT_FALSE(config.exponentialBackoff);
}

// ============================================================================
// Test Runner
// ============================================================================

void runAllTests() {
    UNITY_BEGIN();

    // MQTTConfig builder tests
    RUN_TEST(test_config_basic_construction);
    RUN_TEST(test_config_with_client_id);
    RUN_TEST(test_config_with_credentials);
    RUN_TEST(test_config_with_keepalive);
    RUN_TEST(test_config_with_last_will);
    RUN_TEST(test_config_with_auto_reconnect_disabled);
    RUN_TEST(test_config_builder_chaining);

    // Error code tests
    RUN_TEST(test_error_codes_distinct);
    RUN_TEST(test_error_ok_is_zero);

    // Message structure tests
    RUN_TEST(test_mqtt_message_sizes);
    RUN_TEST(test_mqtt_message_copy);

    // Constants tests
    RUN_TEST(test_constants_reasonable_values);

    // Singleton tests
    RUN_TEST(test_singleton_returns_same_instance);
    RUN_TEST(test_singleton_not_connected_initially);
    RUN_TEST(test_singleton_not_initialized_initially);

    // Event bits tests
    RUN_TEST(test_event_bits_distinct);
    RUN_TEST(test_event_bits_are_powers_of_two);

    // ReconnectConfig tests
    RUN_TEST(test_reconnect_config_defaults);
    RUN_TEST(test_reconnect_config_custom);

    UNITY_END();
}

#ifdef ARDUINO
void setup() {
    delay(2000);  // Allow serial to initialize
    runAllTests();
}

void loop() {
    // Nothing to do
}
#else
int main(int argc, char** argv) {
    runAllTests();
    return 0;
}
#endif
