#include "MQTTManager.h"
#include <algorithm>

// Global ESP32MQTTClient instance - MUST be global for the library to work properly
static ESP32MQTTClient globalMqttClient;

MQTTManager::MQTTManager() : mqttClient(&globalMqttClient), connected(false), loopStarted(false) {
    mutex = xSemaphoreCreateMutex();
    if (!mutex) {
        MQTTM_LOG_E("Failed to create mutex");
    }
    
    mqttEventGroup = xEventGroupCreate();
    if (!mqttEventGroup) {
        MQTTM_LOG_E("Failed to create event group");
    }
    
    mqttQueue = xQueueCreate(MQTT_QUEUE_SIZE, sizeof(MqttMessage));
    if (!mqttQueue) {
        MQTTM_LOG_E("Failed to create message queue");
    }
}

MQTTManager::~MQTTManager() {
    disconnect();
    
    // Stop auto-reconnect and delete task if running
    autoReconnect = false;
    if (reconnectTask) {
        vTaskDelete(reconnectTask);
        reconnectTask = nullptr;
    }
    
    // Stop and delete reconnect timer if running
    if (reconnectTimer) {
        xTimerStop(reconnectTimer, 0);
        xTimerDelete(reconnectTimer, 0);
        reconnectTimer = nullptr;
    }
    
    // Take mutex before cleanup
    if (mutex) {
        xSemaphoreTake(mutex, portMAX_DELAY);
    }
    
    // Don't delete mqttClient as it points to the global instance
    mqttClient = nullptr;
    
    if (mqttEventGroup) {
        vEventGroupDelete(mqttEventGroup);
        mqttEventGroup = nullptr;
    }
    
    if (mqttQueue) {
        // Clear any remaining messages
        MqttMessage msg;
        while (xQueueReceive(mqttQueue, &msg, 0) == pdTRUE) {
            // Message cleared
        }
        vQueueDelete(mqttQueue);
        mqttQueue = nullptr;
    }
    
    if (mutex) {
        xSemaphoreGive(mutex);
        vSemaphoreDelete(mutex);
        mutex = nullptr;
    }
}

MQTTResult<void> MQTTManager::begin(const char* serverUri, const char* clientID,
                        const char* username, const char* password) {
    // Validate input
    if (!serverUri || strlen(serverUri) == 0) {
        MQTTM_LOG_E("Invalid server URI provided");
        return MQTTResult<void>::error(MQTTError::INVALID_PARAMETER);
    }

    if (!mutex) {
        MQTTM_LOG_E("Mutex not initialized");
        return MQTTResult<void>::error(MQTTError::NOT_INITIALIZED);
    }
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    MQTTM_LOG_D("Begin called with URI: %s", serverUri);
    
    // Store parameters
    this->serverUri = serverUri;
    
    if (clientID && strlen(clientID) > 0) {
        this->clientID = clientID;
    } else {
        // Generate a unique client ID if not provided
        uint64_t chipid = ESP.getEfuseMac();
        char chipIdStr[17];
        snprintf(chipIdStr, sizeof(chipIdStr), "%016llX", chipid);
        this->clientID = std::string("ESP32_") + chipIdStr;
    }
    
    MQTTM_LOG_D("Client ID: %s", this->clientID.c_str());
    
    if (username && strlen(username) > 0) this->username = username;
    if (password && strlen(password) > 0) this->password = password;
    
    // Build the full URI with credentials and store it persistently
    this->fullUri = serverUri;
    if (!this->username.empty() && !this->password.empty()) {
        // Parse the URI to insert credentials
        if (this->fullUri.find("mqtt://") == 0) {
            this->fullUri = "mqtt://" + this->username + ":" + 
                            this->password + "@" + this->fullUri.substr(7);
        } else if (this->fullUri.find("mqtts://") == 0) {
            this->fullUri = "mqtts://" + this->username + ":" + 
                            this->password + "@" + this->fullUri.substr(8);
        }
    }
    
    MQTTM_LOG_D("Full URI: %s", this->fullUri.c_str());
    
    // Configure the global MQTT client
    configureClient();

    // Mark as initialized
    initialized_ = true;

    MQTTM_LOG_I("MQTT Manager configured with URI: %s", serverUri);

    xSemaphoreGive(mutex);
    return MQTTResult<void>::ok();
}

