#include "notifications/notification_engine.h"
#include "notifications/channels/email_channel.h"
#include "notifications/channels/webhook_channel.h"
#include "notifications/channels/mqtt_channel.h"
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
        try {
            auto category = static_cast<Category>(std::stoi(entry.first));
            config_.enabled_categories[category] = entry.second;
        } catch (...) {
            // ignore non-numeric event keys
        }
    }

    // Update aggregation settings
    config_.aggregate_messages = config.aggregate_messages();
    config_.aggregation_interval_minutes = std::max(1, config.aggregation_interval_minutes());

    // N2: build channels from the proto config. Remove any previously-managed
    // email/webhook/mqtt channels first, then re-create from config. The
    // always-on "log" channel (and any channels registered outside configure)
    // are left untouched.
    clearManagedChannels();
    for (const auto& cc : config.channels()) {
        if (!cc.enabled()) continue;

        std::unique_ptr<NotificationChannel> channel;
        switch (cc.type()) {
            case astro_mount::CHANNEL_EMAIL: {
                EmailChannel::Config cfg;
                cfg.smtp_host = cc.email().smtp_host();
                cfg.smtp_port = cc.email().smtp_port() > 0 ? cc.email().smtp_port() : 587;
                cfg.use_tls = cc.email().use_tls();
                cfg.username = cc.email().username();
                cfg.password = cc.email().password();
                cfg.from_address = cc.email().from_address();
                for (const auto& to : cc.email().to_addresses()) cfg.to_addresses.push_back(to);
                cfg.subject_prefix = cc.email().subject_prefix();
                channel = std::make_unique<EmailChannel>(cfg);
                break;
            }
            case astro_mount::CHANNEL_WEBHOOK: {
                WebhookChannel::Config cfg;
                cfg.url = cc.webhook().url();
                cfg.method = cc.webhook().method().empty() ? "POST" : cc.webhook().method();
                for (const auto& [k, v] : cc.webhook().headers()) cfg.headers[k] = v;
                cfg.auth_token = cc.webhook().auth_token();
                cfg.timeout_seconds = cc.webhook().timeout_seconds() > 0 ? cc.webhook().timeout_seconds() : 10;
                cfg.retry_count = cc.webhook().retry_count();
                channel = std::make_unique<WebhookChannel>(cfg);
                break;
            }
            case astro_mount::CHANNEL_MQTT: {
                MqttChannel::Config cfg;
                cfg.broker_url = cc.mqtt().broker_url();
                cfg.broker_port = cc.mqtt().broker_port() > 0 ? cc.mqtt().broker_port() : 1883;
                cfg.client_id = cc.mqtt().client_id();
                cfg.topic_prefix = cc.mqtt().topic_prefix();
                cfg.use_tls = cc.mqtt().use_tls();
                cfg.username = cc.mqtt().username();
                cfg.password = cc.mqtt().password();
                cfg.qos = std::clamp(cc.mqtt().qos(), 0, 2);
                cfg.retain = cc.mqtt().retain();
                channel = std::make_unique<MqttChannel>(cfg);
                break;
            }
            default:
                continue;
        }

        if (channel && channel->initialize()) {
            channels_.push_back(std::move(channel));
        }
    }
}

void NotificationEngine::clearManagedChannels() {
    // Called with mutex_ held. Remove channels created by configure() so a
    // re-configure does not accumulate duplicates.
    channels_.erase(std::remove_if(channels_.begin(), channels_.end(),
        [](const auto& ch) {
            return ch->name() == "email" || ch->name() == "webhook" || ch->name() == "mqtt";
        }), channels_.end());
}

