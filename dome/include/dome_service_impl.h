#ifndef DOME_SERVICE_IMPL_H
#define DOME_SERVICE_IMPL_H

#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <grpcpp/grpcpp.h>
#include "dome/proto/dome.grpc.pb.h"

namespace astro_mount {
namespace hal {
class DomeControl;
} // namespace hal
namespace controllers {
class DomeController;
} // namespace controllers
} // namespace astro_mount

namespace astro_dome {

/**
 * @brief Implementation of the DomeService gRPC interface
 *
 * Owns and manages a DomeController instance, translating gRPC
 * requests into dome HAL operations.
 */
class DomeServiceImpl final : public DomeService::Service {
public:
    /**
     * @brief Construct the service implementation
     * @param config_path Path to JSON configuration for initialising the dome HAL
     */
    explicit DomeServiceImpl(const std::string& config_path);
    ~DomeServiceImpl() override;

    // ============================================
    // gRPC service methods
    // ============================================

    grpc::Status OpenShutter(grpc::ServerContext* context,
                             const google::protobuf::Empty* request,
                             google::protobuf::Empty* response) override;

    grpc::Status CloseShutter(grpc::ServerContext* context,
                              const google::protobuf::Empty* request,
                              google::protobuf::Empty* response) override;

    grpc::Status RotateTo(grpc::ServerContext* context,
                          const RotateRequest* request,
                          google::protobuf::Empty* response) override;

    grpc::Status Halt(grpc::ServerContext* context,
                      const google::protobuf::Empty* request,
                      google::protobuf::Empty* response) override;

    grpc::Status Park(grpc::ServerContext* context,
                      const google::protobuf::Empty* request,
                      google::protobuf::Empty* response) override;

    grpc::Status Unpark(grpc::ServerContext* context,
                        const google::protobuf::Empty* request,
                        google::protobuf::Empty* response) override;

    grpc::Status GoHome(grpc::ServerContext* context,
                        const google::protobuf::Empty* request,
                        google::protobuf::Empty* response) override;

    grpc::Status GetStatus(grpc::ServerContext* context,
                           const google::protobuf::Empty* request,
                           DomeStatus* response) override;

    grpc::Status WatchStatus(grpc::ServerContext* context,
                             const google::protobuf::Empty* request,
                             grpc::ServerWriter<DomeStatus>* writer) override;

    grpc::Status SetAutoSync(grpc::ServerContext* context,
                             const AutoSyncConfig* request,
                             google::protobuf::Empty* response) override;

    grpc::Status GetAutoSync(grpc::ServerContext* context,
                             const google::protobuf::Empty* request,
                             AutoSyncConfig* response) override;

    grpc::Status UpdateMountAzimuth(grpc::ServerContext* context,
                                    const MountAzimuth* request,
                                    google::protobuf::Empty* response) override;

    grpc::Status CheckHealth(grpc::ServerContext* context,
                             const google::protobuf::Empty* request,
                             google::protobuf::Empty* response) override;

    // ============================================
    // In-process integration API (mount controller)
    // ============================================

    /**
     * @brief Feed the latest mount azimuth directly (no gRPC round-trip).
     *
     * Used when the dome subsystem is hosted in-process inside the mount
     * controller. Equivalent to the UpdateMountAzimuth RPC.
     *
     * @param azimuth_deg Mount telescope azimuth in degrees
     */
    void setMountAzimuth(double azimuth_deg);

private:
    /// Populate a DomeStatus proto message from the current controller state
    void populateDomeStatus(DomeStatus* status) const;

    /// Initialise the mount azimuth callback on the controller
    void initMountAzimuthCallback();

    std::unique_ptr<astro_mount::controllers::DomeController> controller_;
    std::string config_path_;
    bool initialized_{false};

    // Thread safety for controller access
    mutable std::mutex controller_mutex_;

    // Latest mount azimuth received from the mount controller process
    // Used by the dome auto-sync loop via the callback.
    std::atomic<double> latest_mount_azimuth_{0.0};
    bool mount_azimuth_callback_initialized_{false};
};

} // namespace astro_dome

#endif // DOME_SERVICE_IMPL_H
