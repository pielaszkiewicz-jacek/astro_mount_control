#ifndef GRPC_CLIENT_H
#define GRPC_CLIENT_H
#include <QObject>
#include <grpcpp/grpcpp.h>
#include "mount_controller.grpc.pb.h"
#include <google/protobuf/empty.pb.h>

class GrpcClient : public QObject {
    Q_OBJECT
public:
    explicit GrpcClient(const std::string& address, QObject* parent = nullptr);
    ~GrpcClient();

    astro_mount::ControllerState getState();
    astro_mount::ControllerState::MountStatus getStatus();
    bool slewTo(double ra, double dec);
    bool track(double ra, double dec);
    bool stop();
    bool park();
    bool unpark();
    bool clearErrors();

    /// Low-level axis control for uncalibrated mounts (velocity/position mode)
    bool controlAxis(int axis_id, int mode, double target_position,
                     double target_velocity, double acceleration, bool relative);
    bool stopAxis(int axis_id, bool decelerate, double deceleration);
    bool emergencyStop(int axis_id, bool reset_after);

    /// Configuration management
    astro_mount::Configuration getConfig();
    bool updateConfig(const astro_mount::Configuration& config);

    /// Reconnect to a different gRPC server address (host:port)
    void reconnect(const std::string& address);

    /// Returns the current connection address
    std::string address() const { return address_; }

private:
    std::string address_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<astro_mount::MountControllerService::Stub> stub_;
};

#endif
