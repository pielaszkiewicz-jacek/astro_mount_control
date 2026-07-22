#include "notifications/channels/mqtt_channel.h"
#include <sstream>
#include <chrono>
#include <algorithm>

namespace astro_mount {
namespace notifications {

MqttChannel::MqttChannel(Config config)
    : config_(std::move(config)) {
}

MqttChannel::~MqttChannel() {
    shutdown();
}

bool MqttChannel::initialize() {
    if (config_.broker_url.empty()) {
        return false;
    }

    // TODO: Implement actual MQTT client connection
    // MQTTClient_create(&mqtt_client_, broker_address, client_id_.c_str(),
    //                   MQTTCLIENT_PERSISTENCE_NONE, nullptr);
    // MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    // MQTTClient_connect(mqtt_client_, &conn_opts);

    initialized_ = true;
    return true;
}

bool MqttChannel::send(const NotificationEvent& event) {
    if (!initialized_) return false;

    std::string topic = buildTopic(event);
    std::string payload = buildPayload(event);

    // TODO: Implement actual MQTT publish
    // MQTTClient_message pubmsg = MQTTClient_message_initializer;
    // pubmsg.payload = (void*)payload.c_str();
    // pubmsg.payloadlen = payload.length();
    // pubmsg.qos = config_.qos;
    // pubmsg.retained = config_.retain;
    // MQTTClient_publishMessage(mqtt_client_, topic.c_str(), &pubmsg, &token);

    return true;  // Stub — actual MQTT implementation requires Paho MQTT C client library
}

void MqttChannel::shutdown() {
    if (mqtt_client_) {
        // TODO: MQTTClient_disconnect(mqtt_client_, 10000);
        // MQTTClient_destroy(&mqtt_client_);
        mqtt_client_ = nullptr;
    }
    initialized_ = false;
}

bool MqttChannel::isHealthy() const {
    return initialized_ && mqtt_client_ != nullptr;
}

std::string MqttChannel::buildTopic(const NotificationEvent& event) const {
    std::ostringstream ss;
    ss << config_.topic_prefix;

    // Add severity sub-topic
    switch (event.severity) {
        case Severity::DEBUG:    ss << "/debug";    break;
        case Severity::INFO:     ss << "/info";     break;
        case Severity::WARNING:  ss << "/warning";  break;
        case Severity::ERROR:    ss << "/error";    break;
        case Severity::CRITICAL: ss << "/critical"; break;
    }

    // Add category sub-topic
    ss << "/" << event.source;
    std::string topic = ss.str();
    std::transform(topic.begin(), topic.end(), topic.begin(), ::tolower);

    return topic;
}

std::string MqttChannel::buildPayload(const NotificationEvent& event) const {
    std::ostringstream ss;
    ss << "{"
        << "\"id\":\"" << event.id << "\","
        << "\"title\":\"" << event.title << "\","
        << "\"message\":\"" << event.message << "\","
        << "\"severity\":" << static_cast<int>(event.severity)
        << "}";
    return ss.str();
}

} // namespace notifications
} // namespace astro_mount
