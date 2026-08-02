#ifndef GRPC_CLIENT_H
#define GRPC_CLIENT_H
#include <QObject>
#include <grpcpp/grpcpp.h>
#include "mount_controller.grpc.pb.h"

class GrpcClient : public QObject {
    Q_OBJECT
public:
    explicit GrpcClient(const std::string& address, QObject* parent = nullptr);
    ~GrpcClient();

    astro_mount::ControllerState getState();
    astro_mount::MountStatus getStatus();
    bool slewTo(double ra, double dec);
    bool track(double ra, double dec);
    bool stop();
    bool park();

private:
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<astro_mount::MountControllerService::Stub> stub_;
};

#endif
