#ifndef NOTIFICATION_ENGINE_H
#define NOTIFICATION_ENGINE_H

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <mutex>
#include <map>
#include <queue>
#include <deque>
#include <optional>
#include <atomic>
#include <thread>
#include <condition_variable>
#include "proto/notification.pb.h"

namespace astro_mount {
namespace notifications {

/**
 * @brief Severity level for notification events (mirrors protobuf enum)
 */
enum class Severity {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
    CRITICAL = 4
};

/**
 * @brief Category of notification event (mirrors protobuf enum)
 */
enum class Category {
    UNSPECIFIED = 0,
    MOUNT = 1,
    WEATHER = 2,
    SEQUENCER = 3,
    POWER = 4,
    SESSION = 5,
    SYSTEM = 6,
    GUIDER = 7,
    FOCUSER = 8,
    CAMERA = 9,
    DOME = 10
};

/**
 * @brief A single notification event (internal representation)
 */
struct NotificationEvent {
    std::string id;
    std::chrono::system_clock::time_point timestamp;
    Severity severity;
    Category category;
    std::string source;
    std::string title;
    std::string message;
    std::map<std::string, std::string> metadata;
    bool acknowledged{false};
};

/**
 * @brief Abstract base class for notification channels
 *
 * Each channel type (email, webhook, MQTT, log) implements this interface.
 * Channels are registered with the NotificationEngine and receive events
 * that pass the severity and category filters.
 */
class NotificationChannel {
public:
    virtual ~NotificationChannel() = default;

    /// @brief Channel name (e.g. "email", "webhook", "mqtt")
    virtual std::string name() const = 0;

    /// @brief Initialize the channel (open connections, validate config)
    virtual bool initialize() = 0;

    /// @brief Send a notification event through this channel
    /// @return true if delivery was successful
    virtual bool send(const NotificationEvent& event) = 0;

    /// @brief Shutdown the channel (close connections)
    virtual void shutdown() = 0;

    /// @brief Check if channel is healthy/operational
    virtual bool isHealthy() const = 0;
};

/**
 * @brief Core notification engine
 *
 * Manages notification channels, event queuing, filtering,
 * aggregation, and delivery. Thread-safe — events can be pushed
 * from any thread and are processed on a background worker thread.
 *
 * Architecture:
 *   Sources → pushEvent() → [Queue + Filter] → [Worker Thread] → Channels
 */
class NotificationEngine {
public:
    NotificationEngine();
    ~NotificationEngine();

    // Non-copyable
    NotificationEngine(const NotificationEngine&) = delete;
    NotificationEngine& operator=(const NotificationEngine&) = delete;

    /**
     * @brief Register a notification channel
     * @param channel Unique pointer to the channel implementation
     * @return true if channel was registered successfully
     */
    bool registerChannel(std::unique_ptr<NotificationChannel> channel);

    /**
     * @brief Remove a registered channel by name
     */
    void unregisterChannel(const std::string& name);

    /**
     * @brief Start the notification engine (begins processing queued events)
     * @return true if started successfully
     */
    bool start();

    /**
     * @brief Stop the notification engine (flushes queue, shuts down channels)
     */
    void stop();

    /**
     * @brief Push an event into the notification queue
     *
     * Thread-safe — can be called from any thread.
     * The event is filtered and queued; delivery happens on the worker thread.
     */
    void pushEvent(NotificationEvent event);

    /**
     * @brief Configure the engine from protobuf config
     */
    void configure(const astro_mount::NotificationConfig& config);

    /**
     * @brief Get current engine status
     */
    astro_mount::NotificationStatus getStatus() const;

    /**
     * @brief Set minimum severity level for notifications
     * @param severity Minimum severity (events below this are dropped)
     */
    void setMinSeverity(Severity severity);

    /**
     * @brief Enable or disable a specific event category
     */
    void setCategoryEnabled(Category category, bool enabled);

    /**
     * @brief Send a test notification through all active channels
     * @param message Optional custom message
     */
    void sendTestNotification(const std::string& message = "Test notification from Astro Mount Controller");

    /**
     * @brief Subscribe to the event stream (receive all processed events)
     * @param callback Function called for each processed event
     * @return Subscription ID (use to unsubscribe)
     */
    int subscribe(std::function<void(const NotificationEvent&)> callback);

    /**
     * @brief Unsubscribe from the event stream
     * @param subscription_id ID returned from subscribe()
     */
    void unsubscribe(int subscription_id);

private:
    // Background worker thread
    void workerLoop();

    // Filter event against current configuration
    bool shouldDeliver(const NotificationEvent& event) const;

    // Deliver an event to all channels + subscribers + stats (N4).
    void deliver(const NotificationEvent& event);

    // Aggregate similar events (combine repeated events within interval).
    // N4: windowed — matching events are merged into pending_aggregate_ and
    // flushed only when the aggregation window expires or a non-matching event
    // arrives, so the client receives one digest per window instead of a flood.
    NotificationEvent aggregate(const NotificationEvent& event);

    // True when `event` belongs to the same aggregation bucket as `base`.
    bool matchesAggregate(const NotificationEvent& base, const NotificationEvent& event) const;

    // Remove any dynamically-managed channels (email/webhook/mqtt) before
    // re-applying a NotificationConfig (N2). The always-on "log" channel and
    // any channels registered outside configure() are left untouched.
    void clearManagedChannels();

    // Convert internal event to protobuf
    astro_mount::NotificationEvent toProto(const NotificationEvent& event) const;

    // Convert protobuf to internal event
    NotificationEvent fromProto(const astro_mount::NotificationEvent& proto) const;

    // Configuration
    struct Config {
        Severity min_severity{Severity::INFO};
        std::map<Category, bool> enabled_categories;
        bool aggregate_messages{false};
        int aggregation_interval_minutes{5};
    };

    Config config_;
    std::vector<std::unique_ptr<NotificationChannel>> channels_;
    std::queue<NotificationEvent> event_queue_;

    // Aggregation state (N4) — guarded by mutex_.
    std::optional<NotificationEvent> pending_aggregate_;
    std::chrono::system_clock::time_point pending_aggregate_time_{};
    int pending_aggregate_count_{0};

    // Rolling 1-hour send timestamps (N4) — guarded by mutex_.
    std::deque<std::chrono::system_clock::time_point> sent_timestamps_;

    // Subscribers
    std::map<int, std::function<void(const NotificationEvent&)>> subscribers_;
    int next_subscriber_id_{0};

    // Threading
    std::unique_ptr<std::thread> worker_thread_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    std::condition_variable cv_;

    // Statistics
    std::atomic<int> events_sent_total_{0};
    std::atomic<int> events_failed_{0};
    std::chrono::system_clock::time_point last_event_time_;
    std::string last_error_;
};

} // namespace notifications
} // namespace astro_mount

#endif // NOTIFICATION_ENGINE_H
