#ifndef MQTTMANAGER_H
#define MQTTMANAGER_H

// Include the dedicated logging configuration
#include "MQTTManagerLogging.h"

#include <Arduino.h>
#include "Result.h"  // common::Result from LibraryCommon
#include "ESP32MQTTClient.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "IMqttMessageHandler.h"
#include <unordered_map>
#include <string>
#include <functional>
#include "freertos/semphr.h"

// Maximum sizes for MQTT messages
constexpr size_t MQTT_MAX_TOPIC_LENGTH = 100;
constexpr size_t MQTT_MAX_PAYLOAD_LENGTH = 256;
constexpr size_t MQTT_QUEUE_SIZE = 10;
constexpr uint32_t MQTT_DEFAULT_TIMEOUT_MS = 5000;
constexpr uint16_t MQTT_DEFAULT_KEEPALIVE_S = 30;
constexpr uint32_t MQTT_RECONNECT_DELAY_MS = 1000;
constexpr uint32_t MQTT_MAX_RECONNECT_DELAY_MS = 30000;

/**
 * @struct MQTTConfig
 * @brief Builder pattern for MQTT connection configuration
 *
 * @example
 * auto config = MQTTConfig("mqtt://broker.local")
 *     .withClientId("esp32-device")
 *     .withCredentials("user", "pass")
 *     .withKeepAlive(60)
 *     .withLastWill("status/offline", "disconnected");
 *
 * MQTTManager::getInstance().begin(config);
 */
struct MQTTConfig {
    const char* serverUri = nullptr;
    const char* clientId = nullptr;
    const char* username = nullptr;
    const char* password = nullptr;
    const char* lastWillTopic = nullptr;
    const char* lastWillPayload = nullptr;
    uint16_t keepAlive = MQTT_DEFAULT_KEEPALIVE_S;
    int lastWillQos = 0;
    bool lastWillRetain = false;
    bool autoReconnect = true;

    explicit MQTTConfig(const char* uri) : serverUri(uri) {}

    MQTTConfig& withClientId(const char* id) { clientId = id; return *this; }
    MQTTConfig& withCredentials(const char* user, const char* pass) {
        username = user;
        password = pass;
        return *this;
    }
    MQTTConfig& withKeepAlive(uint16_t seconds) { keepAlive = seconds; return *this; }
    MQTTConfig& withLastWill(const char* topic, const char* payload, int qos = 0, bool retain = false) {
        lastWillTopic = topic;
        lastWillPayload = payload;
        lastWillQos = qos;
        lastWillRetain = retain;
        return *this;
    }
    MQTTConfig& withAutoReconnect(bool enable) { autoReconnect = enable; return *this; }
};

struct MqttMessage {
    char topic[MQTT_MAX_TOPIC_LENGTH];
    char payload[MQTT_MAX_PAYLOAD_LENGTH];
};

// Error codes for MQTT operations
enum class MQTTError {
    OK = 0,
    NOT_INITIALIZED,
    ALREADY_CONNECTED,
    CONNECTION_FAILED,
    BROKER_UNREACHABLE,
    PUBLISH_FAILED,
    SUBSCRIBE_FAILED,
    INVALID_PARAMETER,
    MEMORY_ALLOCATION_FAILED,
    TIMEOUT,
    UNKNOWN_ERROR
};

/**
 * @brief Result type for MQTT operations using common::Result
 * @tparam T The type of the value on success
 *
 * Uses common::Result from LibraryCommon with MQTTError as error type.
 */
template<typename T>
using MQTTResult = common::Result<T, MQTTError>;

// Forward declare the global callbacks
void onMqttConnect(esp_mqtt_client_handle_t client);
void handleMQTT(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);

/**
 * @class MQTTManager
 * @brief Singleton MQTT client manager for ESP32
 *
 * Provides thread-safe MQTT communication with:
 * - Auto-reconnect with exponential backoff
 * - Event-driven callbacks
 * - FreeRTOS integration (event groups, queues)
 *
 * Uses Meyer's singleton pattern for safe initialization.
 */
