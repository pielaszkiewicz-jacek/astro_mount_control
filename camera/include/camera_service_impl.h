#ifndef CAMERA_SERVICE_IMPL_H
#define CAMERA_SERVICE_IMPL_H

#include <mutex>
#include <grpcpp/grpcpp.h>
#include "proto/camera.grpc.pb.h"

/**
 * @brief gRPC implementation of the CameraService (R3).
 *
 * Hosted in-process on the unified gRPC port (50051). Uses a SIMULATED camera
 * (no ZWO/SDK HAL available) — the simulated nature is reported through
 * CameraInfo (name "Simulated Camera", driver "simulated-1.0") so the UI never
 * shows it as real hardware. A real camera HAL can replace the simulated model
 * without changing the service interface.
 */
namespace astro_camera {

class CameraServiceImpl final : public astro_mount::CameraService::Service {
public:
    CameraServiceImpl() = default;

    grpc::Status StartExposure(grpc::ServerContext* context,
                               const astro_mount::ExposureRequest* request,
                               grpc::ServerWriter<astro_mount::ExposureProgress>* writer) override;

    grpc::Status AbortExposure(grpc::ServerContext* context,
                               const google::protobuf::Empty* request,
                               google::protobuf::Empty* response) override;

    grpc::Status GetCameraInfo(grpc::ServerContext* context,
                               const google::protobuf::Empty* request,
                               astro_mount::CameraInfo* response) override;

    grpc::Status SetFilter(grpc::ServerContext* context,
                           const astro_mount::FilterRequest* request,
                           google::protobuf::Empty* response) override;

    grpc::Status GetFilterPosition(grpc::ServerContext* context,
                                   const google::protobuf::Empty* request,
                                   astro_mount::FilterPosition* response) override;

    grpc::Status StartVideoPreview(grpc::ServerContext* context,
                                   const astro_mount::VideoPreviewRequest* request,
                                   grpc::ServerWriter<astro_mount::VideoFrame>* writer) override;

    grpc::Status StopVideoPreview(grpc::ServerContext* context,
                                  const google::protobuf::Empty* request,
                                  google::protobuf::Empty* response) override;

    grpc::Status SetCooler(grpc::ServerContext* context,
                           const astro_mount::SetCoolerRequest* request,
                           astro_mount::CoolerStatus* response) override;

    grpc::Status GetCoolerStatus(grpc::ServerContext* context,
                                 const google::protobuf::Empty* request,
                                 astro_mount::CoolerStatus* response) override;

private:
    std::mutex mtx_;
    int filter_position_{0};
    bool cooler_enabled_{false};
    double cooler_target_c_{-10.0};
    double cooler_current_c_{20.0};
    double cooler_ambient_c_{20.0};
    double cooler_power_percent_{0.0};
};

} // namespace astro_camera

#endif // CAMERA_SERVICE_IMPL_H
