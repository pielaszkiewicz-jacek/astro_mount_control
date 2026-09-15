#ifndef ASTRO_MOUNT_DRIVER_H
#define ASTRO_MOUNT_DRIVER_H

#include <memory>
#include <string>
#include <atomic>
#include <chrono>

#include <inditelescope.h>
#include <indiguiderinterface.h>

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
class AstroMountINDI : public INDI::Telescope, public INDI::GuiderInterface
{
public:
    AstroMountINDI(const char* grpcHost = "localhost", int grpcPort = 50051);
    virtual ~AstroMountINDI() = default;

    // ============================================
    // INDI::Telescope overrides
    // ============================================

    // Pure virtual in INDI::DefaultDevice — must be provided.
    const char *getDefaultName() override;
    void ISGetProperties(const char* dev) override;
    bool initProperties() override;
    bool updateProperties() override;
    bool ISNewNumber(const char* dev, const char* name,
                     double values[], char* names[], int n) override;
    bool ISNewSwitch(const char* dev, const char* name,
                     ISState* states, char* names[], int n) override;
    bool ISNewText(const char* dev, const char* name,
                   char* texts[], char* names[], int n) override;
    void TimerHit() override;

    // The base INDI::Telescope::Connect() does not know about the gRPC link to
    // the mount controller — override it so toggling CONNECT in the INDI client
    // actually establishes/tears down the gRPC channel.
    bool Connect() override;
    bool Disconnect() override;

    // The base class would attempt a serial/TCP handshake with a "telescope"
    // at 127.0.0.1:7624; our real handshake is the gRPC CheckHealth done in
    // Connect(), so report the gRPC channel state instead.
    bool Handshake() override;

    // ============================================
    // INDI::Telescope movement methods
    // ============================================

    bool Goto(double ra, double dec) override;
    bool GotoRaDec(double ra, double dec);
    bool Sync(double ra, double dec) override;
    bool MoveNS(INDI_DIR_NS dir, TelescopeMotionCommand command) override;
    bool MoveWE(INDI_DIR_WE dir, TelescopeMotionCommand command) override;
    bool Abort() override;
    bool Park() override;
    bool UnPark() override;
    bool SetCurrentPark() override;
    bool SetDefaultPark() override;
    bool Flip(double ra, double dec) override;
    bool updateLocation(double latitude, double longitude, double elevation) override;
    bool updateTime(ln_date* utc, double utc_offset) override;
    bool ReadScopeStatus() override;

    // ============================================
    // Tracking and slew-rate control
    // ============================================

    bool SetTrackMode(uint8_t mode) override;
    bool SetTrackRate(double raRate, double deRate) override;
    bool SetTrackEnabled(bool enabled) override;
    bool SetSlewRate(int index) override;

    // ============================================
    // GuiderInterface — pulse guiding
    // ============================================

    IPState GuideNorth(uint32_t ms) override;
    IPState GuideSouth(uint32_t ms) override;
    IPState GuideEast(uint32_t ms) override;
    IPState GuideWest(uint32_t ms) override;

protected:
    // ============================================
    // Additional INDI properties (Faza 2+)
    // ============================================

    // Bootstrap calibration switch
    ISwitchVectorProperty BootstrapCalibrationSP{};
    ISwitch BootstrapCalibrationS[3]{}; // RUN, CLEAR, STATUS

    // Bootstrap status text
    ITextVectorProperty BootstrapStatusTP{};
    IText BootstrapStatusT[2]{}; // STATUS, MEASUREMENTS

    // J2000 equatorial coordinates (custom)
    INumberVectorProperty EquatorialCoordsJ2000NP{};
    INumber EquatorialCoordsJ2000N[2]{}; // RA_J2000, DEC_J2000

    // ============================================
    // Faza 3: TPOINT status (read-only text)
    // ============================================

    ITextVectorProperty TPointStatusTP{};
    IText TPointStatusT[3]{}; // COEFFICIENTS, CHI2, CALIBRATED

    // ============================================
    // Faza 3: Environmental conditions
    // ============================================

