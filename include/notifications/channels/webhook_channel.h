#ifndef WEBHOOK_CHANNEL_H
#define WEBHOOK_CHANNEL_H

#include "notifications/notification_engine.h"
#include <string>
#include <map>

namespace astro_mount {
namespace notifications {

/**
 * @brief Webhook notification channel via HTTP POST/PUT
 *
 * Sends notification events as JSON HTTP requests to a configured endpoint.
 * Useful for integrating with Slack, Discord, Telegram, custom dashboards, etc.
 * Requires libcurl for HTTP communication.
 *
 * Configuration via WebhookChannelConfig:
 * - url: Target URL
 * - method: HTTP method ("POST" or "PUT")
 * - headers: Custom HTTP headers
 * - auth_token: Bearer token for Authorization header
 * - timeout_seconds: Request timeout
 * - retry_count: Number of retries on failure
 */
class WebhookChannel : public NotificationChannel {
public:
    struct Config {
        std::string url;
        std::string method{"POST"};
        std::map<std::string, std::string> headers;
        std::string auth_token;
        int timeout_seconds{10};
        int retry_count{3};
    };

    explicit WebhookChannel(Config config);
    WebhookChannel();
    ~WebhookChannel() override;

    std::string name() const override { return "webhook"; }
    bool initialize() override;
    bool send(const NotificationEvent& event) override;
    void shutdown() override;
    bool isHealthy() const override;

private:
    // Build JSON payload from event
    std::string buildPayload(const NotificationEvent& event) const;

    // Perform HTTP request with retry logic
    bool httpPost(const std::string& url, const std::string& payload,
                  const std::map<std::string, std::string>& headers,
                  const std::string& method, int retries, int timeout_seconds);

    Config config_;
    bool initialized_{false};
};

} // namespace notifications
} // namespace astro_mount

#endif // WEBHOOK_CHANNEL_H
