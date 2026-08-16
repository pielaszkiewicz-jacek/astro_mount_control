#ifndef PULLEY_SERVICE_IMPL_H
#define PULLEY_SERVICE_IMPL_H

#include <mutex>
#include <chrono>
#include <grpcpp/grpcpp.h>
#include "proto/pulley.grpc.pb.h"

/**
 * @brief gRPC implementation of the PulleyService (R3).
 *
 * Hosted in-process on the unified gRPC port (50051). Uses a SIMULATED linear
 * actuator model (position tracking toward a target). Reported as
 * "Simulated Pulley" in PulleyStatus.model_name so the UI never mistakes it
 * for real hardware. A real motor HAL can replace the model without changing
 * the service interface.
 */
namespace astro_pulley {

class PulleyServiceImpl final : public astro_mount::PulleyService::Service {
public:
    PulleyServiceImpl() = default;

    grpc::Status DeployPulley(grpc::ServerContext* context,
                              const astro_mount::PulleyMoveRequest* request,
                              google::protobuf::Empty* response) override;

    grpc::Status RetractPulley(grpc::ServerContext* context,
                               const google::protobuf::Empty* request,
                               google::protobuf::Empty* response) override;

    grpc::Status StopPulley(grpc::ServerContext* context,
                            const google::protobuf::Empty* request,
                            google::protobuf::Empty* response) override;

    grpc::Status SetPulleyPosition(grpc::ServerContext* context,
                                   const astro_mount::PulleyPositionRequest* request,
                                   google::protobuf::Empty* response) override;

    grpc::Status HomePulley(grpc::ServerContext* context,
                            const google::protobuf::Empty* request,
                            grpc::ServerWriter<astro_mount::PulleyHomeProgress>* writer) override;

    grpc::Status GetPulleyStatus(grpc::ServerContext* context,
                                 const google::protobuf::Empty* request,
                                 astro_mount::PulleyStatus* response) override;

    grpc::Status SetPulleySpeed(grpc::ServerContext* context,
                                const astro_mount::PulleySpeedRequest* request,
                                google::protobuf::Empty* response) override;

private:
    void integratePosition() const;

    mutable std::mutex mtx_;
    mutable int position_percent_{0};
    int target_percent_{0};
    int speed_percent_{50};
    bool homed_{false};
    mutable std::chrono::steady_clock::time_point last_update_{std::chrono::steady_clock::now()};
};

} // namespace astro_pulley

#endif // PULLEY_SERVICE_IMPL_H