MQTTResult<void> MQTTManager::begin(const MQTTConfig& config) {
    // Use the existing begin() for basic initialization
    auto result = begin(config.serverUri, config.clientId, config.username, config.password);
    if (!result.isOk()) {
        return result;
    }

    // Apply additional configuration from builder
    if (config.keepAlive != MQTT_DEFAULT_KEEPALIVE_S) {
        setKeepAlive(config.keepAlive);
    }

    if (config.lastWillTopic && config.lastWillPayload) {
        setLastWill(config.lastWillTopic, config.lastWillPayload,
                   config.lastWillQos, config.lastWillRetain);
    }

    setAutoReconnect(config.autoReconnect);

    return MQTTResult<void>::ok();
}

void MQTTManager::configureClient() {
    if (!mqttClient) {
        MQTTM_LOG_E("MQTT client is null");
        return;
    }

    // Enable debugging if needed
    #ifdef MQTTMANAGER_DEBUG
    if (debugEnabled) {
        mqttClient->enableDebuggingMessages();
    }
    #endif

    // Set the URI with separate username and password
    MQTTM_LOG_I("Setting URI: %s", this->serverUri.c_str());
    MQTTM_LOG_I("Setting username: %s", this->username.c_str());
    MQTTM_LOG_I("Setting password: %s", this->password.empty() ? "(empty)" : "****");

    // Ensure URI has proper format - store in member to persist for ESP32MQTTClient
    // NOTE: ESP32MQTTClient stores raw pointers, so the string must persist!
    cleanedUri_ = this->serverUri;
    if (cleanedUri_.find("mqtt://") != 0 && cleanedUri_.find("mqtts://") != 0) {
        cleanedUri_ = "mqtt://" + cleanedUri_;
        MQTTM_LOG_I("Added mqtt:// prefix to URI: %s", cleanedUri_.c_str());
    }

    mqttClient->setURI(cleanedUri_.c_str(), this->username.c_str(), this->password.c_str());

    // Set the client ID
    if (!this->clientID.empty()) {
        MQTTM_LOG_I("Setting client ID: %s", this->clientID.c_str());
        mqttClient->setMqttClientName(this->clientID.c_str());
    }

    // Set keep alive
    mqttClient->setKeepAlive(MQTT_DEFAULT_KEEPALIVE_S);

    // Set last will if configured
    if (!lastWillTopic.empty()) {
        MQTTM_LOG_D("Setting last will - topic: %s", lastWillTopic.c_str());
        mqttClient->enableLastWillMessage(lastWillTopic.c_str(),
                                         lastWillPayload.c_str(),
                                         lastWillRetain);
    }

    // Set up SUBACK callback to forward subscription acknowledgments
    mqttClient->setSubscribeAckCallback([this](int msgId, const std::string& topic, int grantedQos) {
        MQTTM_LOG_I("SUBACK received: topic=%s, msg_id=%d, qos=%d", topic.c_str(), msgId, grantedQos);

        // Notify via event system
        SubscribeAckEventData eventData = { msgId, topic.c_str(), grantedQos };
        notifyEvent(MQTTEvent::SUBSCRIBE_COMPLETE, &eventData);

        // Log warning if subscription was rejected
        if (grantedQos == 0x80) {
            MQTTM_LOG_E("Subscription REJECTED by broker: %s", topic.c_str());
        }
    });
}

