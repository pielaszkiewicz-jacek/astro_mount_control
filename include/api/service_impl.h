#ifndef SERVICE_IMPL_H
#define SERVICE_IMPL_H

#include <grpcpp/grpcpp.h>
#include "proto/mount_controller.grpc.pb.h"

namespace astro_mount {
namespace controllers {
class MountController;
} // namespace controllers

namespace api {

/**
 * @brief Implementation of the gRPC service
 * 
 * Handles all gRPC requests and translates them to
 * mount controller operations.
 */
class MountControllerServiceImpl final : public astro_mount::MountControllerService::Service {
public:
    /**
     * @brief Construct a new MountControllerServiceImpl object
     * @param controller Reference to mount controller
     */
    explicit MountControllerServiceImpl(controllers::MountController& controller);
    
    // Basic mount control
    grpc::Status SlewToCoordinates(grpc::ServerContext* context,
                                   const astro_mount::Coordinates* request,
                                   google::protobuf::Empty* response) override;
    
    grpc::Status SlewToHorizontal(grpc::ServerContext* context,
                                  const astro_mount::HorizontalCoordinates* request,
                                  google::protobuf::Empty* response) override;
    
    grpc::Status TrackObject(grpc::ServerContext* context,
                             const astro_mount::Coordinates* request,
                             google::protobuf::Empty* response) override;
    
    grpc::Status Stop(grpc::ServerContext* context,
                      const google::protobuf::Empty* request,
                      google::protobuf::Empty* response) override;
    
    grpc::Status Park(grpc::ServerContext* context,
                      const google::protobuf::Empty* request,
                      google::protobuf::Empty* response) override;
    
    grpc::Status Unpark(grpc::ServerContext* context,
                        const google::protobuf::Empty* request,
                        google::protobuf::Empty* response) override;
    
    // State management
    grpc::Status GetState(grpc::ServerContext* context,
                          const google::protobuf::Empty* request,
                          astro_mount::ControllerState* response) override;
    
    grpc::Status WatchState(grpc::ServerContext* context,
                            const google::protobuf::Empty* request,
                            grpc::ServerWriter<astro_mount::ControllerState>* writer) override;
    
    grpc::Status SaveState(grpc::ServerContext* context,
                           const astro_mount::StateSaveRequest* request,
                           astro_mount::StateSaveResponse* response) override;
    
    grpc::Status LoadState(grpc::ServerContext* context,
                           const astro_mount::StateLoadRequest* request,
                           google::protobuf::Empty* response) override;
    
    grpc::Status ClearErrors(grpc::ServerContext* context,
                             const google::protobuf::Empty* request,
                             google::protobuf::Empty* response) override;
    
    // Measurement and calibration
    grpc::Status AddMeasurement(grpc::ServerContext* context,
                                const astro_mount::Measurement* request,
                                google::protobuf::Empty* response) override;
    
    // Bootstrap calibration API (for initial alignment)
    grpc::Status AddBootstrapMeasurement(grpc::ServerContext* context,
                                         const astro_mount::BootstrapMeasurement* request,
                                         google::protobuf::Empty* response) override;
    
    grpc::Status RunBootstrapCalibration(grpc::ServerContext* context,
                                         const google::protobuf::Empty* request,
                                         astro_mount::BootstrapCalibrationResult* response) override;
    
    grpc::Status GetBootstrapStatus(grpc::ServerContext* context,
                                    const google::protobuf::Empty* request,
                                    astro_mount::BootstrapStatus* response) override;
    
    grpc::Status ClearBootstrapMeasurements(grpc::ServerContext* context,
                                            const google::protobuf::Empty* request,
                                            google::protobuf::Empty* response) override;
    
    // TPOINT calibration API (full precision)
    grpc::Status AddTPointMeasurement(grpc::ServerContext* context,
                                      const astro_mount::Measurement* request,
                                      google::protobuf::Empty* response) override;
    
    grpc::Status ClearTPointMeasurements(grpc::ServerContext* context,
                                         const google::protobuf::Empty* request,
                                         google::protobuf::Empty* response) override;
    
