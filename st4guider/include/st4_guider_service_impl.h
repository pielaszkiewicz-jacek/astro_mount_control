#ifndef ST4_GUIDER_SERVICE_IMPL_H
#define ST4_GUIDER_SERVICE_IMPL_H

#include <memory>
#include <mutex>
#include <string>
#include <grpcpp/grpcpp.h>
#include "proto/st4_guider.grpc.pb.h"

namespace astro_mount {
namespace controllers { class St4Guider; }
namespace hal { class St4Control; }
} // namespace astro_mount

/**
 * @brief gRPC implementation of the ST4 guider service (P2).
 *
 * Wraps the St4Guider controller (calibration + ST4 pulse conversion) and
 * exposes it over gRPC. Hosted in-process inside the mount controller on the
 * unified gRPC port (50051), like the dome/derotator/focuser subsystems.
 */
namespace astro_st4guider {

class St4GuiderServiceImpl final : public astro_mount::St4GuiderService::Service {
public:
    explicit St4GuiderServiceImpl(const std::string& config_path);
    ~St4GuiderServiceImpl() override;

    grpc::Status StartGuiding(grpc::ServerContext* context,
                              const astro_mount::St4GuiderConfig* request,
                              google::protobuf::Empty* response) override;

    grpc::Status StopGuiding(grpc::ServerContext* context,
                             const google::protobuf::Empty* request,
                             google::protobuf::Empty* response) override;

    grpc::Status GetStatus(grpc::ServerContext* context,
                           const google::protobuf::Empty* request,
                           astro_mount::St4Status* response) override;

    grpc::Status PulseGuide(grpc::ServerContext* context,
                            const astro_mount::St4Pulse* request,
                            google::protobuf::Empty* response) override;

    grpc::Status Calibrate(grpc::ServerContext* context,
                           const astro_mount::St4CalibrateRequest* request,
                           grpc::ServerWriter<astro_mount::St4CalibrateProgress>* writer) override;

private:
    bool configureFromJson(const std::string& config_path);
    void populateStatus(astro_mount::St4Status* status) const;

    std::unique_ptr<astro_mount::controllers::St4Guider> guider_;
    std::string interface_type_{"simulated"};
    // PHD2 endpoint — configurable (default localhost:4400).
    std::string phd2_host_{"localhost"};
    int phd2_port_{4400};
    bool guiding_{false};
    bool connected_{false};
    bool phd2_connected_{false};   // N3: live PHD2 connection state
    mutable std::mutex mutex_;
};

} // namespace astro_st4guider

#endif // ST4_GUIDER_SERVICE_IMPL_H