astro_mount::NotificationStatus NotificationEngine::getStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);

    astro_mount::NotificationStatus status;
    status.set_configured(!channels_.empty());
    status.set_active_channels(static_cast<int32_t>(channels_.size()));
    status.set_events_sent_total(events_sent_total_);

    // N4: rolling 1-hour counter — count send timestamps still within the
    // last hour (deliver() prunes the deque, so this is just a count).
    auto now = std::chrono::system_clock::now();
    int last_hour = 0;
    for (const auto& t : sent_timestamps_) {
        if (now - t <= std::chrono::hours(1)) ++last_hour;
    }
    status.set_events_sent_last_hour(last_hour);

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
        bool have_event = false;
        bool flush_pending = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(500), [this] {
                return !event_queue_.empty() || !running_;
            });

            if (!running_) break;

            // N4: flush the current aggregation window once it expires.
            if (config_.aggregate_messages && pending_aggregate_) {
                auto deadline = pending_aggregate_time_ +
                    std::chrono::minutes(config_.aggregation_interval_minutes);
                if (std::chrono::system_clock::now() >= deadline) {
                    flush_pending = true;
                }
            }

            if (!event_queue_.empty()) {
                event = std::move(event_queue_.front());
                event_queue_.pop();
                have_event = true;
            }
        }

        // N4: windowed aggregation — matching events are merged into the
        // pending digest and delivered only when the window expires or a
        // non-matching event arrives.
        if (config_.aggregate_messages) {
            if (flush_pending && pending_aggregate_) {
                deliver(*pending_aggregate_);
                pending_aggregate_.reset();
                pending_aggregate_count_ = 0;
            }
            if (have_event) {
                if (pending_aggregate_ && matchesAggregate(*pending_aggregate_, event)) {
                    // Merge into the pending digest — do not deliver yet.
                    std::ostringstream merged;
                    merged << pending_aggregate_->message << "\n  • " << event.message;
                    pending_aggregate_->message = merged.str();
                    pending_aggregate_->metadata["aggregated_count"] =
                        std::to_string(++pending_aggregate_count_);
                } else {
                    if (pending_aggregate_) {
                        deliver(*pending_aggregate_);
                        pending_aggregate_.reset();
                        pending_aggregate_count_ = 0;
                    }
                    pending_aggregate_ = event;
                    pending_aggregate_time_ = std::chrono::system_clock::now();
                    pending_aggregate_count_ = 1;
                    pending_aggregate_->metadata["aggregated_count"] = "1";
                }
            }
            continue;  // aggregated events are delivered only via deliver()
        }

        // No aggregation — deliver directly.
        if (have_event) {
            deliver(event);
        }
    }

    // Flush any remaining aggregated digest on shutdown.
    if (config_.aggregate_messages && pending_aggregate_) {
        deliver(*pending_aggregate_);
        pending_aggregate_.reset();
        pending_aggregate_count_ = 0;
    }
}

void NotificationEngine::deliver(const NotificationEvent& event) {
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

        if (any_success) {
            events_sent_total_++;
            last_event_time_ = std::chrono::system_clock::now();
            // N4: record send time for the rolling 1-hour counter and prune
            // timestamps older than an hour.
            auto now = last_event_time_;
            sent_timestamps_.push_back(now);
            while (!sent_timestamps_.empty() && now - sent_timestamps_.front() > std::chrono::hours(1)) {
                sent_timestamps_.pop_front();
            }
        }

        // Notify subscribers
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

bool NotificationEngine::matchesAggregate(const NotificationEvent& base,
                                          const NotificationEvent& event) const {
    return base.category == event.category &&
           base.source == event.source &&
           base.title == event.title &&
           base.severity == event.severity;
}

NotificationEvent NotificationEngine::aggregate(const NotificationEvent& event) {
    // Kept for API compatibility — the windowed logic lives in workerLoop().
    // Called with a matching pending window this merges the event into the
    // pending digest; otherwise it is a pass-through for the current event.
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_aggregate_ && matchesAggregate(*pending_aggregate_, event)) {
        std::ostringstream merged;
        merged << pending_aggregate_->message << "\n  • " << event.message;
        pending_aggregate_->message = merged.str();
        pending_aggregate_->metadata["aggregated_count"] =
            std::to_string(++pending_aggregate_count_);
        return *pending_aggregate_;
    }
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