    grpc::Status RunTPointCalibration(grpc::ServerContext* context,
                                      const google::protobuf::Empty* request,
                                      google::protobuf::Empty* response) override;
    
    grpc::Status GetTPointParameters(grpc::ServerContext* context,
                                     const google::protobuf::Empty* request,
                                     astro_mount::TPointParameters* response) override;
    
    grpc::Status GetRotationMatrix(grpc::ServerContext* context,
                                   const google::protobuf::Empty* request,
                                   astro_mount::RotationMatrix* response) override;
    
    // Pole position determination
    grpc::Status DeterminePolePosition(grpc::ServerContext* context,
                                       const astro_mount::PoleDeterminationRequest* request,
                                       astro_mount::PolePosition* response) override;
    
    // Encoder control
    grpc::Status EnableEncoders(grpc::ServerContext* context,
                                const astro_mount::EncoderConfig* request,
                                google::protobuf::Empty* response) override;
    
    grpc::Status DisableEncoders(grpc::ServerContext* context,
                                 const google::protobuf::Empty* request,
                                 google::protobuf::Empty* response) override;
    
    // Guider control
    grpc::Status ConnectGuider(grpc::ServerContext* context,
                               const astro_mount::GuiderConfig* request,
                               google::protobuf::Empty* response) override;
    
    grpc::Status DisconnectGuider(grpc::ServerContext* context,
                                  const google::protobuf::Empty* request,
                                  google::protobuf::Empty* response) override;
    
    grpc::Status SendGuiderCorrection(grpc::ServerContext* context,
                                      const astro_mount::GuiderCorrection* request,
                                      google::protobuf::Empty* response) override;
    
    // Configuration
    grpc::Status GetConfiguration(grpc::ServerContext* context,
                                  const google::protobuf::Empty* request,
                                  astro_mount::Configuration* response) override;
    
    grpc::Status UpdateConfiguration(grpc::ServerContext* context,
                                     const astro_mount::Configuration* request,
                                     google::protobuf::Empty* response) override;
    
    // Health check
    grpc::Status CheckHealth(grpc::ServerContext* context,
                             const astro_mount::HealthCheckRequest* request,
                             astro_mount::HealthCheckResponse* response) override;
    
    // Ephemeris-based tracking for moving objects
    grpc::Status UploadEphemeris(grpc::ServerContext* context,
                                 const astro_mount::EphemerisData* request,
                                 google::protobuf::Empty* response) override;
    
    grpc::Status StartEphemerisTracking(grpc::ServerContext* context,
                                        const astro_mount::StartEphemerisTrackingRequest* request,
                                        astro_mount::EphemerisTrackStatus* response) override;
    
    grpc::Status StartEphemerisTrackingWithData(grpc::ServerContext* context,
                                                const astro_mount::EphemerisTrackRequest* request,
                                                astro_mount::EphemerisTrackStatus* response) override;
    
    grpc::Status GetEphemerisTrackStatus(grpc::ServerContext* context,
                                         const google::protobuf::Empty* request,
                                         astro_mount::EphemerisTrackStatus* response) override;
    
    grpc::Status StopEphemerisTracking(grpc::ServerContext* context,
                                       const google::protobuf::Empty* request,
                                       google::protobuf::Empty* response) override;
    
    grpc::Status GetEphemerisMetrics(grpc::ServerContext* context,
                                     const google::protobuf::Empty* request,
                                     astro_mount::EphemerisMetrics* response) override;
    
    grpc::Status ClearEphemerisCache(grpc::ServerContext* context,
                                     const google::protobuf::Empty* request,
                                     google::protobuf::Empty* response) override;
    
    // ============================================
    // Low-level axis control API for uncalibrated mounts
    // ============================================
    
    grpc::Status ControlAxis(grpc::ServerContext* context,
                             const astro_mount::AxisControlRequest* request,
                             google::protobuf::Empty* response) override;
    
    grpc::Status StopAxis(grpc::ServerContext* context,
                          const astro_mount::AxisStopRequest* request,
                          google::protobuf::Empty* response) override;
    