MQTTResult<void> MQTTManager::connect() {
    if (!mutex) return MQTTResult<void>::error(MQTTError::NOT_INITIALIZED);

    xSemaphoreTake(mutex, portMAX_DELAY);

    if (connected) {
        MQTTM_LOG_I("Already connected");
        xSemaphoreGive(mutex);
        return MQTTResult<void>::error(MQTTError::ALREADY_CONNECTED);
    }

    if (this->fullUri.empty()) {
        MQTTM_LOG_E("Cannot connect - URI not configured. Call begin() first.");
        xSemaphoreGive(mutex);
        return MQTTResult<void>::error(MQTTError::NOT_INITIALIZED);
    }

    if (!mqttClient) {
        MQTTM_LOG_E("MQTT client is null");
        xSemaphoreGive(mutex);
        return MQTTResult<void>::error(MQTTError::NOT_INITIALIZED);
    }

    // Only start the loop once
    if (!loopStarted) {
        MQTTM_LOG_I("Starting MQTT client loop...");
        bool success = mqttClient->loopStart();
        if (success) {
            loopStarted = true;
            MQTTM_LOG_I("MQTT client loop started successfully");
        } else {
            MQTTM_LOG_E("Failed to start MQTT client loop");
            xSemaphoreGive(mutex);
            return MQTTResult<void>::error(MQTTError::CONNECTION_FAILED);
        }
    } else {
        MQTTM_LOG_I("MQTT client loop already running, triggering reconnection");
    }

    MQTTM_LOG_I("MQTT connection initiated");
    xSemaphoreGive(mutex);
    return MQTTResult<void>::ok();
}

void MQTTManager::disconnect() {
    if (!mutex) return;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    if (mqttClient && mqttClient->isConnected()) {
        MQTTM_LOG_I("Disconnecting from MQTT broker");
        mqttClient->disconnect();
        loopStarted = false;
    }
    
    if (connected) {
        connected = false;
        if (mqttEventGroup) {
            xEventGroupSetBits(mqttEventGroup, MQTT_DISCONNECTED_BIT);
            xEventGroupClearBits(mqttEventGroup, MQTT_CONNECTED_BIT);
        }
    }
    
    xSemaphoreGive(mutex);
}

bool MQTTManager::isConnected() const {
    // Use event group bits instead of internal connected flag
    // This is more reliable as the event bits are properly set/cleared
    if (!mqttEventGroup) {
        MQTTM_LOG_E("isConnected() called but event group is NULL");
        return false;
    }
    
    // Note: xEventGroupGetBits is thread-safe and doesn't require mutex
    EventBits_t bits = xEventGroupGetBits(mqttEventGroup);
    bool isConnected = (bits & MQTT_CONNECTED_BIT) != 0;
    
    return isConnected;
}

MQTTResult<void> MQTTManager::publish(const char* topic, const char* payload, int qos, bool retain) {
    if (!topic || !payload) {
        MQTTM_LOG_E("Invalid topic or payload");
        return MQTTResult<void>::error(MQTTError::INVALID_PARAMETER);
    }

    if (strlen(topic) == 0) {
        MQTTM_LOG_E("Empty topic");
        return MQTTResult<void>::error(MQTTError::INVALID_PARAMETER);
    }

    if (!isConnected()) {
        MQTTM_LOG_E("Cannot publish - not connected");
        return MQTTResult<void>::error(MQTTError::CONNECTION_FAILED);
    }

    if (!mutex) return MQTTResult<void>::error(MQTTError::NOT_INITIALIZED);

    xSemaphoreTake(mutex, portMAX_DELAY);

    if (!mqttClient) {
        MQTTM_LOG_E("MQTT client is null");
        xSemaphoreGive(mutex);
        return MQTTResult<void>::error(MQTTError::NOT_INITIALIZED);
    }

    bool publishResult = mqttClient->publish(topic, payload, qos, retain);
    if (publishResult) {  // true means success
        MQTTM_LOG_D("Published to %s: %s", topic, payload);
        xSemaphoreGive(mutex);
        return MQTTResult<void>::ok();
    } else {
        MQTTM_LOG_E("Failed to publish to %s", topic);
        xSemaphoreGive(mutex);
        return MQTTResult<void>::error(MQTTError::PUBLISH_FAILED);
    }
}

MQTTResult<void> MQTTManager::publish(const char* topic, const String& payload, int qos, bool retain) {
    return publish(topic, payload.c_str(), qos, retain);
}

