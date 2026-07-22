#ifndef MQTT_CHANNEL_H
#define MQTT_CHANNEL_H

#include "notifications/notification_engine.h"
#include <string>

namespace astro_mount {
namespace notifications {

/**
 * @brief MQTT notification channel
 *
 * Publishes notification events as MQTT messages to a broker.
 * Useful for integration with home automation systems (Home Assistant,
 * Node-RED, etc.) and IoT dashboards.
 *
 * Configuration via MqttChannelConfig:
 * - broker_url, broker_port: MQTT broker address
 * - client_id: MQTT client identifier
 * - topic_prefix: Topic prefix (e.g. "astro-mount/notifications")
 * - use_tls: Enable TLS for MQTT connection
 * - username/password: MQTT authentication
 * - qos: MQTT Quality of Service (0, 1, or 2)
 * - retain: Retain messages on broker
 */
class MqttChannel : public NotificationChannel {
public:
    struct Config {
        std::string broker_url{"localhost"};
        int broker_port{1883};
        std::string client_id{"astro-mount-notifications"};
        std::string topic_prefix{"astro-mount/notifications"};
        bool use_tls{false};
        std::string username;
        std::string password;
        int qos{1};
        bool retain{false};
    };

    explicit MqttChannel(Config config = {});
    ~MqttChannel() override;

    std::string name() const override { return "mqtt"; }
    bool initialize() override;
    bool send(const NotificationEvent& event) override;
    void shutdown() override;
    bool isHealthy() const override;

private:
    // Build MQTT topic from event
    std::string buildTopic(const NotificationEvent& event) const;

    // Build JSON payload from event
    std::string buildPayload(const NotificationEvent& event) const;

    Config config_;
    bool initialized_{false};
    void* mqtt_client_{nullptr};  // Opaque pointer to MQTT client (implementation-specific)
};

} // namespace notifications
} // namespace astro_mount

#endif // MQTT_CHANNEL_H
