#include "notifications/notification_engine.h"
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <uuid/uuid.h>

namespace astro_mount {
namespace notifications {

// ─── NotificationEngine Implementation ───────────────────────────────────────

NotificationEngine::NotificationEngine()
    : running_(false)
    , events_sent_total_(0)
    , events_failed_(0) {
    // Initialize with all categories enabled by default
    for (int i = 1; i <= 10; ++i) {
        config_.enabled_categories[static_cast<Category>(i)] = true;
    }
}

NotificationEngine::~NotificationEngine() {
    stop();
}

bool NotificationEngine::registerChannel(std::unique_ptr<NotificationChannel> channel) {
    if (!channel) return false;

    std::lock_guard<std::mutex> lock(mutex_);

    // Check for duplicate names
    for (const auto& c : channels_) {
        if (c->name() == channel->name()) {
            return false;
        }
    }

    if (!channel->initialize()) {
        return false;
    }

    channels_.push_back(std::move(channel));
    return true;
}

void NotificationEngine::unregisterChannel(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = std::remove_if(channels_.begin(), channels_.end(),
        [&name](const auto& channel) {
            if (channel->name() == name) {
                channel->shutdown();
                return true;
            }
            return false;
        });
    channels_.erase(it, channels_.end());
}

bool NotificationEngine::start() {
    if (running_) return true;

    running_ = true;
    worker_thread_ = std::make_unique<std::thread>(&NotificationEngine::workerLoop, this);
    return true;
}

void NotificationEngine::stop() {
    if (!running_) return;

    running_ = false;
    cv_.notify_one();

    if (worker_thread_ && worker_thread_->joinable()) {
        worker_thread_->join();
    }
    worker_thread_.reset();

    // Shutdown all channels
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& channel : channels_) {
        channel->shutdown();
    }
}

void NotificationEngine::pushEvent(NotificationEvent event) {
    if (!shouldDeliver(event)) return;

    std::lock_guard<std::mutex> lock(mutex_);
    event_queue_.push(std::move(event));
    cv_.notify_one();
}

void NotificationEngine::configure(const astro_mount::NotificationConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Update minimum severity
    config_.min_severity = static_cast<Severity>(config.min_severity());

    // Update category filters
    for (const auto& entry : config.enabled_events()) {
        auto category = static_cast<Category>(std::stoi(entry.first));
        config_.enabled_categories[category] = entry.second;
    }

    // Update aggregation settings
    config_.aggregate_messages = config.aggregate_messages();
    config_.aggregation_interval_minutes = config.aggregation_interval_minutes();

    // Configure channels
    for (const auto& channel_config : config.channels()) {
        // Channel configuration would be applied to matching registered channels
        // Implementation depends on channel type mapping
    }
}

astro_mount::NotificationStatus NotificationEngine::getStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);

    astro_mount::NotificationStatus status;
    status.set_configured(!channels_.empty());
    status.set_active_channels(static_cast<int32_t>(channels_.size()));
    status.set_events_sent_total(events_sent_total_);
    status.set_events_sent_last_hour(0);  // Would need rolling counter
    status.set_events_queued(static_cast<int32_t>(event_queue_.size()));
    status.set_events_failed(events_failed_);
    status.set_last_error(last_error_);

    if (last_event_time_.time_since_epoch().count() > 0) {
        auto proto_time = status.mutable_last_event_time();
        auto duration = last_event_time_.time_since_epoch();
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
        auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count() % 1000000000;
        proto_time->set_seconds(secs);
        proto_time->set_nanos(static_cast<int32_t>(nanos));
    }

    // Add channel status
    for (const auto& channel : channels_) {
        auto* cs = status.add_channel_status();
        cs->set_enabled(true);
        // Set channel type based on channel name
        if (channel->name() == "email") cs->set_type(astro_mount::CHANNEL_EMAIL);
        else if (channel->name() == "webhook") cs->set_type(astro_mount::CHANNEL_WEBHOOK);
        else if (channel->name() == "mqtt") cs->set_type(astro_mount::CHANNEL_MQTT);
        else cs->set_type(astro_mount::CHANNEL_LOG);
    }

    return status;
}