MQTTResult<void> MQTTManager::subscribe(const char* topic, std::function<void(const String&)> callback, int qos) {
    if (!topic || strlen(topic) == 0) {
        MQTTM_LOG_E("Invalid topic");
        return MQTTResult<void>::error(MQTTError::INVALID_PARAMETER);
    }

    if (!callback) {
        MQTTM_LOG_E("Invalid callback");
        return MQTTResult<void>::error(MQTTError::INVALID_PARAMETER);
    }

    if (!mutex) return MQTTResult<void>::error(MQTTError::NOT_INITIALIZED);

    xSemaphoreTake(mutex, portMAX_DELAY);

    if (!mqttClient) {
        MQTTM_LOG_E("Cannot subscribe - MQTT client not initialized");
        xSemaphoreGive(mutex);
        return MQTTResult<void>::error(MQTTError::NOT_INITIALIZED);
    }

    std::string topicStr(topic);
    // ESP32MQTTClient uses std::string, convert to Arduino String for callback
    (void)mqttClient->subscribe(topicStr, [this, callback, topicStr](const std::string& payload) {
        // Convert std::string to Arduino String for callback
        String payloadArduino(payload.c_str());
        callback(payloadArduino);

        // Notify message received event
        MessageEventData msgData = { topicStr.c_str(), payload.c_str() };
        notifyEvent(MQTTEvent::MESSAGE_RECEIVED, &msgData);
    }, qos);
    MQTTM_LOG_I("Subscribed to topic: %s", topic);

    xSemaphoreGive(mutex);
    return MQTTResult<void>::ok();
}

MQTTResult<void> MQTTManager::subscribe(const char* topic, std::function<void(const String&, const String&)> callback, int qos) {
    if (!topic || strlen(topic) == 0) {
        MQTTM_LOG_E("Invalid topic");
        return MQTTResult<void>::error(MQTTError::INVALID_PARAMETER);
    }

    if (!callback) {
        MQTTM_LOG_E("Invalid callback");
        return MQTTResult<void>::error(MQTTError::INVALID_PARAMETER);
    }

    if (!mutex) return MQTTResult<void>::error(MQTTError::NOT_INITIALIZED);

    xSemaphoreTake(mutex, portMAX_DELAY);

    if (!mqttClient) {
        MQTTM_LOG_E("Cannot subscribe - MQTT client not initialized");
        xSemaphoreGive(mutex);
        return MQTTResult<void>::error(MQTTError::NOT_INITIALIZED);
    }

    std::string topicStr(topic);
    // ESP32MQTTClient uses std::string, convert to Arduino String for callback
    (void)mqttClient->subscribe(topicStr, [this, callback, topicStr](const std::string& topic, const std::string& payload) {
        // Convert std::string to Arduino String for callback
        String topicArduino(topic.c_str());
        String payloadArduino(payload.c_str());
        callback(topicArduino, payloadArduino);

        // Notify message received event
        MessageEventData msgData = { topicStr.c_str(), payload.c_str() };
        notifyEvent(MQTTEvent::MESSAGE_RECEIVED, &msgData);
    }, qos);
    MQTTM_LOG_I("Subscribed to topic with topic callback: %s", topic);

    xSemaphoreGive(mutex);
    return MQTTResult<void>::ok();
}

bool MQTTManager::unsubscribe(const char* topic) {
    if (!topic || strlen(topic) == 0) {
        MQTTM_LOG_E("Invalid topic");
        return false;
    }
    
    if (!mutex) return false;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    if (!mqttClient) {
        MQTTM_LOG_E("Cannot unsubscribe - MQTT client not initialized");
        xSemaphoreGive(mutex);
        return false;
    }
    
    (void)mqttClient->unsubscribe(topic);
    MQTTM_LOG_I("Unsubscribed from topic: %s", topic);
    
    xSemaphoreGive(mutex);
    return true;
}

