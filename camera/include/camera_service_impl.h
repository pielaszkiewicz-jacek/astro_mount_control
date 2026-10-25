#ifndef CAMERA_SERVICE_IMPL_H
#define CAMERA_SERVICE_IMPL_H

#include <memory>
#include <mutex>
#include <string>
#include <grpcpp/grpcpp.h>
#include "proto/camera.grpc.pb.h"

/**
 * @brief gRPC implementation of the CameraService — INDI bridge only.
 *
 * Hosted in-process on the unified gRPC port (50051). Drives a real CCD device
 * (e.g. PlayerOne `indi_playerone`) through a running indiserver. Camera devices
 * can be listed and selected at runtime via ListCameras/SelectCamera, and any
 * INDI property exposed by the selected device is read/written via
 * ListProperties/SetProperty.
 */
namespace astro_camera {

class IndiCameraClient;

class CameraServiceImpl final : public astro_mount::CameraService::Service {
public:
    /// INDI bridge configuration.
    struct IndiCameraConfig {
        bool enabled{false};
        std::string host{"localhost"};
        int port{7624};
        std::string device;   // INDI device name, e.g. "PlayerOne"
    };

    explicit CameraServiceImpl(IndiCameraConfig indi_config);
    ~CameraServiceImpl() override;

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

    grpc::Status ListCameras(grpc::ServerContext* context,
                             const google::protobuf::Empty* request,
                             astro_mount::CameraDeviceList* response) override;

    grpc::Status SelectCamera(grpc::ServerContext* context,
                              const astro_mount::CameraSelectRequest* request,
                              google::protobuf::Empty* response) override;

    grpc::Status ListProperties(grpc::ServerContext* context,
                                const google::protobuf::Empty* request,
                                astro_mount::CameraPropertyList* response) override;

    grpc::Status SetProperty(grpc::ServerContext* context,
                             const astro_mount::SetCameraPropertyRequest* request,
                             google::protobuf::Empty* response) override;

private:
    /// Connect to indiserver (lazily) if not already connected.
    void ensureIndiServerConnected();
    /// @return true when an INDI camera device is selected.
    bool indiCameraActive();

    IndiCameraConfig indi_config_;
    std::unique_ptr<IndiCameraClient> indi_client_;
    std::mutex mtx_;
};

} // namespace astro_camera

#endif // CAMERA_SERVICE_IMPL_H