void NotificationEngine::setMinSeverity(Severity severity) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.min_severity = severity;
}

void NotificationEngine::setCategoryEnabled(Category category, bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.enabled_categories[category] = enabled;
}

void NotificationEngine::sendTestNotification(const std::string& message) {
    NotificationEvent event;
    event.id = "test-" + std::to_string(
        std::chrono::system_clock::now().time_since_epoch().count());
    event.timestamp = std::chrono::system_clock::now();
    event.severity = Severity::INFO;
    event.category = Category::SYSTEM;
    event.source = "NotificationEngine";
    event.title = "Test Notification";
    event.message = message;

    pushEvent(std::move(event));
}

int NotificationEngine::subscribe(std::function<void(const NotificationEvent&)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    int id = next_subscriber_id_++;
    subscribers_[id] = std::move(callback);
    return id;
}

void NotificationEngine::unsubscribe(int subscription_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_.erase(subscription_id);
}

// ─── Private Methods ─────────────────────────────────────────────────────────

void NotificationEngine::workerLoop() {
    while (running_) {
        NotificationEvent event;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(500), [this] {
                return !event_queue_.empty() || !running_;
            });

            if (!running_) break;
            if (event_queue_.empty()) continue;

            event = std::move(event_queue_.front());
            event_queue_.pop();
        }

        // Apply aggregation if enabled
        if (config_.aggregate_messages) {
            event = aggregate(event);
        }

        // Deliver to all active channels
        bool any_success = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& channel : channels_) {
                if (channel->send(event)) {
                    any_success = true;
                } else {
                    events_failed_++;
                }
            }
        }

        if (any_success) {
            events_sent_total_++;
            last_event_time_ = std::chrono::system_clock::now();
        }

        // Notify subscribers
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, callback] : subscribers_) {
            try {
                callback(event);
            } catch (...) {
                // Ignore subscriber errors
            }
        }
    }
}

bool NotificationEngine::shouldDeliver(const NotificationEvent& event) const {
    // Check severity
    if (static_cast<int>(event.severity) < static_cast<int>(config_.min_severity)) {
        return false;
    }

    // Check if category is enabled
    auto it = config_.enabled_categories.find(event.category);
    if (it != config_.enabled_categories.end() && !it->second) {
        return false;
    }

    return true;
}

NotificationEvent NotificationEngine::aggregate(const NotificationEvent& event) {
    // TODO: Implement event aggregation
    // For now, pass through without aggregation
    return event;
}

astro_mount::NotificationEvent NotificationEngine::toProto(const NotificationEvent& event) const {
    astro_mount::NotificationEvent proto;

    proto.set_id(event.id);
    proto.set_severity(static_cast<astro_mount::EventSeverity>(event.severity));
    proto.set_category(static_cast<astro_mount::EventCategory>(event.category));
    proto.set_source(event.source);
    proto.set_title(event.title);
    proto.set_message(event.message);
    proto.set_acknowledged(event.acknowledged);

    if (event.timestamp.time_since_epoch().count() > 0) {
        auto proto_time = proto.mutable_timestamp();
        auto duration = event.timestamp.time_since_epoch();
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
        auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count() % 1000000000;
        proto_time->set_seconds(secs);
        proto_time->set_nanos(static_cast<int32_t>(nanos));
    }

    for (const auto& [key, value] : event.metadata) {
        (*proto.mutable_metadata())[key] = value;
    }

    return proto;
}

NotificationEvent NotificationEngine::fromProto(const astro_mount::NotificationEvent& proto) const {
    NotificationEvent event;

    event.id = proto.id();
    event.severity = static_cast<Severity>(proto.severity());
    event.category = static_cast<Category>(proto.category());
    event.source = proto.source();
    event.title = proto.title();
    event.message = proto.message();
    event.acknowledged = proto.acknowledged();

    if (proto.has_timestamp()) {
        event.timestamp = std::chrono::system_clock::from_time_t(proto.timestamp().seconds());
    }

    for (const auto& [key, value] : proto.metadata()) {
        event.metadata[key] = value;
    }

    return event;
}

} // namespace notifications
} // namespace astro_mount