// Legacy subscribe for IMqttMessageHandler compatibility
MQTTResult<void> MQTTManager::subscribe(const char* topic) {
    if (!topic || strlen(topic) == 0) {
        MQTTM_LOG_E("Invalid topic");
        return MQTTResult<void>::error(MQTTError::INVALID_PARAMETER);
    }

    if (!mqttClient || !mutex || !mqttEventGroup || !mqttQueue) {
        MQTTM_LOG_E("MQTT Manager not properly initialized");
        return MQTTResult<void>::error(MQTTError::NOT_INITIALIZED);
    }

    xSemaphoreTake(mutex, portMAX_DELAY);

    std::string topicStr(topic);
    // ESP32MQTTClient uses std::string
    (void)mqttClient->subscribe(topicStr, [this, topicStr](const std::string& payload) {
        // Handle message through legacy topic handlers
        auto it = topicHandlers.find(topicStr);
        if (it != topicHandlers.end() && it->second != nullptr) {
            it->second->handleMessage(topicStr.c_str(), payload.c_str());
        }

        // Queue the message if needed
        MqttMessage msg;
        strncpy(msg.topic, topicStr.c_str(), sizeof(msg.topic) - 1);
        strncpy(msg.payload, payload.c_str(), sizeof(msg.payload) - 1);
        msg.topic[sizeof(msg.topic) - 1] = '\0';
        msg.payload[sizeof(msg.payload) - 1] = '\0';

        // Check if queue is full before sending
        if (uxQueueSpacesAvailable(mqttQueue) > 0) {
            xQueueSend(mqttQueue, &msg, 0);
        } else {
            MQTTM_LOG_E("Message queue full, dropping message");
        }

        // Notify message received event
        MessageEventData msgData = { topicStr.c_str(), payload.c_str() };
        notifyEvent(MQTTEvent::MESSAGE_RECEIVED, &msgData);
    });

    MQTTM_LOG_I("Subscribed to topic (legacy): %s", topic);

    xSemaphoreGive(mutex);
    return MQTTResult<void>::ok();
}

bool MQTTManager::registerTopicHandler(const std::string& topic, IMqttMessageHandler* handler) {
    if (topic.empty()) {
        MQTTM_LOG_E("Invalid topic");
        return false;
    }
    
    if (!handler) {
        MQTTM_LOG_E("Invalid handler");
        return false;
    }
    
    if (!mutex) return false;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    topicHandlers[topic] = handler;
    xSemaphoreGive(mutex);
    
    // Also subscribe to the topic
    return subscribe(topic.c_str()).isOk();
}

bool MQTTManager::setKeepAlive(uint16_t seconds) {
    if (seconds == 0) {
        MQTTM_LOG_E("Invalid keep-alive value");
        return false;
    }
    
    if (!mutex) return false;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    if (!mqttClient) {
        MQTTM_LOG_E("MQTT client not initialized");
        xSemaphoreGive(mutex);
        return false;
    }
    
    mqttClient->setKeepAlive(seconds);
    xSemaphoreGive(mutex);
    return true;
}

bool MQTTManager::setLastWill(const char* topic, const char* payload, int qos, bool retain) {
    if (!topic || strlen(topic) == 0) {
        MQTTM_LOG_E("Invalid last will topic");
        return false;
    }
    
    if (!mutex) return false;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    // Store for later use
    lastWillTopic = topic;
    lastWillPayload = payload ? payload : "";
    lastWillRetain = retain;
    
    // If client already exists, set it now
    if (mqttClient) {
        mqttClient->enableLastWillMessage(topic, payload, retain);
        if (qos != 0) {
            MQTTM_LOG_I("Note: ESP32MQTTClient does not support QoS for Last Will messages");
        }
    }
    
    xSemaphoreGive(mutex);
    return true;
}

void MQTTManager::enableDebugging(bool enable) {
    if (!mutex) return;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    debugEnabled = enable;
    if (mqttClient && enable) {
        mqttClient->enableDebuggingMessages();
    }
    xSemaphoreGive(mutex);
}

void MQTTManager::onConnect() {
    if (!mutex) return;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    connected = true;
    
    MQTTM_LOG_I("MQTT connected");
    xSemaphoreGive(mutex);
    
    // Notify event (outside mutex to avoid deadlock)
    notifyEvent(MQTTEvent::CONNECTED);
    
    // Stop reconnect timer if running
    if (reconnectTimer) {
        xTimerStop(reconnectTimer, 0);
    }
}

