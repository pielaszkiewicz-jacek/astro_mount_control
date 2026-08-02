#include "notifications/channels/email_channel.h"
#include <sstream>
#include <chrono>
#include <iomanip>

namespace astro_mount {
namespace notifications {

EmailChannel::EmailChannel(Config config)
    : config_(std::move(config)) {
}

EmailChannel::EmailChannel() : EmailChannel(Config{}) {}

EmailChannel::~EmailChannel() {
    shutdown();
}

bool EmailChannel::initialize() {
    // Validate configuration
    if (config_.to_addresses.empty()) {
        return false;
    }
    if (config_.from_address.empty()) {
        return false;
    }

    initialized_ = true;
    return true;
}

bool EmailChannel::send(const NotificationEvent& event) {
    if (!initialized_) return false;

    std::string subject = buildSubject(event);
    std::string body = buildBody(event);

    // TODO: Implement actual SMTP sending via libcurl
    // For now, log the email that would be sent
    // curl_easy_setopt(curl, CURLOPT_URL, "smtp://" + config_.smtp_host + ":" + std::to_string(config_.smtp_port));
    // curl_easy_setopt(curl, CURLOPT_MAIL_FROM, config_.from_address.c_str());
    // curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

    return true;  // Stub — actual SMTP implementation requires libcurl integration
}

void EmailChannel::shutdown() {
    initialized_ = false;
}

bool EmailChannel::isHealthy() const {
    return initialized_;
}

std::string EmailChannel::buildSubject(const NotificationEvent& event) const {
    std::ostringstream ss;

    // Severity prefix
    switch (event.severity) {
        case Severity::CRITICAL: ss << "[CRITICAL] "; break;
        case Severity::ERROR:    ss << "[ERROR] ";    break;
        case Severity::WARNING:  ss << "[WARNING] ";  break;
        default: break;
    }

    // Prefix + title
    if (!config_.subject_prefix.empty()) {
        ss << config_.subject_prefix << " ";
    }
    ss << event.title;

    return ss.str();
}

std::string EmailChannel::buildBody(const NotificationEvent& event) const {
    std::ostringstream ss;

    ss << "=== Notification Event ===" << "\n\n";
    ss << "ID:       " << event.id << "\n";
    ss << "Time:     " << std::chrono::system_clock::to_time_t(event.timestamp) << "\n";
    ss << "Severity: " << static_cast<int>(event.severity) << "\n";
    ss << "Category: " << static_cast<int>(event.category) << "\n";
    ss << "Source:   " << event.source << "\n";
    ss << "Title:    " << event.title << "\n";
    ss << "Message:  " << event.message << "\n";

    if (!event.metadata.empty()) {
        ss << "\n--- Metadata ---\n";
        for (const auto& [key, value] : event.metadata) {
            ss << key << ": " << value << "\n";
        }
    }

    return ss.str();
}

} // namespace notifications
} // namespace astro_mount
