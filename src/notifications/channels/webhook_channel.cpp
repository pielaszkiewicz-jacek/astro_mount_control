#include "notifications/channels/webhook_channel.h"
#include "http_client.h"
#include <sstream>
#include <chrono>
#include <thread>

namespace astro_mount {
namespace notifications {

WebhookChannel::WebhookChannel(Config config)
    : config_(std::move(config)) {
}

WebhookChannel::WebhookChannel() : WebhookChannel(Config{}) {}

WebhookChannel::~WebhookChannel() {
    shutdown();
}

bool WebhookChannel::initialize() {
    if (config_.url.empty()) {
        return false;
    }
    initialized_ = true;
    return true;
}

bool WebhookChannel::send(const NotificationEvent& event) {
    if (!initialized_ || config_.url.empty()) return false;

    std::string payload = buildPayload(event);

    // Add auth header if configured
    std::map<std::string, std::string> headers = config_.headers;
    if (!config_.auth_token.empty()) {
        headers["Authorization"] = "Bearer " + config_.auth_token;
    }
    headers["Content-Type"] = "application/json";

    return httpPost(config_.url, payload, headers,
                    config_.method, config_.retry_count, config_.timeout_seconds);
}

void WebhookChannel::shutdown() {
    initialized_ = false;
}

bool WebhookChannel::isHealthy() const {
    return initialized_;
}

std::string WebhookChannel::buildPayload(const NotificationEvent& event) const {
    std::ostringstream ss;

    ss << "{\n";
    ss << "  \"id\": \"" << event.id << "\",\n";
    ss << "  \"timestamp\": " << std::chrono::system_clock::to_time_t(event.timestamp) << ",\n";
    ss << "  \"severity\": " << static_cast<int>(event.severity) << ",\n";
    ss << "  \"severity_name\": \"";
    switch (event.severity) {
        case Severity::DEBUG:    ss << "DEBUG";    break;
        case Severity::INFO:     ss << "INFO";     break;
        case Severity::WARNING:  ss << "WARNING";  break;
        case Severity::ERROR:    ss << "ERROR";    break;
        case Severity::CRITICAL: ss << "CRITICAL"; break;
    }
    ss << "\",\n";
    ss << "  \"category\": " << static_cast<int>(event.category) << ",\n";
    ss << "  \"source\": \"" << event.source << "\",\n";
    ss << "  \"title\": \"" << event.title << "\",\n";
    ss << "  \"message\": \"" << event.message << "\"\n";
    ss << "}";

    return ss.str();
}

bool WebhookChannel::httpPost(const std::string& url, const std::string& payload,
                              const std::map<std::string, std::string>& headers,
                              const std::string& method, int retries, int timeout_seconds) {
    // P9: real HTTP POST/PUT via libcurl, with retry/backoff.
    for (int attempt = 0; attempt <= retries; ++attempt) {
        if (attempt > 0) {
            std::this_thread::sleep_for(std::chrono::seconds(1 * attempt));
        }
        if (astro_mount::http::post(url, payload, headers, timeout_seconds,
                                    "astro-mount-controller/1.0", method)) {
            return true;
        }
    }
    return false;
}

} // namespace notifications
} // namespace astro_mount