void MQTTManager::onDisconnect() {
    if (!mutex) return;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    connected = false;
    
    MQTTM_LOG_I("MQTT disconnected");
    
    // Start reconnection timer if auto-reconnect is enabled
    if (autoReconnect && reconnectTimer) {
        MQTTM_LOG_I("Starting reconnection timer");
        xTimerStart(reconnectTimer, 0);
    } else if (autoReconnect && !reconnectTimer) {
        // Fallback to task-based reconnection if timer not configured
        if (!reconnectTask) {
            MQTTM_LOG_I("Starting reconnection task");
            xTaskCreate(reconnectTaskFunc, "mqtt_reconnect", 4096, this, 1, &reconnectTask);
        } else {
            MQTTM_LOG_I("Reconnection task already running");
        }
    }
    
    xSemaphoreGive(mutex);
    
    // Notify event (outside mutex to avoid deadlock)
    notifyEvent(MQTTEvent::DISCONNECTED);
}

bool MQTTManager::waitForConnection(uint32_t timeoutMs) {
    if (!mqttEventGroup) {
        MQTTM_LOG_E("Event group not initialized");
        return false;
    }
    
    if (timeoutMs == 0) {
        MQTTM_LOG_E("Invalid timeout value");
        return false;
    }
    
    EventBits_t bits = xEventGroupWaitBits(
        mqttEventGroup,
        MQTT_CONNECTED_BIT,
        pdFALSE,  // Don't clear the bit
        pdTRUE,   // Wait for all bits
        pdMS_TO_TICKS(timeoutMs)
    );
    
    return (bits & MQTT_CONNECTED_BIT) != 0;
}

// Global callback functions that ESP32MQTTClient expects
void onMqttConnect(esp_mqtt_client_handle_t client) {
    if (!client) {
        MQTTM_LOG_E("onMqttConnect called with null client");
        return;
    }

    auto& manager = MQTTManager::getInstance();

    // Always call onConnect() for the singleton instance
    // The ESP32MQTTClient library only supports one client anyway
    manager.onConnect();

    // Subscribe to all registered topics after connection
    // Note: subscribe() already handles mutex internally
    if (manager.mutex) {
        xSemaphoreTake(manager.mutex, portMAX_DELAY);
        auto handlers = manager.topicHandlers;  // Copy to avoid issues
        xSemaphoreGive(manager.mutex);

        for (const auto& handler : handlers) {
            (void)manager.subscribe(handler.first.c_str());
        }
    }
}

void handleMQTT(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    if (!event_data) return;

    auto* event = static_cast<esp_mqtt_event_handle_t>(event_data);
    auto& manager = MQTTManager::getInstance();
    if (manager.mqttClient) {
        manager.mqttClient->onEventCallback(event);

        // Handle disconnect event
        if (event_id == MQTT_EVENT_DISCONNECTED) {
            manager.onDisconnect();
        }
    }
}

// Helper validation methods
bool MQTTManager::isValidTopic(const char* topic) const {
    if (!topic || strlen(topic) == 0) {
        return false;
    }
    
    // Check topic length
    if (strlen(topic) >= MQTT_MAX_TOPIC_LENGTH) {
        MQTTM_LOG_E("Topic too long");
        return false;
    }
    
    // Check for wildcard characters in publish topics (not allowed)
    if (strchr(topic, '#') || strchr(topic, '+')) {
        // This check should only apply to publish operations
        // Subscribe operations can use wildcards
        return true;  // Let the caller decide based on context
    }
    
    return true;
}

bool MQTTManager::isValidPayload(const char* payload, size_t maxLen) const {
    if (!payload) {
        return true;  // Empty payload is valid
    }
    
    if (strlen(payload) >= maxLen) {
        MQTTM_LOG_E("Payload too long");
        return false;
    }
    
    return true;
}


bool MQTTManager::clearMessageQueue() {
    if (!mqttQueue || !mutex) return false;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    // Clear all messages from the queue
    MqttMessage msg;
    while (xQueueReceive(mqttQueue, &msg, 0) == pdTRUE) {
        // Message cleared
    }
    
    MQTTM_LOG_I("Message queue cleared");
    xSemaphoreGive(mutex);
    return true;
}