class MQTTManager {
public:
    // Event types
    enum class MQTTEvent {
        CONNECTED,
        DISCONNECTED,
        MESSAGE_RECEIVED,
        PUBLISH_COMPLETE,
        SUBSCRIBE_COMPLETE,
        ERROR
    };

    // Event data structures
    struct MessageEventData {
        const char* topic;
        const char* payload;
    };

    struct ErrorEventData {
        MQTTError error;
        const char* message;
    };

    struct SubscribeAckEventData {
        int msgId;
        const char* topic;
        int grantedQos;  // 0-2 = success, 0x80 = rejected by broker
    };

    // Event callback type
    using EventCallback = std::function<void(MQTTEvent event, void* data)>;

    /**
     * @brief Get the singleton instance
     * @return Reference to the MQTTManager singleton
     * @note Thread-safe (C++11 guarantee)
     */
    static MQTTManager& getInstance() {
        static MQTTManager instance;
        return instance;
    }

    // Delete copy/move operations
    MQTTManager(const MQTTManager&) = delete;
    MQTTManager& operator=(const MQTTManager&) = delete;
    MQTTManager(MQTTManager&&) = delete;
    MQTTManager& operator=(MQTTManager&&) = delete;

    ~MQTTManager();

    // Initialize MQTT connection
    [[nodiscard]] MQTTResult<void> begin(const char* serverUri, const char* clientID = nullptr,
               const char* username = nullptr, const char* password = nullptr);

    /**
     * @brief Initialize MQTT connection using builder config
     * @param config Configuration struct built with builder pattern
     * @return MQTTResult<void> Success or error
     *
     * @example
     * auto result = MQTTManager::getInstance().begin(
     *     MQTTConfig("mqtt://broker.local")
     *         .withClientId("esp32")
     *         .withCredentials("user", "pass")
     *         .withKeepAlive(60)
     * );
     */
    [[nodiscard]] MQTTResult<void> begin(const MQTTConfig& config);

    // Publish methods
    [[nodiscard]] MQTTResult<void> publish(const char* topic, const char* payload, int qos = 0, bool retain = false);
    [[nodiscard]] MQTTResult<void> publish(const char* topic, const String& payload, int qos = 0, bool retain = false);

    // Subscribe with callback
    [[nodiscard]] MQTTResult<void> subscribe(const char* topic, std::function<void(const String&)> callback, int qos = 0);
    [[nodiscard]] MQTTResult<void> subscribe(const char* topic, std::function<void(const String&, const String&)> callback, int qos = 0);

    // Legacy subscribe (for IMqttMessageHandler compatibility)
    [[nodiscard]] MQTTResult<void> subscribe(const char* topic);
    bool registerTopicHandler(const std::string& topic, IMqttMessageHandler* handler);

    // Connection control
    [[nodiscard]] MQTTResult<void> connect();
    void disconnect();
    bool isConnected() const noexcept;

    /**
     * @brief Check if MQTT manager has been initialized via begin()
     * @return true if begin() was successfully called
     */
    bool isInitialized() const noexcept { return initialized_; }
    
    // Configuration
    bool setKeepAlive(uint16_t seconds);
    bool setLastWill(const char* topic, const char* payload, int qos = 0, bool retain = false);
    void enableDebugging(bool enable = true);
    bool clearMessageQueue();
    
    // Event-driven configuration
    void registerEventCallback(EventCallback callback);
    void setEventGroup(EventGroupHandle_t eventGroup);
    void setEventBits(uint32_t connectedBit, uint32_t disconnectedBit, uint32_t messageBit);
    
    // Auto-reconnect configuration
    struct ReconnectConfig {
        uint32_t minInterval = 1000;      // ms
        uint32_t maxInterval = 30000;     // ms
        uint32_t maxAttempts = 10;
        bool exponentialBackoff = true;
    };
    void setAutoReconnect(bool enable);
    void setAutoReconnect(bool enable, const ReconnectConfig& config);
    