    grpc::Status EmergencyStop(grpc::ServerContext* context,
                               const astro_mount::EmergencyStopRequest* request,
                               google::protobuf::Empty* response) override;
    
    grpc::Status GetAxisStatus(grpc::ServerContext* context,
                               const google::protobuf::Empty* request,
                               astro_mount::AxisStatus* response) override;
    
    // ============================================
    // FIELD ROTATION / DEROTATOR CONTROL
    // ============================================
    
    grpc::Status ConfigureDerotator(grpc::ServerContext* context,
                                    const astro_mount::DerotatorConfig* request,
                                    google::protobuf::Empty* response) override;
    
    grpc::Status EnableFieldRotation(grpc::ServerContext* context,
                                     const astro_mount::FieldRotationParams* request,
                                     google::protobuf::Empty* response) override;
    
    grpc::Status ControlFieldRotation(grpc::ServerContext* context,
                                      const astro_mount::FieldRotationControlRequest* request,
                                      google::protobuf::Empty* response) override;
    
    grpc::Status GetDerotatorStatus(grpc::ServerContext* context,
                                    const google::protobuf::Empty* request,
                                    astro_mount::DerotatorStatus* response) override;
    
    grpc::Status HomeDerotator(grpc::ServerContext* context,
                               const astro_mount::DerotatorHomingRequest* request,
                               google::protobuf::Empty* response) override;
    
    grpc::Status GetFieldRotationParams(grpc::ServerContext* context,
                                        const google::protobuf::Empty* request,
                                        astro_mount::FieldRotationParams* response) override;
    
    // ============================================
    // HAL Configuration RPCs
    // ============================================
    
    grpc::Status GetHALConfig(grpc::ServerContext* context,
                               const google::protobuf::Empty* request,
                               astro_mount::HALConfig* response) override;
    
    grpc::Status SetHALConfig(grpc::ServerContext* context,
                               const astro_mount::HALConfigRequest* request,
                               google::protobuf::Empty* response) override;
    
    grpc::Status GetHALStatus(grpc::ServerContext* context,
                               const google::protobuf::Empty* request,
                               astro_mount::HALStatus* response) override;
    
    grpc::Status ReinitializeHAL(grpc::ServerContext* context,
                                  const astro_mount::HALReinitRequest* request,
                                  google::protobuf::Empty* response) override;
    
    // ============================================
    // Mount orientation (CASUAL mount type)
    // ============================================
    
    grpc::Status SetMountOrientation(grpc::ServerContext* context,
                                     const astro_mount::MountOrientation* request,
                                     google::protobuf::Empty* response) override;
    
    grpc::Status GetMountOrientation(grpc::ServerContext* context,
                                     const google::protobuf::Empty* request,
                                     astro_mount::MountOrientation* response) override;
    
    // ============================================
    // Trajectory generation and execution
    // ============================================
    
    grpc::Status GenerateTrajectory(grpc::ServerContext* context,
                                    const astro_mount::TrajectoryParams* request,
                                    astro_mount::Trajectory* response) override;
    
    grpc::Status ExecuteTrajectory(grpc::ServerContext* context,
                                   const astro_mount::Trajectory* request,
                                   google::protobuf::Empty* response) override;
    
    grpc::Status StopTrajectory(grpc::ServerContext* context,
                                const google::protobuf::Empty* request,
                                google::protobuf::Empty* response) override;
    
private:
    controllers::MountController& controller_;
    
    // Helper methods
    astro_mount::Coordinates convertCoordinatesToProto(double ra, double dec) const;
    astro_mount::MountPosition convertMountPositionToProto(double axis1, double axis2) const;
    astro_mount::ControllerState::MountStatus convertStatus(
        int status) const;
    
    // Health check helpers
    astro_mount::SystemMetrics collectSystemMetrics() const;
    astro_mount::MountControllerMetrics collectMountMetrics() const;
    astro_mount::KalmanFilterMetrics collectKalmanMetrics() const;
    astro_mount::TPointMetrics collectTPointMetrics() const;
};

} // namespace api
} // namespace astro_mount

#endif // SERVICE_IMPL_H