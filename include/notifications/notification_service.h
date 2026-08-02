#ifndef NOTIFICATION_SERVICE_H
#define NOTIFICATION_SERVICE_H

#include <memory>
#include "proto/notification.grpc.pb.h"
#include "notifications/notification_engine.h"

namespace astro_mount {
namespace notifications {

/**
 * @brief gRPC service implementation for NotificationService
 *
 * Bridges gRPC API calls to the NotificationEngine.
 * Handles ConfigureNotifications, GetNotificationStatus,
 * SendTestNotification, and SubscribeToEvents RPCs.
 */
class NotificationServiceImpl final : public astro_mount::NotificationService::Service {
public:
    explicit NotificationServiceImpl(std::shared_ptr<NotificationEngine> engine);
    ~NotificationServiceImpl() override = default;

    grpc::Status ConfigureNotifications(
        grpc::ServerContext* context,
        const astro_mount::NotificationConfig* request,
        google::protobuf::Empty* response) override;

    grpc::Status GetNotificationStatus(
        grpc::ServerContext* context,
        const google::protobuf::Empty* request,
        astro_mount::NotificationStatus* response) override;

    grpc::Status SendTestNotification(
        grpc::ServerContext* context,
        const astro_mount::TestNotificationRequest* request,
        google::protobuf::Empty* response) override;

    grpc::Status SubscribeToEvents(
        grpc::ServerContext* context,
        const astro_mount::EventSubscription* request,
        grpc::ServerWriter<astro_mount::NotificationEvent>* writer) override;

private:
    std::shared_ptr<NotificationEngine> engine_;
};

} // namespace notifications
} // namespace astro_mount

#endif // NOTIFICATION_SERVICE_H
