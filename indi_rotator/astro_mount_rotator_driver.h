#ifndef ASTRO_MOUNT_ROTATOR_DRIVER_H
#define ASTRO_MOUNT_ROTATOR_DRIVER_H

#include <memory>
#include <string>
#include <atomic>
#include <chrono>

#include <indirotator.h>

#include "MountGrpcClient.h"

/**
 * @brief INDI Rotator driver for AstroMountController gRPC service.
 *
 * Faza 5: Osobny INDI Rotator driver dziedziczący po INDI::Rotator.
 * Komunikuje się z kontrolerem montażu przez gRPC (MountGrpcClient).
 *
 * Mapa INDI → gRPC:
 *   ROTATOR_ANGLE       → ControlFieldRotation(FIXED_ANGLE)
 *   ROTATOR_ABORT       → ControlFieldRotation(DISABLED)
 *   ROTATOR_HOME        → HomeDerotator(SEQUENTIAL)
 *   Aktualny kąt         → GetDerotatorStatus().current_angle()
 */
class AstroMountRotatorINDI : public INDI::Rotator
{
public:
    AstroMountRotatorINDI(const char* grpcHost = "localhost", int grpcPort = 50051);
    virtual ~AstroMountRotatorINDI() = default;

    // ============================================
    // INDI::DefaultDevice overrides
    // ============================================

    bool initProperties() override;
    bool updateProperties() override;
    void ISGetProperties(const char* dev) override;
    bool ISNewSwitch(const char* dev, const char* name,
                     ISState* states, char* names[], int n) override;
    bool ISNewNumber(const char* dev, const char* name,
                     double values[], char* names[], int n) override;
    void TimerHit() override;

protected:
    // ============================================
    // INDI::RotatorInterface overrides
    // ============================================

    /**
     * @brief Move rotator to absolute angle.
     * Maps to: ControlFieldRotation(FIXED_ANGLE, target_angle=angle)
     */
    IPState MoveRotator(double angle) override;

    /**
     * @brief Abort rotator motion.
     * Maps to: ControlFieldRotation(DISABLED)
     */
    bool AbortRotator() override;

    /**
     * @brief Go to home position.
     * Maps to: HomeDerotator(SEQUENTIAL)
     */
    IPState HomeRotator() override;

    /**
     * @brief Verify gRPC connection during handshake.
     */
    bool Handshake() override;

private:
    // ============================================
    // Internal state
    // ============================================

    std::unique_ptr<MountGrpcClient> m_grpc;
    std::string m_grpcHost;
    int m_grpcPort;

    // Cached status from GetDerotatorStatus
    astro_mount::DerotatorStatus m_lastStatus;
    std::chrono::steady_clock::time_point m_lastPoll;

    // Polling interval for TimerHit (ms)
    static constexpr int POLL_INTERVAL_MS = 500;

    /// @brief Poll controller for latest derotator status.
    bool pollStatus();

    /// @brief Update INDI rotator properties from cached status.
    void updateRotatorProperties();

    /// @brief Set the rotator angle on INDI properties.
    void setRotatorAngle(double angleDegrees);
};

#endif // ASTRO_MOUNT_ROTATOR_DRIVER_H