void MQTTManager::reconnectTaskFunc(void* param) {
    MQTTManager* manager = static_cast<MQTTManager*>(param);
    if (!manager) {
        vTaskDelete(NULL);
        return;
    }
    
    while (manager->autoReconnect) {
        if (!manager->isConnected()) {
            // Wait for the reconnect delay FIRST
            MQTTM_LOG_I("Waiting %d ms before reconnection attempt", manager->currentReconnectDelay);
            vTaskDelay(pdMS_TO_TICKS(manager->currentReconnectDelay));
            
            // Check again after delay in case we connected elsewhere
            if (manager->isConnected()) {
                MQTTM_LOG_I("Connected during wait, exiting reconnect task");
                break;
            }
            
            MQTTM_LOG_I("Attempting reconnection now");
            // Try to reconnect
            auto result = manager->connect();
            if (result.isOk()) {
                // Reset delay on successful connection
                xSemaphoreTake(manager->mutex, portMAX_DELAY);
                manager->currentReconnectDelay = manager->reconnectConfig.minInterval;
                xSemaphoreGive(manager->mutex);
            } else {
                // Exponential backoff with maximum
                xSemaphoreTake(manager->mutex, portMAX_DELAY);
                if (manager->reconnectConfig.exponentialBackoff) {
                    manager->currentReconnectDelay = std::min(manager->currentReconnectDelay * 2, manager->reconnectConfig.maxInterval);
                }
                xSemaphoreGive(manager->mutex);
            }
        } else {
            // Connected, no need for this task anymore
            break;
        }
    }
    
    // Clean up task handle
    xSemaphoreTake(manager->mutex, portMAX_DELAY);
    manager->reconnectTask = nullptr;
    xSemaphoreGive(manager->mutex);
    
    vTaskDelete(NULL);
}

// Event-driven methods
void MQTTManager::registerEventCallback(EventCallback callback) {
    if (!mutex) return;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    eventCallback = callback;
    xSemaphoreGive(mutex);
}

void MQTTManager::setEventGroup(EventGroupHandle_t eventGroup) {
    if (!mutex) return;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    externalEventGroup = eventGroup;
    xSemaphoreGive(mutex);
}

void MQTTManager::setEventBits(uint32_t connectedBit, uint32_t disconnectedBit, uint32_t messageBit) {
    if (!mutex) return;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    externalConnectedBit = connectedBit;
    externalDisconnectedBit = disconnectedBit;
    externalMessageBit = messageBit;
    xSemaphoreGive(mutex);
}

void MQTTManager::notifyEvent(MQTTEvent event, void* data) {
    // Call registered callback if any
    if (eventCallback) {
        eventCallback(event, data);
    }
    
    // Set external event bits if configured
    if (externalEventGroup) {
        switch (event) {
            case MQTTEvent::CONNECTED:
                if (externalConnectedBit) {
                    xEventGroupSetBits(externalEventGroup, externalConnectedBit);
                }
                break;
            case MQTTEvent::DISCONNECTED:
                if (externalDisconnectedBit) {
                    xEventGroupSetBits(externalEventGroup, externalDisconnectedBit);
                }
                break;
            case MQTTEvent::MESSAGE_RECEIVED:
                if (externalMessageBit) {
                    xEventGroupSetBits(externalEventGroup, externalMessageBit);
                }
                break;
            default:
                break;
        }
    }
    
    // Always set internal event bits
    if (mqttEventGroup) {
        switch (event) {
            case MQTTEvent::CONNECTED:
                xEventGroupSetBits(mqttEventGroup, MQTT_CONNECTED_BIT);
                xEventGroupClearBits(mqttEventGroup, MQTT_DISCONNECTED_BIT);
                break;
            case MQTTEvent::DISCONNECTED:
                xEventGroupSetBits(mqttEventGroup, MQTT_DISCONNECTED_BIT);
                xEventGroupClearBits(mqttEventGroup, MQTT_CONNECTED_BIT);
                break;
            case MQTTEvent::MESSAGE_RECEIVED:
                xEventGroupSetBits(mqttEventGroup, MQTT_MESSAGE_RECEIVED_BIT);
                break;
            default:
                break;
        }
    }
}

