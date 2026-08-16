#ifndef LOG_CHANNEL_H
#define LOG_CHANNEL_H

#include "notifications/notification_engine.h"
#include <iostream>

namespace astro_mount {
namespace notifications {

/**
 * @brief Log notification channel (R1).
 *
 * Always available — logs every notification event to the console/stdout.
 * Provides a meaningful default channel for the hosted NotificationService so
 * events are never silently dropped when no external channel is configured.
 */
class LogChannel : public NotificationChannel {
public:
    std::string name() const override { return "log"; }
    bool initialize() override { initialized_ = true; return true; }
    bool send(const NotificationEvent& event) override {
        if (!initialized_) return false;
        std::cout << "[notification] " << static_cast<int>(event.severity)
                  << " | " << event.source << " | " << event.title
                  << " | " << event.message << std::endl;
        return true;
    }
    void shutdown() override { initialized_ = false; }
    bool isHealthy() const override { return initialized_; }
private:
    bool initialized_{false};
};

} // namespace notifications
} // namespace astro_mount

#endif // LOG_CHANNEL_H
