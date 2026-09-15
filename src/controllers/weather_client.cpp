#include "controllers/weather_client.h"
#include <algorithm>
#include <chrono>
#include <thread>
#include <iostream>

namespace astro_mount {
namespace controllers {

WeatherClient::WeatherClient(const std::string& server_address,
                             std::shared_ptr<notifications::NotificationEngine> notification_engine)
    : server_address_(server_address),
      notification_engine_(std::move(notification_engine)) {
}

WeatherClient::~WeatherClient() {
    stop();
}

bool WeatherClient::start(int interval_ms,
                          std::function<void(const std::string&)> on_danger,
                          std::function<void(const astro_mount::WeatherStatus&)> on_status) {
    if (running_) return true;

    try {
        auto channel = grpc::CreateChannel(server_address_,
                                           grpc::InsecureChannelCredentials());
        stub_ = WeatherService::NewStub(channel);
    } catch (const std::exception& e) {
        std::cerr << "[WeatherClient] Failed to create gRPC channel: " << e.what() << "\n";
        return false;
    }

    poll_interval_ms_ = interval_ms;
    on_danger_callback_ = std::move(on_danger);
    on_status_callback_ = std::move(on_status);
    running_ = true;
    connected_ = false;
    safe_to_observe_ = true;
    alert_level_ = 0;

    subscribe_thread_ = std::make_unique<std::thread>(&WeatherClient::subscribeLoop, this);
    return true;
}

void WeatherClient::stop() {
    if (!running_) return;
    running_ = false;

    // Unblock the reader thread if it is currently waiting inside
    // ClientReader::Read() for the next pushed update.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_stream_context_) {
            active_stream_context_->TryCancel();
        }
    }

    if (subscribe_thread_ && subscribe_thread_->joinable()) {
        subscribe_thread_->join();
    }
    subscribe_thread_.reset();
    stub_.reset();
    connected_ = false;
}

bool WeatherClient::getWeatherStatus(astro_mount::WeatherStatus* status) {
    if (!stub_) return false;

    grpc::ClientContext context;
    google::protobuf::Empty request;

    // Set a reasonable deadline (5 seconds)
    std::chrono::system_clock::time_point deadline =
        std::chrono::system_clock::now() + std::chrono::seconds(5);
    context.set_deadline(deadline);

    auto rpc_status = stub_->GetWeatherStatus(&context, request, status);
    if (rpc_status.ok()) {
        connected_ = true;
        return true;
    }

    connected_ = false;
    return false;
}

void WeatherClient::sendNotification(notifications::Severity severity,
                                     const std::string& title,
                                     const std::string& message) {
    if (!notification_engine_) return;

    notifications::NotificationEvent event;
    event.id = "weather_" + std::to_string(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    event.timestamp = std::chrono::system_clock::now();
    event.severity = severity;
    event.category = notifications::Category::WEATHER;
    event.source = "weather_client";
    event.title = title;
    event.message = message;

    notification_engine_->pushEvent(std::move(event));
}

void WeatherClient::handleStatus(const astro_mount::WeatherStatus& status) {
    safe_to_observe_ = status.safe_to_observe();
    alert_level_ = status.alert_level();

    // Forward live environmental conditions to the mount controller
    // (temperature/pressure/humidity) so refraction and status reporting
    // use real weather data instead of static defaults.
    if (on_status_callback_) {
        on_status_callback_(status);
    }

    if (!status.safe_to_observe()) {
        std::string msg = status.safety_message();
        if (msg.empty()) {
            msg = "Unsafe weather conditions detected";
        }

        // Trigger auto-park callback
        if (on_danger_callback_) {
            on_danger_callback_(msg);
        }

        // Send notification via notification engine
        switch (status.alert_level()) {
            case WeatherAlertSeverity::WEATHER_CAUTION:
                sendNotification(notifications::Severity::WARNING,
                    "Weather Caution", msg);
                break;
            case WeatherAlertSeverity::WEATHER_WARNING:
                sendNotification(notifications::Severity::ERROR,
                    "Weather Warning", msg);
                break;
            case WeatherAlertSeverity::WEATHER_DANGER:
                sendNotification(notifications::Severity::CRITICAL,
                    "Weather DANGER — Auto-park triggered", msg);
                break;
            default:
                break;
        }
    } else if (alert_level_ != 0) {
        // Weather returned to safe — send all-clear notification
        sendNotification(notifications::Severity::INFO,
            "Weather Conditions Improved",
            "Weather has returned to safe levels. Observatory operations may resume.");
    }
}

void WeatherClient::subscribeLoop() {
    while (running_) {
        if (!stub_) {
            // Stub creation failed; back off before trying again.
            for (int i = 0; i < 1000 && running_; i += 100) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }

        // Each subscription has a bounded lifetime so a dead weather service
        // cannot leave this reader blocked forever; the loop resubscribes.
        auto context = std::make_unique<grpc::ClientContext>();
        context->set_deadline(std::chrono::system_clock::now() +
                              std::chrono::seconds(30));

        google::protobuf::Empty request;
        std::unique_ptr<grpc::ClientReader<astro_mount::WeatherStatus>> reader(
            stub_->SubscribeWeatherStatus(context.get(), request));

        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_stream_context_ = context.get();
        }

        astro_mount::WeatherStatus status;
        while (running_ && reader->Read(&status)) {
            connected_ = true;
            handleStatus(status);
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_stream_context_ = nullptr;
        }
        connected_ = false;

        if (!running_) break;

        // The stream ended while an alert was active — notify the operator.
        if (alert_level_ != 0) {
            sendNotification(notifications::Severity::WARNING,
                "Weather Stream Interrupted",
                "Lost weather status stream. Reconnecting; manual supervision required.");
            alert_level_ = 0;
        }

        // Back off before resubscribing so a restarted service is not hammered.
        const int backoff_ms = std::max(1000, poll_interval_ms_);
        for (int i = 0; i < backoff_ms && running_; i += 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

} // namespace controllers
} // namespace astro_mount
