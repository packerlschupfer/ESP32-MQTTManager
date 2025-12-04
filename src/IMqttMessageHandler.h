#ifndef IMQTTMESSAGEHANDLER_H
#define IMQTTMESSAGEHANDLER_H

#include <string>

class IMqttMessageHandler {
public:
    virtual ~IMqttMessageHandler() = default;
    virtual void handleMessage(const std::string& topic, const std::string& payload) = 0;
};

#endif // IMQTTMESSAGEHANDLER_H
