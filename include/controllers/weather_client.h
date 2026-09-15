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
 * Connects to the standalone WeatherServer process and consumes a
 * server-pushed stream of WeatherStatus updates. The weather service
 * drives the flow, so the mount controller no longer polls it. When
 * configured, the client can trigger mount safety actions (auto-park)
 * upon detecting dangerous weather conditions.
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
     * @brief Start consuming the weather service's pushed status stream
     * @param interval_ms Resubscribe backoff interval in milliseconds
     * @param on_danger Callback invoked when dangerous weather is detected
     *                  (e.g. to trigger auto-park). Called with the alert message.
     * @param on_status Optional callback invoked for every pushed WeatherStatus.
     *                  Used to forward live environmental conditions
     *                  (temperature, pressure, humidity) to the mount controller.
     * @return True if the subscription was started
     */
    bool start(int interval_ms = 10000,
               std::function<void(const std::string&)> on_danger = nullptr,
               std::function<void(const astro_mount::WeatherStatus&)> on_status = nullptr);

    /// Stop the weather subscription and join its thread
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

    /// Handle one pushed WeatherStatus update
    void handleStatus(const astro_mount::WeatherStatus& status);

    /// Subscription loop — consumes the server-pushed WeatherStatus stream
    /// and resubscribes automatically when the stream ends.
    void subscribeLoop();

    std::string server_address_;
    std::unique_ptr<WeatherService::Stub> stub_;
    std::shared_ptr<notifications::NotificationEngine> notification_engine_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> safe_to_observe_{true};
    std::atomic<int> alert_level_{0};
    int poll_interval_ms_{10000};
    std::unique_ptr<std::thread> subscribe_thread_;
    std::function<void(const std::string&)> on_danger_callback_;
    std::function<void(const astro_mount::WeatherStatus&)> on_status_callback_;

    /// Active stream context for the current subscription. Guarded by mutex_;
    /// ClientContext::TryCancel() is thread-safe and lets stop() unblock the
    /// reader thread blocked in subscribeLoop().
    grpc::ClientContext* active_stream_context_{nullptr};

    mutable std::mutex mutex_;
};

} // namespace controllers
} // namespace astro_mount

#endif // WEATHER_CLIENT_H