    // Unsubscribe method
    bool unsubscribe(const char* topic);

    /**
     * @brief Check if a subscription has been confirmed by the broker (SUBACK received)
     * @param topic The topic to check
     * @return true if broker acknowledged the subscription
     */
    bool isSubscriptionConfirmed(const char* topic) const;

    /**
     * @brief Get the QoS level granted by the broker for a subscription
     * @param topic The topic to check
     * @return Granted QoS (0-2), -1 if pending, 0x80 if rejected, -2 if not found
     */
    int getSubscriptionQos(const char* topic) const;
    
    // Event handlers (can be overridden)
    void onConnect();
    void onDisconnect();
    
    // Get event group for external synchronization
    EventGroupHandle_t getEventGroup() const noexcept { return mqttEventGroup; }

    // Get queue for message processing
    QueueHandle_t getMessageQueue() const noexcept { return mqttQueue; }
    
    // Wait for connection with timeout
    bool waitForConnection(uint32_t timeoutMs = 5000);
    
    // Non-blocking message processing
    bool processMessages(uint32_t maxMessages = 1, TickType_t timeout = 0);
    
    // Event bits
    static constexpr EventBits_t MQTT_CONNECTED_BIT = BIT0;
    static constexpr EventBits_t MQTT_DISCONNECTED_BIT = BIT1;
    static constexpr EventBits_t MQTT_MESSAGE_RECEIVED_BIT = BIT2;
    
    // Public for global callbacks
    ESP32MQTTClient* mqttClient;

    // Friend function for global callbacks
    friend void onMqttConnect(esp_mqtt_client_handle_t client);

private:
    MQTTManager();
    EventGroupHandle_t mqttEventGroup;
    QueueHandle_t mqttQueue;
    
    // Legacy topic handlers for IMqttMessageHandler compatibility
    std::unordered_map<std::string, IMqttMessageHandler*> topicHandlers;
    
    // Connection parameters - store the full URI with credentials
    std::string fullUri;  // This will hold the complete URI including credentials
    std::string serverUri;
    std::string cleanedUri_;  // URI with mqtt:// prefix (persists for ESP32MQTTClient)
    std::string clientID;
    std::string username;
    std::string password;
    
    // Last will configuration
    std::string lastWillTopic;
    std::string lastWillPayload;
    bool lastWillRetain = false;
    
    // State
    bool initialized_ = false;  // Track if begin() was successfully called
    bool connected;
    bool loopStarted = false;  // Track if loopStart() has been called
    bool debugEnabled = false;
    bool autoReconnect = true;
    ReconnectConfig reconnectConfig;
    uint32_t currentReconnectDelay = MQTT_RECONNECT_DELAY_MS;
    uint32_t reconnectAttempts = 0;
    
    // Thread safety
    SemaphoreHandle_t mutex;
    
    // Tasks
    TaskHandle_t reconnectTask = nullptr;
    TimerHandle_t reconnectTimer = nullptr;
    
    // Event-driven support
    EventCallback eventCallback = nullptr;
    EventGroupHandle_t externalEventGroup = nullptr;
    uint32_t externalConnectedBit = 0;
    uint32_t externalDisconnectedBit = 0;
    uint32_t externalMessageBit = 0;
    
    // Helper methods
    void configureClient();
    bool isValidTopic(const char* topic) const;
    bool isValidPayload(const char* payload, size_t maxLen = MQTT_MAX_PAYLOAD_LENGTH) const;
    
    // Reconnection task
    static void reconnectTaskFunc(void* param);
    static void reconnectTimerCallback(TimerHandle_t xTimer);
    
    // Event notification
    void notifyEvent(MQTTEvent event, void* data = nullptr);
};

#endif // MQTTMANAGER_H