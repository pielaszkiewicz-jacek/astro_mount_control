#include "notifications/notification_service.h"
#include <chrono>

namespace astro_mount {
namespace notifications {

NotificationServiceImpl::NotificationServiceImpl(std::shared_ptr<NotificationEngine> engine)
    : engine_(std::move(engine)) {
}

grpc::Status NotificationServiceImpl::ConfigureNotifications(
    grpc::ServerContext* context,
    const astro_mount::NotificationConfig* request,
    google::protobuf::Empty* response) {

    if (!engine_) {
        return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                            "Notification engine not initialized");
    }

    engine_->configure(*request);
    return grpc::Status::OK;
}

grpc::Status NotificationServiceImpl::GetNotificationStatus(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    astro_mount::NotificationStatus* response) {

    if (!engine_) {
        return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                            "Notification engine not initialized");
    }

    *response = engine_->getStatus();
    return grpc::Status::OK;
}

grpc::Status NotificationServiceImpl::SendTestNotification(
    grpc::ServerContext* context,
    const astro_mount::TestNotificationRequest* request,
    google::protobuf::Empty* response) {

    if (!engine_) {
        return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                            "Notification engine not initialized");
    }

    std::string message = request->message().empty()
        ? "Test notification from Astro Mount Controller"
        : request->message();

    engine_->sendTestNotification(message);
    return grpc::Status::OK;
}

grpc::Status NotificationServiceImpl::SubscribeToEvents(
    grpc::ServerContext* context,
    const astro_mount::EventSubscription* request,
    grpc::ServerWriter<astro_mount::NotificationEvent>* writer) {

    if (!engine_) {
        return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                            "Notification engine not initialized");
    }

    // Subscribe to all events and stream them to the client
    int subscription_id = engine_->subscribe(
        [writer](const NotificationEvent& event) {
            astro_mount::NotificationEvent proto;
            proto.set_id(event.id);

            auto timestamp = std::chrono::system_clock::to_time_t(event.timestamp);
            auto proto_time = proto.mutable_timestamp();
            proto_time->set_seconds(timestamp);
            proto_time->set_nanos(0);

            proto.set_severity(static_cast<astro_mount::EventSeverity>(event.severity));
            proto.set_category(static_cast<astro_mount::EventCategory>(event.category));
            proto.set_source(event.source);
            proto.set_title(event.title);
            proto.set_message(event.message);

            writer->Write(proto);
        }
    );

    // Keep the RPC active until the client disconnects
    while (!context->IsCancelled()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    engine_->unsubscribe(subscription_id);
    return grpc::Status::OK;
}

} // namespace notifications
} // namespace astro_mount
