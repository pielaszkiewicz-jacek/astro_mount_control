#ifndef DEROTATOR_SERVICE_IMPL_H
#define DEROTATOR_SERVICE_IMPL_H

#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <grpcpp/grpcpp.h>
#include "derotator/proto/derotator.grpc.pb.h"

namespace astro_mount {
namespace hal { class DerotatorControl; }
namespace controllers { class DerotatorController; }
namespace models { struct FieldRotationResult; }
} // namespace astro_mount

namespace astro_derotator {

class DerotatorServiceImpl final : public DerotatorService::Service {
public:
    explicit DerotatorServiceImpl(const std::string& config_path);
    ~DerotatorServiceImpl() override;

    grpc::Status SetMode(grpc::ServerContext* context,
                         const SetModeRequest* request,
                         google::protobuf::Empty* response) override;

    grpc::Status SetAngle(grpc::ServerContext* context,
                          const SetAngleRequest* request,
                          google::protobuf::Empty* response) override;

    grpc::Status SetRate(grpc::ServerContext* context,
                         const SetRateRequest* request,
                         google::protobuf::Empty* response) override;

    grpc::Status Home(grpc::ServerContext* context,
                      const google::protobuf::Empty* request,
                      google::protobuf::Empty* response) override;

    grpc::Status GetStatus(grpc::ServerContext* context,
                           const google::protobuf::Empty* request,
                           DerotatorStatus* response) override;

    grpc::Status WatchStatus(grpc::ServerContext* context,
                             const google::protobuf::Empty* request,
                             grpc::ServerWriter<DerotatorStatus>* writer) override;

    grpc::Status GetFieldRotation(grpc::ServerContext* context,
                                  const google::protobuf::Empty* request,
                                  FieldRotationResult* response) override;

    grpc::Status UpdateMountPosition(grpc::ServerContext* context,
                                     const MountPositionUpdate* request,
                                     google::protobuf::Empty* response) override;

    grpc::Status CheckHealth(grpc::ServerContext* context,
                             const google::protobuf::Empty* request,
                             google::protobuf::Empty* response) override;

    // ============================================
    // In-process integration API (mount controller)
    // ============================================

    /**
     * @brief Feed the latest mount position directly (no gRPC round-trip).
     *
     * Used when the derotator subsystem is hosted in-process inside the
     * mount controller. Equivalent to the UpdateMountPosition RPC.
     *
     * @param axis1_deg Telescope axis1 position [degrees]
     * @param axis2_deg Telescope axis2 position [degrees]
     * @param latitude_deg Site latitude [degrees]
     * @param ha_hours Hour angle [hours]
     * @param dec_deg Declination [degrees]
     */
    void setMountPosition(double axis1_deg, double axis2_deg,
                          double latitude_deg, double ha_hours, double dec_deg);

private:
    void populateStatus(DerotatorStatus* status) const;
    void populateFieldRotation(FieldRotationResult* result) const;

    /// Callback invoked by the controller when mount position updates arrive
    void onMountPositionUpdate(double axis1_deg, double axis2_deg);

    std::unique_ptr<astro_mount::controllers::DerotatorController> controller_;
    std::string config_path_;
    bool initialized_{false};

    // WatchStatus
    std::atomic<bool> watching_{false};
    std::unique_ptr<std::thread> watch_thread_;

    // Thread safety
    mutable std::mutex mtx_;

    // Latest mount position for field rotation calculation
    std::atomic<double> last_axis1_{0.0};
    std::atomic<double> last_axis2_{0.0};
    double mount_latitude_{52.0};
    double ha_hours_{0.0};
    double dec_deg_{0.0};
};

} // namespace astro_derotator

#endif // DEROTATOR_SERVICE_IMPL_H
