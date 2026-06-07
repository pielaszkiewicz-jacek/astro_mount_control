#ifndef MOUNT_GRPC_CLIENT_H
#define MOUNT_GRPC_CLIENT_H

#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "mount_controller.grpc.pb.h"

/**
 * @brief gRPC client wrapper for MountControllerService.
 *
 * Faza 1 (Fundament): podstawowe połączenie, CheckHealth, GetState.
 * Faza 2 (Podstawowe sterowanie): SlewToCoordinates, Stop, Park, Unpark,
 *     TrackObject, GetConfiguration, UpdateConfiguration,
 *     AddBootstrapMeasurement, RunBootstrapCalibration, Sync.
 *
 * Thread-safe: każda metoda tworzy osobny ClientContext.
 * Reconnection: Connect() próbuje połączyć się z timeoutem.
 */
class MountGrpcClient
{
public:
    /**
     * @param host  gRPC server hostname or IP (default "localhost")
     * @param port  gRPC server port (default 50051)
     */
    MountGrpcClient(const std::string& host = "localhost", int port = 50051);

    ~MountGrpcClient();

    // Move-only (gRPC channel is not copyable)
    MountGrpcClient(MountGrpcClient&&) = default;
    MountGrpcClient& operator=(MountGrpcClient&&) = default;

    // No copies
    MountGrpcClient(const MountGrpcClient&) = delete;
    MountGrpcClient& operator=(const MountGrpcClient&) = delete;

    // ============================================
    // Connection management
    // ============================================

    /**
     * @brief Create gRPC channel and verify connectivity via CheckHealth.
     * @throws std::runtime_error if connection or health check fails.
     */
    void connect();

    /**
     * @brief Gracefully shut down the gRPC channel.
     */
    void disconnect();

    /**
     * @return true if the channel is in READY state.
     */
    bool isConnected() const;

    /**
     * @brief Reconnect: disconnect then connect again.
     */
    void reconnect();

    // ============================================
    // Faza 1: Podstawowe metody
    // ============================================

    /**
     * @brief Get current controller state snapshot.
     */
    astro_mount::ControllerState getState();

    /**
     * @brief Check gRPC service health.
     * @param service  Service name to check (default "mount_controller").
     */
    astro_mount::HealthCheckResponse checkHealth(
        const std::string& service = "mount_controller");

    /**
     * @brief Clear controller error state.
     */
    void clearErrors();

    // ============================================
    // Faza 2: Podstawowe sterowanie montażem
    // ============================================

    /**
     * @brief Slew telescope to equatorial coordinates (RA/Dec J2000).
     */
    void slewToCoordinates(const astro_mount::Coordinates& coords);

    /**
     * @brief Slew telescope to horizontal coordinates (Altitude/Azimuth).
     */
    void slewToHorizontal(const astro_mount::HorizontalCoordinates& coords);

    /**
     * @brief Start tracking an object at the given equatorial coordinates.
     */
    void trackObject(const astro_mount::Coordinates& coords);

    /**
     * @brief Stop all mount motion immediately.
     */
    void stop();

    /**
     * @brief Park the mount at the configured park position.
     */
    void park();

    /**
     * @brief Unpark the mount (wake from park).
     */
    void unpark();

    /**
     * @brief Get current controller configuration.
     */
    astro_mount::Configuration getConfiguration();

    /**
     * @brief Update controller configuration.
     */
    void updateConfiguration(const astro_mount::Configuration& config);

    // ============================================
    // Faza 2: Bootstrap calibration
    // ============================================

    /**
     * @brief Add a bootstrap measurement for initial alignment.
     */
    void addBootstrapMeasurement(const astro_mount::BootstrapMeasurement& measurement);

    /**
     * @brief Run bootstrap calibration with collected measurements.
     */
    astro_mount::BootstrapCalibrationResult runBootstrapCalibration();

    /**
     * @brief Get bootstrap calibration status.
     */
    astro_mount::BootstrapStatus getBootstrapStatus();

    /**
     * @brief Clear all collected bootstrap measurements.
     */
    void clearBootstrapMeasurements();

    // ============================================
    // Faza 3: Axis control
    // ============================================

    /**
     * @brief Control a specific mount axis (position or velocity).
     */
    void controlAxis(const astro_mount::AxisControlRequest& req);

    /**
     * @brief Stop a specific axis.
     */
    void stopAxis(const astro_mount::AxisStopRequest& req);

    /**
     * @brief Emergency stop (all or specific axis).
     */
    void emergencyStop(const astro_mount::EmergencyStopRequest& req);

    // ============================================
    // Faza 3: Guider
    // ============================================

    /**
     * @brief Connect the internal guider.
     */
    void connectGuider(const astro_mount::GuiderConfig& config);

    /**
     * @brief Disconnect the internal guider.
     */
    void disconnectGuider();

    /**
     * @brief Send a guider correction pulse.
     */
    void sendGuiderCorrection(const astro_mount::GuiderCorrection& correction);

    // ============================================
    // Faza 5: Derotator / Field Rotation
    // ============================================

    /**
     * @brief Enable/disable field rotation compensation.
     */
    void enableFieldRotation(const astro_mount::FieldRotationParams& params);

    /**
     * @brief Control field rotation (position, rate, mode).
     */
    void controlFieldRotation(const astro_mount::FieldRotationControlRequest& req);

    /**
     * @brief Get current derotator status.
     */
    astro_mount::DerotatorStatus getDerotatorStatus();

    /**
     * @brief Home the derotator (find zero position).
     */
    void homeDerotator(const astro_mount::DerotatorHomingRequest& req);

private:
    std::string host_;
    int port_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<astro_mount::MountControllerService::Stub> stub_;

    void ensureConnected() const;
};

#endif // MOUNT_GRPC_CLIENT_H
