#include "notifications/channels/webhook_channel.h"
#include <sstream>
#include <chrono>
#include <thread>

namespace astro_mount {
namespace notifications {

WebhookChannel::WebhookChannel(Config config)
    : config_(std::move(config)) {
}

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

    return httpPost(config_.url, payload, config_.retry_count);
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

bool WebhookChannel::httpPost(const std::string& url, const std::string& payload, int retries) {
    // TODO: Implement actual HTTP POST via libcurl
    // For now, log the webhook that would be sent
    //
    // curl_slist* headers = nullptr;
    // for (const auto& [key, value] : headers_) {
    //     headers = curl_slist_append(headers, (key + ": " + value).c_str());
    // }
    // curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    // curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    // curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    // curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds_);
    //
    // CURLcode res = curl_easy_perform(curl);

    for (int attempt = 0; attempt <= retries; ++attempt) {
        // Simulate attempt
        if (attempt > 0) {
            std::this_thread::sleep_for(std::chrono::seconds(1 * attempt));
        }
        // In real implementation: perform curl_easy_perform and check result
        return true;  // Stub
    }

    return false;
}

} // namespace notifications
} // namespace astro_mount