bool MQTTManager::processMessages(uint32_t maxMessages, TickType_t timeout) {
    if (!mqttQueue) {
        MQTTM_LOG_E("Message queue not initialized");
        return false;
    }
    
    uint32_t processed = 0;
    MqttMessage msg;
    
    while (processed < maxMessages) {
        if (xQueueReceive(mqttQueue, &msg, timeout) == pdTRUE) {
            // Process the message through topic handlers
            std::string topicStr(msg.topic);
            auto it = topicHandlers.find(topicStr);
            if (it != topicHandlers.end() && it->second != nullptr) {
                it->second->handleMessage(msg.topic, msg.payload);
            }
            processed++;
        } else {
            // No more messages available
            break;
        }
    }
    
    return processed > 0;
}

void MQTTManager::setAutoReconnect(bool enable) {
    // Use default config values
    ReconnectConfig defaultConfig;
    defaultConfig.minInterval = 1000;
    defaultConfig.maxInterval = 30000;
    defaultConfig.maxAttempts = 10;
    defaultConfig.exponentialBackoff = true;
    
    setAutoReconnect(enable, defaultConfig);
}

void MQTTManager::setAutoReconnect(bool enable, const ReconnectConfig& config) {
    if (!mutex) return;
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    autoReconnect = enable;
    reconnectConfig = config;
    currentReconnectDelay = config.minInterval;
    reconnectAttempts = 0;
    
    // Stop existing timer if any
    if (reconnectTimer) {
        xTimerStop(reconnectTimer, 0);
        xTimerDelete(reconnectTimer, 0);
        reconnectTimer = nullptr;
    }
    
    // Stop existing task if any
    if (reconnectTask) {
        vTaskDelete(reconnectTask);
        reconnectTask = nullptr;
    }
    
    // Create timer for reconnection if enabled
    if (enable) {
        reconnectTimer = xTimerCreate(
            "MQTTReconnect",
            pdMS_TO_TICKS(config.minInterval),
            pdFALSE,  // Not auto-reload
            this,
            reconnectTimerCallback
        );
    }
    
    xSemaphoreGive(mutex);
}

void MQTTManager::reconnectTimerCallback(TimerHandle_t xTimer) {
    MQTTManager* manager = static_cast<MQTTManager*>(pvTimerGetTimerID(xTimer));
    if (!manager) return;

    if (!manager->isConnected() && manager->autoReconnect) {
        if (manager->reconnectAttempts >= manager->reconnectConfig.maxAttempts) {
            MQTTM_LOG_E("Max reconnection attempts reached");
            ErrorEventData errorData = { MQTTError::CONNECTION_FAILED, "Max reconnection attempts reached" };
            manager->notifyEvent(MQTTEvent::ERROR, &errorData);
            return;
        }

        MQTTM_LOG_I("Reconnection attempt %d/%d",
            manager->reconnectAttempts + 1,
            manager->reconnectConfig.maxAttempts);

        auto result = manager->connect();
        if (result.isOk()) {
            // Reset on successful connection
            manager->currentReconnectDelay = manager->reconnectConfig.minInterval;
            manager->reconnectAttempts = 0;
        } else {
            manager->reconnectAttempts++;

            // Calculate next delay with exponential backoff
            if (manager->reconnectConfig.exponentialBackoff) {
                manager->currentReconnectDelay = std::min(
                    manager->currentReconnectDelay * 2,
                    manager->reconnectConfig.maxInterval
                );
            }

            // Schedule next attempt
            xTimerChangePeriod(manager->reconnectTimer,
                pdMS_TO_TICKS(manager->currentReconnectDelay), 0);
            xTimerStart(manager->reconnectTimer, 0);
        }
    }
}

bool MQTTManager::isSubscriptionConfirmed(const char* topic) const {
    if (!mqttClient || !topic) return false;
    return mqttClient->isSubscriptionConfirmed(std::string(topic));
}

int MQTTManager::getSubscriptionQos(const char* topic) const {
    if (!mqttClient || !topic) return -2;
    return mqttClient->getSubscriptionQos(std::string(topic));
}