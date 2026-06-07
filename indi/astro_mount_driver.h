#ifndef ASTRO_MOUNT_DRIVER_H
#define ASTRO_MOUNT_DRIVER_H

#include <memory>
#include <string>
#include <atomic>
#include <chrono>

#include <inditelescope.h>

#include "MountGrpcClient.h"
#include "IndiPropertyMapper.h"

/**
 * @brief INDI Telescope driver for AstroMountController gRPC service.
 *
 * Connects to the mount controller via gRPC and implements the INDI::Telescope
 * interface. Supports:
 *   - Goto (RA/Dec slew)
 *   - Abort motion
 *   - Park / Unpark
 *   - ReadScopeStatus (periodic position update)
 *   - Sync (bootstrap alignment)
 *
 * Faza 2b: Podstawowe sterowanie montażem.
 * Kolejne fazy dodadzą: MoveNS/MoveWE, PulseGuide, TrackingRate, itd.
 */
class AstroMountINDI : public INDI::Telescope
{
public:
    AstroMountINDI(const char* grpcHost = "localhost", int grpcPort = 50051);
    virtual ~AstroMountINDI() = default;

    // ============================================
    // INDI::Telescope overrides
    // ============================================

    bool initProperties() override;
    bool updateProperties() override;
    bool ISNewNumber(const char* dev, const char* name,
                     double values[], const char* names[], int n) override;
    bool ISNewSwitch(const char* dev, const char* name,
                     ISState* states, char* names[], int n) override;
    bool ISNewText(const char* dev, const char* name,
                   char* texts[], char* names[], int n) override;
    void TimerHit() override;

    // ============================================
    // INDI::Telescope movement methods
    // ============================================

    bool Goto(double ra, double dec) override;
    bool GotoRaDec(double ra, double dec) override;
    bool Sync(double ra, double dec) override;
    bool MoveNS(INDI_DIR_NS dir, TelescopeMotionCommand command) override;
    bool MoveWE(INDI_DIR_WE dir, TelescopeMotionCommand command) override;
    bool Abort() override;
    bool Park() override;
    bool Unpark() override;
    bool SetCurrentPark() override;
    bool SetDefaultPark() override;
    bool UpdateLocation(double latitude, double longitude, double elevation) override;
    bool ReadScopeStatus() override;

protected:
    // ============================================
    // Additional INDI properties (Faza 2+)
    // ============================================

    // Bootstrap calibration switch
    ISwitchVectorProperty BootstrapCalibrationSP;
    ISwitch BootstrapCalibrationS[3]; // RUN, CLEAR, STATUS

    // Bootstrap status text
    ITextVectorProperty BootstrapStatusTP;
    IText BootstrapStatusT[2]; // STATUS, MEASUREMENTS

    // J2000 equatorial coordinates (custom)
    INumberVectorProperty EquatorialCoordsJ2000NP;
    INumber EquatorialCoordsJ2000N[2]; // RA_J2000, DEC_J2000

    // ============================================
    // Faza 3: TPOINT status (read-only text)
    // ============================================

    ITextVectorProperty TPointStatusTP;
    IText TPointStatusT[3]; // COEFFICIENTS, CHI2, CALIBRATED

    // ============================================
    // Faza 3: Environmental conditions
    // ============================================

    INumberVectorProperty EnvironmentNP;
    INumber EnvironmentN[3]; // TEMPERATURE, PRESSURE, HUMIDITY

private:
    // ============================================
    // Internal state
    // ============================================

    std::unique_ptr<MountGrpcClient> m_grpc;
    std::unique_ptr<IndiPropertyMapper> m_mapper;
    astro_mount::ControllerState m_lastState;
    std::chrono::steady_clock::time_point m_lastPoll;
    bool m_isParked;
    double m_targetRA;
    double m_targetDec;

    // Cache for polled state
    std::mutex m_stateMutex;

    // ============================================
    // Internal helpers
    // ============================================

    /// @brief Poll the controller for fresh state (called from TimerHit).
    bool pollController();

    /// @brief Update INDI properties from cached controller state.
    void updateIndiProperties();

    /// @brief Set INDI property values from gRPC state.
    void setEquatorialCoords(double raHours, double decDegrees);

    /// @brief Convert RA/Dec to mount position and slew.
    bool performGoto(double ra, double dec);
};

#endif // ASTRO_MOUNT_DRIVER_H