    INumberVectorProperty EnvironmentNP{};
    INumber EnvironmentN[3]{}; // TEMPERATURE, PRESSURE, HUMIDITY

    // Horizontal coordinates (Alt/Az) — custom, RW.
    INumberVectorProperty HorizontalCoordNP{};
    INumber HorizontalCoordN[2]{}; // ALT, AZ

    // ============================================
    // Controller status (read-only) — surfaces ERROR state
    // ============================================

    ITextVectorProperty MountStatusTP{};
    IText MountStatusT[2]{}; // STATE, ERROR

    // TPOINT calibration control (RUN/CLEAR/STATUS)
    ISwitchVectorProperty TPointCalibrationSP{};
    ISwitch TPointCalibrationS[3]{}; // RUN, CLEAR, STATUS

    // Encoder control (ENABLE/DISABLE)
    ISwitchVectorProperty EncodersSP{};
    ISwitch EncodersS[2]{}; // ENABLE, DISABLE

    // Homing — set reference position from current axes
    ISwitchVectorProperty HomeSP{};
    ISwitch HomeS[1]{}; // SET_REFERENCE

    // Emergency stop
    ISwitchVectorProperty EmergencyStopSP{};
    ISwitch EmergencyStopS[1]{}; // TRIGGER

    // Controller state save / load
    ISwitchVectorProperty ControllerStateSP{};
    ISwitch ControllerStateS[2]{}; // SAVE, LOAD

    // Automatic bootstrap
    ISwitchVectorProperty AutoBootstrapSP{};
    ISwitch AutoBootstrapS[2]{}; // RUN, STATUS

    // Gamepad manual-control loop
    ISwitchVectorProperty GamepadSP{};
    ISwitch GamepadS[2]{}; // START, STOP

    // LX200 serial protocol server
    ISwitchVectorProperty Lx200SP{};
    ISwitch Lx200S[2]{}; // START, STOP

    // ============================================
    // Connection configuration (UI) — GRPC host/port + TLS
    // ============================================

    // Text: GRPC_HOST / GRPC_PORT (configured from the INDI client)
    ITextVectorProperty ConnectionTP{};
    IText ConnectionT[2]{}; // HOST, PORT

    // Switch: TLS ENABLE / DISABLE
    ISwitchVectorProperty ConnectionSslSP{};
    ISwitch ConnectionSslS[2]{}; // ENABLE, DISABLE

    // Read-only connection status
    ITextVectorProperty ConnectionStatusTP{};
    IText ConnectionStatusT[1]{}; // STATUS

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

    // Slew rate selected via INDI SlewRateSP (GUIDE/CENTERING/FIND/MAX).
    int m_slewRateIndex{3};               // 0..3 (SLEW_MAX by default)
    double m_slewRateDegPerSec{1.0};

    // Tracking mode / custom rates. Custom rates are sent to the controller
    // as Coordinates.custom_track_rate_ra/dec by SetTrackEnabled().
    uint8_t m_trackMode{TRACK_SIDEREAL};
    double m_customTrackRaArcsecPerSec{15.041067};
    double m_customTrackDecArcsecPerSec{0.0};

    /// @brief Send a guider correction (arcsec) via SendGuiderCorrection.
    IPState guideCorrection(double raCorrectionArcsec, double decCorrectionArcsec);

    /// @brief Build and submit a bootstrap measurement with distinct
    /// observed/expected coordinates (used by Sync and bootstrap RUN).
    bool addSyncMeasurement(double raHours, double decDegrees);

    // gRPC endpoint, configurable from the INDI UI (ConnectionTP/ConnectionSslSP).
    std::string m_grpcHost{"localhost"};
    int m_grpcPort{50051};
    bool m_grpcUseSsl{false};

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

    /// @brief (Re)create the gRPC client from the configured host/port/ssl
    /// (called on Connect and after the UI config is changed).
    void applyConnectionConfig();

    /// @brief Push the current connection state into the read-only status text.
    void updateConnectionStatus();
};

#endif // ASTRO_MOUNT_DRIVER_H
