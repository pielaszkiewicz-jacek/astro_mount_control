#ifndef EMAIL_CHANNEL_H
#define EMAIL_CHANNEL_H

#include "notifications/notification_engine.h"
#include <string>
#include <vector>

namespace astro_mount {
namespace notifications {

/**
 * @brief Email notification channel via SMTP
 *
 * Sends notification events as email messages using SMTP protocol.
 * Requires libcurl for SMTP communication. Supports STARTTLS.
 *
 * Configuration via EmailChannelConfig:
 * - smtp_host, smtp_port: SMTP server address
 * - use_tls: Enable STARTTLS encryption
 * - username/password: SMTP authentication
 * - from_address: Sender email
 * - to_addresses: Recipient list
 * - subject_prefix: Optional prefix (e.g. "[AstroMount]")
 */
class EmailChannel : public NotificationChannel {
public:
    struct Config {
        std::string smtp_host{"localhost"};
        int smtp_port{587};
        bool use_tls{true};
        std::string username;
        std::string password;
        std::string from_address{"astro-mount@localhost"};
        std::vector<std::string> to_addresses;
        std::string subject_prefix{"[AstroMount]"};
    };

    explicit EmailChannel(Config config);
    EmailChannel();
    ~EmailChannel() override;

    std::string name() const override { return "email"; }
    bool initialize() override;
    bool send(const NotificationEvent& event) override;
    void shutdown() override;
    bool isHealthy() const override;

private:
    // Build email content from event
    std::string buildSubject(const NotificationEvent& event) const;
    std::string buildBody(const NotificationEvent& event) const;

    Config config_;
    bool initialized_{false};
};

} // namespace notifications
} // namespace astro_mount

#endif // EMAIL_CHANNEL_H
