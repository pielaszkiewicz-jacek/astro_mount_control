#ifndef WEATHER_CLIENT_H
#define WEATHER_CLIENT_H

#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <thread>
#include <functional>
#include <grpcpp/grpcpp.h>
#include "proto/weather.grpc.pb.h"
#include "notifications/notification_engine.h"

namespace astro_mount {
namespace controllers {

/**
 * @brief gRPC client for the external weather monitoring service
 *
 * Connects to the standalone WeatherServer process and periodically
 * polls weather status. When configured, can trigger mount safety
 * actions (auto-park) upon detecting dangerous weather conditions.
 *
 * Thread-safe. All integration is disabled by default (see
 * config::Configuration::ExternalIntegrationConfig::weather_enabled).
 */
class WeatherClient {
public:
    /**
     * @brief Construct a new WeatherClient
     * @param server_address Address of the weather gRPC service (e.g. "127.0.0.1:50055")
     * @param notification_engine Optional notification engine for weather alerts
     */
    explicit WeatherClient(const std::string& server_address,
                           std::shared_ptr<notifications::NotificationEngine> notification_engine = nullptr);
    ~WeatherClient();

    // Non-copyable
    WeatherClient(const WeatherClient&) = delete;
    WeatherClient& operator=(const WeatherClient&) = delete;

    /**
     * @brief Start polling the weather service
     * @param interval_ms Polling interval in milliseconds
     * @param on_danger Callback invoked when dangerous weather is detected
     *                  (e.g. to trigger auto-park). Called with the alert message.
     * @param on_status Optional callback invoked after every successful poll
     *                  with the full WeatherStatus. Used to forward live
     *                  environmental conditions (temperature, pressure,
     *                  humidity) to the mount controller.
     * @return True if connection established
     */
    bool start(int interval_ms = 10000,
               std::function<void(const std::string&)> on_danger = nullptr,
               std::function<void(const astro_mount::WeatherStatus&)> on_status = nullptr);

    /// Stop polling
    void stop();

    /**
     * @brief Get current weather status (synchronous RPC)
     * @param status Output parameter for weather status
     * @return True if status obtained successfully
     */
    bool getWeatherStatus(astro_mount::WeatherStatus* status);

    /**
     * @brief Check if the weather service is currently connected
     */
    bool isConnected() const { return connected_; }

    /**
     * @brief Check if weather conditions are safe for observing
     */
    bool isSafeToObserve() const { return safe_to_observe_; }

    /**
     * @brief Get the last alert severity level
     */
    int getAlertLevel() const { return alert_level_; }

    /// Send notification event via notification engine
    void sendNotification(notifications::Severity severity,
                          const std::string& title,
                          const std::string& message);

    /// Polling loop
    void pollLoop();

    std::string server_address_;
    std::unique_ptr<WeatherService::Stub> stub_;
    std::shared_ptr<notifications::NotificationEngine> notification_engine_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> safe_to_observe_{true};
    std::atomic<int> alert_level_{0};
    int poll_interval_ms_{10000};
    std::unique_ptr<std::thread> poll_thread_;
    std::function<void(const std::string&)> on_danger_callback_;
    std::function<void(const astro_mount::WeatherStatus&)> on_status_callback_;
    mutable std::mutex mutex_;
};

} // namespace controllers
} // namespace astro_mount

#endif // WEATHER_CLIENT_H
