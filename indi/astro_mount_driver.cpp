#include "astro_mount_driver.h"
#include <libnova/julian_day.h>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <thread>

// We declare the driver loader function expected by indiserver
static std::unique_ptr<AstroMountINDI> s_driver;

void ISGetProperties(const char* dev)
{
    if (!s_driver)
    {
        // Read GRPC_HOST from environment if set
        const char* grpcHost = std::getenv("GRPC_HOST");
        if (!grpcHost) grpcHost = "localhost";

        const char* grpcPortStr = std::getenv("GRPC_PORT");
        int grpcPort = 50051;
        if (grpcPortStr) grpcPort = std::stoi(grpcPortStr);

        s_driver = std::make_unique<AstroMountINDI>(grpcHost, grpcPort);
    }
    s_driver->ISGetProperties(dev);
}

void ISNewSwitch(const char* dev, const char* name,
                 ISState* states, char* names[], int n)
{
    if (s_driver) s_driver->ISNewSwitch(dev, name, states, names, n);
}

void ISNewNumber(const char* dev, const char* name,
                 double values[], char* names[], int n)
{
    if (s_driver) s_driver->ISNewNumber(dev, name, values, names, n);
}

void ISNewText(const char* dev, const char* name,
               char* texts[], char* names[], int n)
{
    if (s_driver) s_driver->ISNewText(dev, name, texts, names, n);
}

void ISNewBLOB(const char* dev, const char* name,
               int sizes[], int blobsizes[], char* blobs[],
               char* formats[], char* names[], int n)
{
    if (s_driver) s_driver->ISNewBLOB(dev, name, sizes, blobsizes,
                                        blobs, formats, names, n);
}

void ISSnoopDevice(XMLEle* root)
{
    if (s_driver) s_driver->ISSnoopDevice(root);
}

// ============================================
// AstroMountINDI implementation
// ============================================

AstroMountINDI::AstroMountINDI(const char* grpcHost, int grpcPort)
    : INDI::Telescope()
    , INDI::GuiderInterface(this)
    , m_isParked(false)
    , m_targetRA(0)
    , m_targetDec(0)
    , m_grpcHost(grpcHost ? grpcHost : "localhost")
    , m_grpcPort(grpcPort > 0 ? grpcPort : 50051)
{
    setVersion(2, 0);
    // The transport to the controller is gRPC, not a serial/TCP telescope
    // connection. Disable the base class connection plugins so Connect() does
    // not attempt a TCP handshake with 127.0.0.1:7624.
    setTelescopeConnection(INDI::Telescope::CONNECTION_NONE);
    // INDI 2.x: telescope type is expressed through capability flags.
    SetTelescopeCapability(INDI::Telescope::TELESCOPE_CAN_GOTO |
                               INDI::Telescope::TELESCOPE_CAN_SYNC |
                               INDI::Telescope::TELESCOPE_CAN_PARK |
                               INDI::Telescope::TELESCOPE_CAN_ABORT |
                               INDI::Telescope::TELESCOPE_HAS_TRACK_MODE |
                               INDI::Telescope::TELESCOPE_CAN_CONTROL_TRACK,
                           4); // GUIDE / CENTERING / FIND / MAX slew rates

    // Tracking modes advertised to INDI clients.
    AddTrackMode("TRACK_SIDEREAL", "Sidereal", true);
    AddTrackMode("TRACK_SOLAR", "Solar");
    AddTrackMode("TRACK_LUNAR", "Lunar");
    AddTrackMode("TRACK_CUSTOM", "Custom");

    m_grpc = std::make_unique<MountGrpcClient>(m_grpcHost, m_grpcPort, m_grpcUseSsl);
    m_mapper = std::make_unique<IndiPropertyMapper>();

    m_lastPoll = std::chrono::steady_clock::now();
}

bool AstroMountINDI::Connect()
{
    // Establish the gRPC channel to the mount controller. The transport is
    // gRPC only (CONNECTION_NONE), so the base class connection plugins must
    // not run — call setConnected() directly instead of the base Connect(),
    // which would report "No active connection defined".
    applyConnectionConfig();
    LOGF_DEBUG("Connect: attempting gRPC connection to %s:%d (ssl=%d)",
               m_grpcHost.c_str(), m_grpcPort, m_grpcUseSsl);
    try
    {
        m_grpc->connect();
    }
    catch (const std::exception& e)
    {
        LOGF_ERROR("Failed to connect to mount controller: %s", e.what());
        return false;
    }

    setConnected(true, IPS_OK, nullptr);

    // Start the periodic poll timer. Without this initial SetTimer the
    // framework never invokes TimerHit(), so KStars would keep showing the
    // position captured once during updateProperties().
    SetTimer(getCurrentPollingPeriod());

    LOG_INFO("Connected to mount controller (gRPC)");
    updateConnectionStatus();
    return true;
}

bool AstroMountINDI::Disconnect()
{
    bool ok = true;
    try
    {
        m_grpc->disconnect();
    }
    catch (const std::exception& e)
    {
        LOGF_ERROR("Failed to disconnect gRPC: %s", e.what());
        ok = false;
    }

    setConnected(false, IPS_OK, nullptr);

    LOG_INFO("Disconnected from mount controller (gRPC)");
    updateConnectionStatus();
    return ok;
}

bool AstroMountINDI::Handshake()
{
    // The real handshake (gRPC CheckHealth) already happened in Connect().
    // With CONNECTION_NONE there is no serial/TCP telescope to handshake with.
    return m_grpc && m_grpc->isConnected();
}

const char *AstroMountINDI::getDefaultName()
{
    return "AstroMount";
}

void AstroMountINDI::ISGetProperties(const char* dev)
{
    INDI::Telescope::ISGetProperties(dev);

    // INDI 2.x defines only a minimum connection set in ISGetProperties();
    // updateProperties() runs on connect/disconnect. The gRPC endpoint config
    // (host/port/TLS) must be visible BEFORE connecting, so define it here as
    // well. Re-defining it later is harmless.
    defineProperty(&ConnectionTP);
    defineProperty(&ConnectionSslSP);
    defineProperty(&ConnectionStatusTP);
    updateConnectionStatus();
}

bool AstroMountINDI::initProperties()
{
    INDI::Telescope::initProperties();

    // Pulse-guiding interface (defines GuideNSNP / GuideWENP).
    INDI::GuiderInterface::initProperties("Guide");

    // Debug control — adds the DEBUG switch so LOG_DEBUG/LOGF_DEBUG output can
    // be toggled from the INDI client at runtime.
    addDebugControl();

    // Primary axis: EQUATORIAL_EOD_COORD (RA/Dec JNow)
    // We add J2000 as additional coordinate display
    IUFillNumber(&EquatorialCoordsJ2000N[0], "RA_J2000", "RA J2000", "%10.6f",
                 0, 24, 0.001, 0);
    IUFillNumber(&EquatorialCoordsJ2000N[1], "DEC_J2000", "DEC J2000", "%10.6f",
                 -90, 90, 0.001, 0);
    IUFillNumberVector(&EquatorialCoordsJ2000NP, EquatorialCoordsJ2000N, 2,
                       getDeviceName(), "EQUATORIAL_J2000", "Eq J2000",
                       MAIN_CONTROL_TAB, IP_RW, 60, IPS_IDLE);

    // Bootstrap Calibration switch
    IUFillSwitch(&BootstrapCalibrationS[0], "RUN", "Run Bootstrap", ISS_OFF);
    IUFillSwitch(&BootstrapCalibrationS[1], "CLEAR", "Clear Measurements", ISS_OFF);
    IUFillSwitch(&BootstrapCalibrationS[2], "STATUS", "Show Status", ISS_OFF);
    IUFillSwitchVector(&BootstrapCalibrationSP, BootstrapCalibrationS, 3,
                       getDeviceName(), "BOOTSTRAP_CALIBRATION",
                       "Bootstrap Calibration", MAIN_CONTROL_TAB, IP_RW,
                       ISR_ATMOST1, 60, IPS_IDLE);

    // Bootstrap Status text
    IUFillText(&BootstrapStatusT[0], "STATUS", "Status", "Not calibrated");
    IUFillText(&BootstrapStatusT[1], "MEASUREMENTS", "Measurements", "0");
    IUFillTextVector(&BootstrapStatusTP, BootstrapStatusT, 2,
                     getDeviceName(), "BOOTSTRAP_STATUS",
                     "Bootstrap Status", MAIN_CONTROL_TAB, IP_RO, 60, IPS_IDLE);

    // ============================================
    // Faza 3: TPOINT Status (read-only text)
    // ============================================
    IUFillText(&TPointStatusT[0], "COEFFICIENTS", "Coefficients", "");
    IUFillText(&TPointStatusT[1], "CHI2", "Chi-Squared", "");
    IUFillText(&TPointStatusT[2], "CALIBRATED", "Calibrated", "No");
    IUFillTextVector(&TPointStatusTP, TPointStatusT, 3,
                     getDeviceName(), "TPOINT_STATUS",
                     "TPOINT Calibration", MAIN_CONTROL_TAB, IP_RO, 60, IPS_IDLE);

    // ============================================
    // Faza 3: Environmental conditions
    // ============================================
    IUFillNumber(&EnvironmentN[0], "TEMPERATURE", "Temperature (C)", "%.2f",
                  -50, 60, 1, 0);
    IUFillNumber(&EnvironmentN[1], "PRESSURE", "Pressure (hPa)", "%.1f",
                  0, 1100, 1, 0);
    IUFillNumber(&EnvironmentN[2], "HUMIDITY", "Humidity (%)", "%.1f",
                  0, 100, 1, 0);
    IUFillNumberVector(&EnvironmentNP, EnvironmentN, 3,
                       getDeviceName(), "ENVIRONMENT",
                       "Environment", MAIN_CONTROL_TAB, IP_RO, 60, IPS_IDLE);

    // Horizontal (Alt/Az) coordinates — RW, lets clients slew in horizontal frame.
    IUFillNumber(&HorizontalCoordN[0], "ALT", "Altitude (deg)", "%10.6f",
                 -90, 90, 0.001, 0);
    IUFillNumber(&HorizontalCoordN[1], "AZ", "Azimuth (deg)", "%10.6f",
                 0, 360, 0.001, 0);
    IUFillNumberVector(&HorizontalCoordNP, HorizontalCoordN, 2,
                       getDeviceName(), "HORIZONTAL_COORD",
                       "Horizontal Coord", MAIN_CONTROL_TAB, IP_RW, 60, IPS_IDLE);

    // Controller status (read-only) — surfaces the controller ERROR state.
    IUFillText(&MountStatusT[0], "STATE", "State", "Unknown");
    IUFillText(&MountStatusT[1], "ERROR", "Error", "");
    IUFillTextVector(&MountStatusTP, MountStatusT, 2,
                     getDeviceName(), "MOUNT_STATUS",
                     "Mount Status", MAIN_CONTROL_TAB, IP_RO, 60, IPS_IDLE);

    // TPOINT calibration control
    IUFillSwitch(&TPointCalibrationS[0], "RUN", "Run TPOINT", ISS_OFF);
    IUFillSwitch(&TPointCalibrationS[1], "CLEAR", "Clear Measurements", ISS_OFF);
    IUFillSwitch(&TPointCalibrationS[2], "STATUS", "Show Status", ISS_OFF);
    IUFillSwitchVector(&TPointCalibrationSP, TPointCalibrationS, 3,
                       getDeviceName(), "TPOINT_CALIBRATION",
                       "TPOINT Calibration", MAIN_CONTROL_TAB, IP_RW,
                       ISR_ATMOST1, 60, IPS_IDLE);

    // Encoder control
    IUFillSwitch(&EncodersS[0], "ENABLE", "Enable", ISS_OFF);
    IUFillSwitch(&EncodersS[1], "DISABLE", "Disable", ISS_ON);
    IUFillSwitchVector(&EncodersSP, EncodersS, 2,
                       getDeviceName(), "ENCODERS",
                       "Encoders", MAIN_CONTROL_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);

    // Homing — set reference position from current telescope axes
    IUFillSwitch(&HomeS[0], "SET_REFERENCE", "Set Reference", ISS_OFF);
    IUFillSwitchVector(&HomeSP, HomeS, 1,
                       getDeviceName(), "HOME",
                       "Home", MAIN_CONTROL_TAB, IP_RW, ISR_ATMOST1, 60, IPS_IDLE);

    // Emergency stop
    IUFillSwitch(&EmergencyStopS[0], "TRIGGER", "Emergency Stop", ISS_OFF);
    IUFillSwitchVector(&EmergencyStopSP, EmergencyStopS, 1,
                       getDeviceName(), "EMERGENCY_STOP",
                       "Emergency Stop", MAIN_CONTROL_TAB, IP_RW, ISR_ATMOST1, 60, IPS_IDLE);

    // Controller state save / load
    IUFillSwitch(&ControllerStateS[0], "SAVE", "Save State", ISS_OFF);
    IUFillSwitch(&ControllerStateS[1], "LOAD", "Load State", ISS_OFF);
    IUFillSwitchVector(&ControllerStateSP, ControllerStateS, 2,
                       getDeviceName(), "CONTROLLER_STATE",
                       "Controller State", MAIN_CONTROL_TAB, IP_RW, ISR_ATMOST1, 60, IPS_IDLE);

    // Automatic bootstrap
    IUFillSwitch(&AutoBootstrapS[0], "RUN", "Run Auto-Bootstrap", ISS_OFF);
    IUFillSwitch(&AutoBootstrapS[1], "STATUS", "Show Status", ISS_OFF);
    IUFillSwitchVector(&AutoBootstrapSP, AutoBootstrapS, 2,
                       getDeviceName(), "AUTO_BOOTSTRAP",
                       "Auto Bootstrap", MAIN_CONTROL_TAB, IP_RW, ISR_ATMOST1, 60, IPS_IDLE);

    // Gamepad manual-control loop
    IUFillSwitch(&GamepadS[0], "START", "Start", ISS_OFF);
    IUFillSwitch(&GamepadS[1], "STOP", "Stop", ISS_OFF);
    IUFillSwitchVector(&GamepadSP, GamepadS, 2,
                       getDeviceName(), "GAMEPAD",
                       "Gamepad", MAIN_CONTROL_TAB, IP_RW, ISR_ATMOST1, 60, IPS_IDLE);

    // LX200 serial protocol server
    IUFillSwitch(&Lx200S[0], "START", "Start", ISS_OFF);
    IUFillSwitch(&Lx200S[1], "STOP", "Stop", ISS_OFF);
    IUFillSwitchVector(&Lx200SP, Lx200S, 2,
                       getDeviceName(), "LX200",
                       "LX200 Server", MAIN_CONTROL_TAB, IP_RW, ISR_ATMOST1, 60, IPS_IDLE);

    // ============================================
    // Connection configuration (UI) — gRPC endpoint
    // ============================================
    // Seed the UI fields with the actual configured endpoint (from GRPC_HOST /
    // GRPC_PORT env or constructor defaults).
    char portBuf[16];
    snprintf(portBuf, sizeof(portBuf), "%d", m_grpcPort);
    IUFillText(&ConnectionT[0], "HOST", "gRPC Host", m_grpcHost.c_str());
    IUFillText(&ConnectionT[1], "PORT", "gRPC Port", portBuf);
    IUFillTextVector(&ConnectionTP, ConnectionT, 2,
                     getDeviceName(), "GRPC_CONNECTION",
                     "Controller Connection", "Connection", IP_RW, 60, IPS_IDLE);

    IUFillSwitch(&ConnectionSslS[0], "ENABLE", "Enabled", ISS_OFF);
    IUFillSwitch(&ConnectionSslS[1], "DISABLE", "Disabled", ISS_ON);
    IUFillSwitchVector(&ConnectionSslSP, ConnectionSslS, 2,
                       getDeviceName(), "GRPC_TLS", "gRPC TLS",
                       "Connection", IP_RW, ISR_1OFMANY, 60, IPS_IDLE);

    IUFillText(&ConnectionStatusT[0], "STATUS", "Status", "Not connected");
    IUFillTextVector(&ConnectionStatusTP, ConnectionStatusT, 1,
                     getDeviceName(), "GRPC_CONNECTION_STATUS",
                     "Connection Status", "Connection", IP_RO, 60, IPS_IDLE);

    // Set default park position (will be updated from config).
    // Equatorial mount parks in HA/Dec at NCP (HA=0, Dec=90).
    SetParkDataType(INDI::Telescope::PARK_HA_DEC);
    SetParkPosition(0.0, 90.0);

    return true;
}

bool AstroMountINDI::updateProperties()
{
    INDI::Telescope::updateProperties();
    INDI::GuiderInterface::updateProperties();

    // Connection configuration (GRPC_CONNECTION / GRPC_TLS / GRPC_CONNECTION_STATUS)
    // is always defined so the endpoint can be configured before connecting.
    defineProperty(&ConnectionTP);
    defineProperty(&ConnectionSslSP);
    defineProperty(&ConnectionStatusTP);
    updateConnectionStatus();

    if (isConnected())
    {
        defineProperty(&EquatorialCoordsJ2000NP);
        defineProperty(&BootstrapCalibrationSP);
        defineProperty(&BootstrapStatusTP);
        defineProperty(&TPointStatusTP);
        defineProperty(&EnvironmentNP);
        defineProperty(&HorizontalCoordNP);
        defineProperty(&MountStatusTP);
        defineProperty(&TPointCalibrationSP);
        defineProperty(&EncodersSP);
        defineProperty(&HomeSP);
        defineProperty(&EmergencyStopSP);
        defineProperty(&ControllerStateSP);
        defineProperty(&AutoBootstrapSP);
        defineProperty(&GamepadSP);
        defineProperty(&Lx200SP);

        // Poll initial state
        pollController();
        updateIndiProperties();
    }
    else
    {
        deleteProperty(EquatorialCoordsJ2000NP.name);
        deleteProperty(BootstrapCalibrationSP.name);
        deleteProperty(BootstrapStatusTP.name);
        deleteProperty(TPointStatusTP.name);
        deleteProperty(EnvironmentNP.name);
        deleteProperty(HorizontalCoordNP.name);
        deleteProperty(MountStatusTP.name);
        deleteProperty(TPointCalibrationSP.name);
        deleteProperty(EncodersSP.name);
        deleteProperty(HomeSP.name);
        deleteProperty(EmergencyStopSP.name);
        deleteProperty(ControllerStateSP.name);
        deleteProperty(AutoBootstrapSP.name);
        deleteProperty(GamepadSP.name);
        deleteProperty(Lx200SP.name);
    }

    return true;
}

// ============================================
// INDI property handlers
// ============================================

bool AstroMountINDI::ISNewNumber(const char* dev, const char* name,
                                  double values[], char* names[], int n)
{
    if (dev && !strcmp(dev, getDeviceName()))
    {
        LOGF_DEBUG("ISNewNumber(%s, n=%d)", name, n);

        // Delegate guide-pulse properties (GuideNSNP / GuideWENP) to the
        // GuiderInterface, which maps them onto GuideNorth/South/East/West.
        if (INDI::GuiderInterface::processNumber(dev, name, values, names, n))
            return true;

        // Handle J2000 coordinate input — precess to JNow and slew.
        if (!strcmp(name, EquatorialCoordsJ2000NP.name))
        {
            IUUpdateNumber(&EquatorialCoordsJ2000NP, values, names, n);
            double raNow = EquatorialCoordsJ2000N[0].value;
            double decNow = EquatorialCoordsJ2000N[1].value;
            m_mapper->j2000ToJnow(EquatorialCoordsJ2000N[0].value,
                                  EquatorialCoordsJ2000N[1].value, raNow, decNow);
            if (GotoRaDec(raNow, decNow))
                EquatorialCoordsJ2000NP.s = IPS_OK;
            else
                EquatorialCoordsJ2000NP.s = IPS_ALERT;
            IDSetNumber(&EquatorialCoordsJ2000NP, nullptr);
            return true;
        }

        // Handle horizontal (Alt/Az) coordinate input
        if (!strcmp(name, HorizontalCoordNP.name))
        {
            IUUpdateNumber(&HorizontalCoordNP, values, names, n);
            try
            {
                astro_mount::HorizontalCoordinates coords;
                coords.set_altitude(HorizontalCoordN[0].value);
                coords.set_azimuth(HorizontalCoordN[1].value);
                m_grpc->slewToHorizontal(coords);
                TrackState = SCOPE_SLEWING;
                HorizontalCoordNP.s = IPS_OK;
                LOGF_INFO("Slewing to Alt=%.4f, Az=%.4f",
                          coords.altitude(), coords.azimuth());
            }
            catch (const std::exception& e)
            {
                HorizontalCoordNP.s = IPS_ALERT;
                LOGF_ERROR("Horizontal slew failed: %s", e.what());
            }
            IDSetNumber(&HorizontalCoordNP, nullptr);
            return true;
        }
    }

    return INDI::Telescope::ISNewNumber(dev, name, values, names, n);
}

bool AstroMountINDI::ISNewSwitch(const char* dev, const char* name,
                                  ISState* states, char* names[], int n)
{
    if (dev && !strcmp(dev, getDeviceName()))
    {
        LOGF_DEBUG("ISNewSwitch(%s, n=%d)", name, n);

        // gRPC TLS toggle (Enabled/Disabled)
        if (!strcmp(name, ConnectionSslSP.name))
        {
            IUUpdateSwitch(&ConnectionSslSP, states, names, n);
            m_grpcUseSsl = (ConnectionSslS[0].s == ISS_ON);
            ConnectionSslSP.s = IPS_OK;
            IDSetSwitch(&ConnectionSslSP, nullptr);
            LOGF_INFO("gRPC TLS %s", m_grpcUseSsl ? "enabled" : "disabled");
            return true;
        }

        // Bootstrap Calibration switch
        if (!strcmp(name, BootstrapCalibrationSP.name))
        {
            int runIndex = IUFindOnSwitchIndex(&BootstrapCalibrationSP);
            IUResetSwitch(&BootstrapCalibrationSP);

            if (runIndex == 0) // RUN
            {
                // Add current position as bootstrap measurement and calibrate.
                try
                {
                    // Use the currently displayed equatorial coordinates as the
                    // catalog target for the measurement. The J2000 display now
                    // really holds J2000; addSyncMeasurement expects JNow, so
                    // precess before submitting.
                    double raJ2000 = EquatorialCoordsJ2000N[0].value;
                    double decJ2000 = EquatorialCoordsJ2000N[1].value;
                    double raNow = 0.0, decNow = 0.0;
                    m_mapper->j2000ToJnow(raJ2000, decJ2000, raNow, decNow);

                    if (!addSyncMeasurement(raNow, decNow))
                    {
                        BootstrapCalibrationSP.s = IPS_ALERT;
                    }
                    else
                    {
                        auto result = m_grpc->runBootstrapCalibration();
                        if (result.success())
                        {
                            LOGF_INFO("Bootstrap calibration successful. "
                                     "Error: %.2f arcsec",
                                     result.alignment_error_arcsec());
                            BootstrapCalibrationSP.s = IPS_OK;
                        }
                        else
                        {
                            LOGF_ERROR("Bootstrap calibration failed: %s",
                                      result.error_message().c_str());
                            BootstrapCalibrationSP.s = IPS_ALERT;
                        }
                    }

                    // Update status text
                    auto status = m_grpc->getBootstrapStatus();
                    IUSaveText(&BootstrapStatusT[0],
                               status.calibrated() ? "Calibrated" : "Not calibrated");
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%d", status.measurement_count());
                    IUSaveText(&BootstrapStatusT[1], buf);
                    IDSetText(&BootstrapStatusTP, nullptr);
                }
                catch (const std::exception& e)
                {
                    LOGF_ERROR("Bootstrap calibration error: %s", e.what());
                    BootstrapCalibrationSP.s = IPS_ALERT;
                }
            }
            else if (runIndex == 1) // CLEAR
            {
                try
                {
                    m_grpc->clearBootstrapMeasurements();
                    IUSaveText(&BootstrapStatusT[0], "Cleared");
                    IUSaveText(&BootstrapStatusT[1], "0");
                    IDSetText(&BootstrapStatusTP, nullptr);
                    BootstrapCalibrationSP.s = IPS_IDLE;
                }
                catch (const std::exception& e)
                {
                    LOGF_ERROR("Failed to clear measurements: %s", e.what());
                    BootstrapCalibrationSP.s = IPS_ALERT;
                }
            }
            else if (runIndex == 2) // STATUS
            {
                try
                {
                    auto status = m_grpc->getBootstrapStatus();
                    IUSaveText(&BootstrapStatusT[0],
                               status.calibrated() ? "Calibrated" : "Not calibrated");
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%d", status.measurement_count());
                    IUSaveText(&BootstrapStatusT[1], buf);
                    IDSetText(&BootstrapStatusTP, nullptr);
                    BootstrapCalibrationSP.s = IPS_OK;
                }
                catch (const std::exception& e)
                {
                    LOGF_ERROR("Failed to get status: %s", e.what());
                    BootstrapCalibrationSP.s = IPS_ALERT;
                }
            }

            IDSetSwitch(&BootstrapCalibrationSP, nullptr);
            return true;
        }

        // TPOINT calibration control (RUN / CLEAR / STATUS)
        if (!strcmp(name, TPointCalibrationSP.name))
        {
            int idx = IUFindOnSwitchIndex(&TPointCalibrationSP);
            IUResetSwitch(&TPointCalibrationSP);
            try
            {
                if (idx == 0) // RUN
                {
                    auto state = m_grpc->getState();
                    double lst = m_mapper->computeLst();
                    double curRa = 0, curDec = 0;
                    if (!m_mapper->toIndiRaDec(state, lst, curRa, curDec))
                    {
                        TPointCalibrationSP.s = IPS_ALERT;
                    }
                    else
                    {
                        astro_mount::Coordinates observed;
                        observed.set_ra(curRa);
                        observed.set_dec(curDec);
                        observed.set_epoch(0.0);  // JNow (of date)
                        // EquatorialCoordsJ2000N now really holds J2000, but
                        // toGrpcCoordinates expects JNow, so precess first.
                        double raJ2000 = EquatorialCoordsJ2000N[0].value;
                        double decJ2000 = EquatorialCoordsJ2000N[1].value;
                        double raNow = 0.0, decNow = 0.0;
                        m_mapper->j2000ToJnow(raJ2000, decJ2000, raNow, decNow);
                        auto expected = m_mapper->toGrpcCoordinates(raNow, decNow);

                        astro_mount::Measurement m;
                        *m.mutable_observed() = observed;
                        *m.mutable_expected() = expected;
                        // AddTPointMeasurement treats mount_position as
                        // telescope degrees (axis1 is divided by 15 server-side).
                        // current_position() holds raw servo degrees and would
                        // corrupt the TPOINT fit, mirroring the bootstrap bug.
                        astro_mount::MountPosition mountPos;
                        mountPos.set_axis1(state.telescope_axis1());
                        mountPos.set_axis2(state.telescope_axis2());
                        *m.mutable_mount_position() = mountPos;
                        m_grpc->addTPointMeasurement(m);
                        m_grpc->runTPointCalibration();
                        TPointCalibrationSP.s = IPS_OK;
                        LOG_INFO("TPOINT measurement added and calibration run");
                    }
                }
                else if (idx == 1) // CLEAR
                {
                    m_grpc->clearTPointMeasurements();
                    TPointCalibrationSP.s = IPS_IDLE;
                }
                else // STATUS
                {
                    auto tp = m_grpc->getTPointParameters();
                    std::string coeffs;
                    for (int i = 0; i < tp.coefficients_size(); ++i)
                    {
                        if (i > 0) coeffs += ", ";
                        coeffs += std::to_string(tp.coefficients(i));
                    }
                    IUSaveText(&TPointStatusT[0], coeffs.empty() ? "none" : coeffs.c_str());
                    IUSaveText(&TPointStatusT[1], std::to_string(tp.chi_squared()).c_str());
                    IUSaveText(&TPointStatusT[2], tp.calibrated() ? "Yes" : "No");
                    IDSetText(&TPointStatusTP, nullptr);
                    TPointCalibrationSP.s = IPS_OK;
                }
            }
            catch (const std::exception& e)
            {
                LOGF_ERROR("TPOINT calibration error: %s", e.what());
                TPointCalibrationSP.s = IPS_ALERT;
            }
            IDSetSwitch(&TPointCalibrationSP, nullptr);
            return true;
        }

        // Encoder control (ENABLE / DISABLE)
        if (!strcmp(name, EncodersSP.name))
        {
            IUUpdateSwitch(&EncodersSP, states, names, n);
            try
            {
                if (EncodersS[0].s == ISS_ON)
                {
                    auto cfg = m_grpc->getConfiguration();
                    astro_mount::EncoderConfig ec;
                    ec.set_type(cfg.encoders_absolute()
                        ? astro_mount::EncoderConfig_EncoderType_ABSOLUTE
                        : astro_mount::EncoderConfig_EncoderType_INCREMENTAL);
                    ec.set_resolution(cfg.encoder_resolution_config());
                    ec.set_use_feedback(true);
                    m_grpc->enableEncoders(ec);
                    LOG_INFO("Encoders enabled");
                }
                else
                {
                    m_grpc->disableEncoders();
                    LOG_INFO("Encoders disabled");
                }
                EncodersSP.s = IPS_OK;
            }
            catch (const std::exception& e)
            {
                LOGF_ERROR("Encoder control failed: %s", e.what());
                EncodersSP.s = IPS_ALERT;
            }
            IDSetSwitch(&EncodersSP, nullptr);
            return true;
        }

        // Homing — set reference position from current telescope axes
        if (!strcmp(name, HomeSP.name))
        {
            IUResetSwitch(&HomeSP);
            try
            {
                auto state = m_grpc->getState();
                astro_mount::MountHomingRequest req;
                req.set_axis1(state.telescope_axis1());
                req.set_axis2(state.telescope_axis2());
                m_grpc->home(req);
                HomeSP.s = IPS_OK;
                LOGF_INFO("Home reference set: axis1=%.4f°, axis2=%.4f°",
                          req.axis1(), req.axis2());
            }
            catch (const std::exception& e)
            {
                LOGF_ERROR("Home failed: %s", e.what());
                HomeSP.s = IPS_ALERT;
            }
            IDSetSwitch(&HomeSP, nullptr);
            return true;
        }

        // Emergency stop (all axes)
        if (!strcmp(name, EmergencyStopSP.name))
        {
            IUResetSwitch(&EmergencyStopSP);
            try
            {
                astro_mount::EmergencyStopRequest req;
                req.set_axis_id(-1);
                req.set_reset_after(false);
                m_grpc->emergencyStop(req);
                EmergencyStopSP.s = IPS_OK;
                TrackState = SCOPE_IDLE;
                LOG_WARN("Emergency stop triggered");
            }
            catch (const std::exception& e)
            {
                LOGF_ERROR("Emergency stop failed: %s", e.what());
                EmergencyStopSP.s = IPS_ALERT;
            }
            IDSetSwitch(&EmergencyStopSP, nullptr);
            return true;
        }

        // Controller state save / load
        if (!strcmp(name, ControllerStateSP.name))
        {
            int idx = IUFindOnSwitchIndex(&ControllerStateSP);
            IUResetSwitch(&ControllerStateSP);
            try
            {
                if (idx == 0)
                {
                    astro_mount::StateSaveRequest req;
                    req.set_include_measurements(true);
                    auto resp = m_grpc->saveState(req);
                    LOGF_INFO("Controller state saved: %s", resp.file_path().c_str());
                }
                else
                {
                    astro_mount::StateLoadRequest req;
                    m_grpc->loadState(req);
                    LOG_INFO("Controller state loaded");
                }
                ControllerStateSP.s = IPS_OK;
            }
            catch (const std::exception& e)
            {
                LOGF_ERROR("Controller state operation failed: %s", e.what());
                ControllerStateSP.s = IPS_ALERT;
            }
            IDSetSwitch(&ControllerStateSP, nullptr);
            return true;
        }

        // Automatic bootstrap (RUN / STATUS)
        if (!strcmp(name, AutoBootstrapSP.name))
        {
            int idx = IUFindOnSwitchIndex(&AutoBootstrapSP);
            IUResetSwitch(&AutoBootstrapSP);
            try
            {
                if (idx == 0)
                {
                    astro_mount::AutoBootstrapRequest req;
                    req.set_min_measurements(3);
                    req.set_max_alignment_error_arcsec(60.0);
                    req.set_proceed_to_tpoint(false);
                    m_grpc->runAutomaticBootstrap(req);
                    AutoBootstrapSP.s = IPS_OK;
                    LOG_INFO("Automatic bootstrap started");
                }
                else
                {
                    auto st = m_grpc->getAutoBootstrapStatus();
                    LOGF_INFO("Auto-bootstrap: state=%d, progress=%.1f%%, measurements=%d/%d",
                              st.state(), st.progress_percent(),
                              st.measurements_collected(), st.measurements_target());
                    AutoBootstrapSP.s = IPS_OK;
                }
            }
            catch (const std::exception& e)
            {
                LOGF_ERROR("Auto-bootstrap failed: %s", e.what());
                AutoBootstrapSP.s = IPS_ALERT;
            }
            IDSetSwitch(&AutoBootstrapSP, nullptr);
            return true;
        }

        // Gamepad manual-control loop (START / STOP)
        if (!strcmp(name, GamepadSP.name))
        {
            int idx = IUFindOnSwitchIndex(&GamepadSP);
            IUResetSwitch(&GamepadSP);
            try
            {
                if (idx == 0) m_grpc->startGamepad();
                else m_grpc->stopGamepad();
                GamepadSP.s = IPS_OK;
                LOGF_INFO("Gamepad %s", idx == 0 ? "started" : "stopped");
            }
            catch (const std::exception& e)
            {
                LOGF_ERROR("Gamepad control failed: %s", e.what());
                GamepadSP.s = IPS_ALERT;
            }
            IDSetSwitch(&GamepadSP, nullptr);
            return true;
        }

        // LX200 serial protocol server (START / STOP)
        if (!strcmp(name, Lx200SP.name))
        {
            int idx = IUFindOnSwitchIndex(&Lx200SP);
            IUResetSwitch(&Lx200SP);
            try
            {
                astro_mount::Lx200Status st;
                if (idx == 0) st = m_grpc->startLx200();
                else st = m_grpc->stopLx200();
                Lx200SP.s = IPS_OK;
                LOGF_INFO("LX200 %s (running=%d, port=%s)",
                          idx == 0 ? "started" : "stopped", st.running(), st.port().c_str());
            }
            catch (const std::exception& e)
            {
                LOGF_ERROR("LX200 control failed: %s", e.what());
                Lx200SP.s = IPS_ALERT;
            }
            IDSetSwitch(&Lx200SP, nullptr);
            return true;
        }
    }

    return INDI::Telescope::ISNewSwitch(dev, name, states, names, n);
}

bool AstroMountINDI::ISNewText(const char* dev, const char* name,
                                char* texts[], char* names[], int n)
{
    if (dev && !strcmp(dev, getDeviceName()))
    {
        LOGF_DEBUG("ISNewText(%s, n=%d)", name, n);

        // gRPC endpoint configuration (host/port) — editable from the client
        // even while connected; takes effect on the next Connect.
        if (!strcmp(name, ConnectionTP.name))
        {
            IUUpdateText(&ConnectionTP, texts, names, n);
            m_grpcHost = ConnectionT[0].text;
            m_grpcPort = atoi(ConnectionT[1].text);
            if (m_grpcPort <= 0) m_grpcPort = 50051;
            IDSetText(&ConnectionTP, nullptr);
            LOGF_INFO("gRPC connection configured: %s:%d", m_grpcHost.c_str(), m_grpcPort);
            return true;
        }
    }

    return INDI::Telescope::ISNewText(dev, name, texts, names, n);
}

// ============================================
// Timer — periodic polling
// ============================================

void AstroMountINDI::TimerHit()
{
    if (!isConnected())
    {
        SetTimer(getCurrentPollingPeriod());
        return;
    }

    // Poll controller state periodically
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_lastPoll).count();

    if (elapsed >= 1000) // Poll every 1 second
    {
        pollController();
        updateIndiProperties();
        m_lastPoll = now;
    }

    SetTimer(getCurrentPollingPeriod());
}

// ============================================
// Telescope movement methods
// ============================================

bool AstroMountINDI::Goto(double ra, double dec)
{
    return GotoRaDec(ra, dec);
}

bool AstroMountINDI::GotoRaDec(double ra, double dec)
{
    LOGF_DEBUG("GotoRaDec(RA=%.6f, Dec=%.6f)", ra, dec);

    try
    {
        m_targetRA = ra;
        m_targetDec = dec;

        auto coords = m_mapper->toGrpcCoordinates(ra, dec);
        m_grpc->slewToCoordinates(coords);

        TrackState = SCOPE_SLEWING;

        LOGF_INFO("Slewing to RA=%.4f, Dec=%.4f", ra, dec);
        return true;
    }
    catch (const std::exception& e)
    {
        LOGF_ERROR("GotoRaDec failed: %s", e.what());
        TrackState = SCOPE_IDLE;
        return false;
    }
}

bool AstroMountINDI::Sync(double ra, double dec)
{
    LOGF_DEBUG("Sync(RA=%.6f, Dec=%.6f)", ra, dec);

    try
    {
        if (!addSyncMeasurement(ra, dec))
            return false;

        auto result = m_grpc->runBootstrapCalibration();
        if (result.success())
        {
            LOGF_INFO("Sync successful. Error: %.2f arcsec",
                     result.alignment_error_arcsec());
            return true;
        }

        LOGF_ERROR("Sync failed: %s", result.error_message().c_str());
        return false;
    }
    catch (const std::exception& e)
    {
        LOGF_ERROR("Sync exception: %s", e.what());
        return false;
    }
}

bool AstroMountINDI::MoveNS(INDI_DIR_NS dir, TelescopeMotionCommand command)
{
    LOGF_DEBUG("MoveNS(%s, %s)", dir == DIRECTION_NORTH ? "North" : "South",
              command == MOTION_START ? "Start" : "Stop");

    try
    {
        if (command == MOTION_START)
        {
            // Move Dec axis (axis_id=1) at the selected slew rate
            double rate = (dir == DIRECTION_NORTH) ? m_slewRateDegPerSec
                                                    : -m_slewRateDegPerSec;
            astro_mount::AxisControlRequest req;
            req.set_axis_id(1);
            req.set_mode(astro_mount::AxisControlMode::VELOCITY_CONTROL);
            req.set_target_velocity(rate);
            req.set_relative(false);
            m_grpc->controlAxis(req);
        }
        else // MOTION_STOP
        {
            astro_mount::AxisStopRequest req;
            req.set_axis_id(1);
            m_grpc->stopAxis(req);
        }
        return true;
    }
    catch (const std::exception& e)
    {
        LOGF_ERROR("MoveNS failed: %s", e.what());
        return false;
    }
}

bool AstroMountINDI::MoveWE(INDI_DIR_WE dir, TelescopeMotionCommand command)
{
    LOGF_DEBUG("MoveWE(%s, %s)", dir == DIRECTION_WEST ? "West" : "East",
              command == MOTION_START ? "Start" : "Stop");

    try
    {
        if (command == MOTION_START)
        {
            // Move RA axis (axis_id=0) at the selected slew rate
            double rate = (dir == DIRECTION_WEST) ? m_slewRateDegPerSec
                                                   : -m_slewRateDegPerSec;
            astro_mount::AxisControlRequest req;
            req.set_axis_id(0);
            req.set_mode(astro_mount::AxisControlMode::VELOCITY_CONTROL);
            req.set_target_velocity(rate);
            req.set_relative(false);
            m_grpc->controlAxis(req);
        }
        else // MOTION_STOP
        {
            astro_mount::AxisStopRequest req;
            req.set_axis_id(0);
            m_grpc->stopAxis(req);
        }
        return true;
    }
    catch (const std::exception& e)
    {
        LOGF_ERROR("MoveWE failed: %s", e.what());
        return false;
    }
}

// 0.5x sidereal rate — used for INDI guide pulses (INDI::GuiderInterface).
static constexpr double kGuideRateDegPerSec = 0.5 * 15.041067 / 3600.0;
static constexpr double kGuideRateArcsecPerSec = 0.5 * 15.041067;

bool AstroMountINDI::SetSlewRate(int index)
{
    // INDI TelescopeSlewRate: 0 = GUIDE, 1 = CENTERING, 2 = FIND, 3 = MAX.
    static const double kSlewRatesDegPerSec[4] = {
        kGuideRateDegPerSec,  // guide speed
        0.05,                 // centering
        0.5,                  // find
        1.0                   // max
    };

    if (index < 0 || index > 3)
        return false;

    m_slewRateIndex = index;
    m_slewRateDegPerSec = kSlewRatesDegPerSec[index];
    LOGF_INFO("Slew rate set to %.6f deg/s", m_slewRateDegPerSec);
    return true;
}

bool AstroMountINDI::SetTrackMode(uint8_t mode)
{
    if (mode > INDI::Telescope::TRACK_CUSTOM)
        return false;

    m_trackMode = mode;
    LOGF_INFO("Track mode set to %d", mode);
    return true;
}

bool AstroMountINDI::SetTrackRate(double raRate, double deRate)
{
    // Store the custom rates and switch to CUSTOM mode. They are sent to the
    // controller as Coordinates.custom_track_rate_ra/dec by SetTrackEnabled().
    m_customTrackRaArcsecPerSec = raRate;
    m_customTrackDecArcsecPerSec = deRate;
    m_trackMode = INDI::Telescope::TRACK_CUSTOM;
    LOGF_INFO("Custom track rate: RA=%.4f arcsec/s, Dec=%.4f arcsec/s",
              raRate, deRate);
    return true;
}

bool AstroMountINDI::SetTrackEnabled(bool enabled)
{
    try
    {
        if (enabled)
        {
            // Track the CURRENT on-sky position, not the last Goto target.
            // KStars "Track" (and "Slew and Track" once the slew settles)
            // enables sidereal tracking where the telescope currently points.
            // Using the stale m_targetRA/m_targetDec from an earlier Goto made
            // the controller slew away to a previous target — the "two
            // alternating positions" symptom observed after "Use current".
            astro_mount::ControllerState state;
            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                state = m_lastState;
            }

            double raHours = 0.0, decDegrees = 0.0;
            const double lst = m_mapper->computeLst();
            if (!m_mapper->toIndiRaDec(state, lst, raHours, decDegrees))
            {
                LOG_WARN("Cannot enable tracking — no current position available");
                return false;
            }

            auto coords = m_mapper->toGrpcCoordinates(raHours, decDegrees);
            coords.set_tracking_mode(m_trackMode);
            if (m_trackMode == INDI::Telescope::TRACK_CUSTOM)
            {
                coords.set_custom_track_rate_ra(m_customTrackRaArcsecPerSec);
                coords.set_custom_track_rate_dec(m_customTrackDecArcsecPerSec);
            }
            LOGF_DEBUG("SetTrackEnabled(true): RA=%.6f Dec=%.4f mode=%d "
                       "customRA=%.4f customDec=%.4f arcsec/s",
                       coords.ra(), coords.dec(), m_trackMode,
                       m_customTrackRaArcsecPerSec, m_customTrackDecArcsecPerSec);
            m_grpc->trackObject(coords);
            TrackState = SCOPE_TRACKING;
            LOGF_INFO("Tracking engaged at current position (mode=%d)", m_trackMode);
        }
        else
        {
            m_grpc->stop();
            TrackState = SCOPE_IDLE;
            LOG_INFO("Tracking disengaged");
        }
        return true;
    }
    catch (const std::exception& e)
    {
        LOGF_ERROR("SetTrackEnabled failed: %s", e.what());
        return false;
    }
}

IPState AstroMountINDI::GuideNorth(uint32_t ms)
{
    return guideCorrection(0.0, kGuideRateArcsecPerSec * ms / 1000.0);
}

IPState AstroMountINDI::GuideSouth(uint32_t ms)
{
    return guideCorrection(0.0, -kGuideRateArcsecPerSec * ms / 1000.0);
}

IPState AstroMountINDI::GuideEast(uint32_t ms)
{
    // East = RA+ → positive RA correction.
    return guideCorrection(kGuideRateArcsecPerSec * ms / 1000.0, 0.0);
}

IPState AstroMountINDI::GuideWest(uint32_t ms)
{
    // West = RA- → negative RA correction.
    return guideCorrection(-kGuideRateArcsecPerSec * ms / 1000.0, 0.0);
}

IPState AstroMountINDI::guideCorrection(double raCorrectionArcsec, double decCorrectionArcsec)
{
    LOGF_DEBUG("Guide correction: dRA=%.3f arcsec, dDec=%.3f arcsec",
               raCorrectionArcsec, decCorrectionArcsec);
    try
    {
        astro_mount::GuiderCorrection correction;
        correction.set_ra_correction(raCorrectionArcsec);
        correction.set_dec_correction(decCorrectionArcsec);
        m_grpc->sendGuiderCorrection(correction);
        return IPS_OK;
    }
    catch (const std::exception& e)
    {
        LOGF_ERROR("Guide correction failed: %s", e.what());
        return IPS_ALERT;
    }
}

bool AstroMountINDI::Abort()
{
    LOG_DEBUG("Abort()");

    try
    {
        m_grpc->stop();
        TrackState = SCOPE_IDLE;
        return true;
    }
    catch (const std::exception& e)
    {
        LOGF_ERROR("Abort failed: %s", e.what());
        return false;
    }
}

bool AstroMountINDI::Park()
{
    LOG_DEBUG("Park()");

    try
    {
        m_grpc->park();
        m_isParked = true;
        TrackState = SCOPE_PARKED;
        LOG_INFO("Mount parked");
        return true;
    }
    catch (const std::exception& e)
    {
        LOGF_ERROR("Park failed: %s", e.what());
        return false;
    }
}

bool AstroMountINDI::UnPark()
{
    LOG_DEBUG("Unpark()");

    try
    {
        m_grpc->unpark();
        m_isParked = false;
        TrackState = SCOPE_IDLE;
        LOG_INFO("Mount unparked");
        return true;
    }
    catch (const std::exception& e)
    {
        LOGF_ERROR("Unpark failed: %s", e.what());
        return false;
    }
}

bool AstroMountINDI::Flip(double ra, double dec)
{
    LOGF_INFO("Meridian flip requested for RA=%.4f Dec=%.4f", ra, dec);

    // Execute a real meridian flip through the controller (slew HA+180°,
    // Dec→180°-Dec, then resume tracking on the opposite pier side).
    try
    {
        m_grpc->executeMeridianFlip();
        TrackState = SCOPE_SLEWING;
        return true;
    }
    catch (const std::exception& e)
    {
        LOGF_ERROR("Meridian flip failed: %s", e.what());
        return false;
    }
}

bool AstroMountINDI::updateTime(ln_date* utc, double utc_offset)
{
    // Store the client-provided time so the driver's coordinate conversions
    // (LST, JNow↔J2000, Alt/Az) use it instead of the system clock. The
    // controller itself uses system time internally and exposes no time RPC.
    if (utc)
    {
        const double jd = ln_get_julian_day(utc);
        m_mapper->setJulianDate(jd);
        LOGF_INFO("Time updated: JD=%.6f, UTC offset=%.2f h", jd, utc_offset);
    }
    else
    {
        LOG_WARN("updateTime called with null ln_date");
    }
    return true;
}

bool AstroMountINDI::SetCurrentPark()
{
    LOG_DEBUG("SetCurrentPark()");

    try
    {
        auto state = m_grpc->getState();
        auto pos = state.current_position();

        // Save current mount position as park position (HA/Dec for equatorial)
        SetParkPosition(pos.axis1(), pos.axis2());

        // Also update configuration on controller
        auto config = m_grpc->getConfiguration();
        config.set_park_position_axis1(pos.axis1());
        config.set_park_position_axis2(pos.axis2());
        m_grpc->updateConfiguration(config);

        LOGF_INFO("Current park set: axis1=%.2f, axis2=%.2f", pos.axis1(), pos.axis2());
        return true;
    }
    catch (const std::exception& e)
    {
        LOGF_ERROR("SetCurrentPark failed: %s", e.what());
        return false;
    }
}

bool AstroMountINDI::SetDefaultPark()
{
    // Set default park: HA=0, Dec=90 (NCP)
    SetParkPosition(0.0, 90.0);
    return true;
}

bool AstroMountINDI::updateLocation(double latitude, double longitude, double elevation)
{
    LOGF_DEBUG("UpdateLocation(lat=%.4f, lon=%.4f, elev=%.1f)",
              latitude, longitude, elevation);

    try
    {
        auto config = m_grpc->getConfiguration();
        config.set_latitude(latitude);
        config.set_longitude(longitude);
        config.set_altitude(elevation);
        m_grpc->updateConfiguration(config);

        m_mapper->setLocation(latitude, longitude, elevation);
        return true;
    }
    catch (const std::exception& e)
    {
        LOGF_ERROR("UpdateLocation failed: %s", e.what());
        return false;
    }
}

bool AstroMountINDI::ReadScopeStatus()
{
    // Poll fresh state from controller
    if (!pollController())
        return false;

    // Update INDI properties
    updateIndiProperties();

    return true;
}

// ============================================
// Internal helpers
// ============================================

bool AstroMountINDI::pollController()
{
    if (!isConnected())
        return false;

    try
    {
        auto state = m_grpc->getState();

        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_lastState = state;
        }

        LOGF_DEBUG("Poll OK: status=%d, axis1=%.4f°, axis2=%.4f°, "
                   "encoders=%d, guider=%d, pier=%.1f",
                   state.status(), state.current_position().axis1(),
                   state.current_position().axis2(), state.encoders_enabled(),
                   state.guider_active(), state.pier_side());

        return true;
    }
    catch (const std::exception& e)
    {
        LOGF_ERROR("Controller poll failed: %s", e.what());

        // If connection is lost, try to reconnect
        if (!m_grpc->isConnected())
        {
            try
            {
                m_grpc->reconnect();
                LOG_INFO("Reconnected to controller");
            }
            catch (const std::exception& re)
            {
                LOGF_ERROR("Reconnection failed: %s", re.what());
            }
        }

        return false;
    }
}

void AstroMountINDI::updateIndiProperties()
{
    astro_mount::ControllerState state;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        state = m_lastState;
    }

    double lst = m_mapper->computeLst();

    // Convert mount position to RA/Dec
    double raHours = 0, decDegrees = 0;
    if (!m_mapper->toIndiRaDec(state, lst, raHours, decDegrees))
    {
        return;
    }

    // Update INDI equatorial coordinates
    setEquatorialCoords(raHours, decDegrees);

    // Update track state
    TrackState = static_cast<INDI::Telescope::TelescopeStatus>(
        m_mapper->toIndiTrackState(state.status()));

    // Update pier side
    setPierSide(static_cast<INDI::Telescope::TelescopePierSide>(
        m_mapper->toIndiPierSide(state.pier_side())));

    // ============================================
    // Mount status — surfaces the controller ERROR state
    // ============================================
    {
        const char* stateName = "UNKNOWN";
        switch (state.status())
        {
        case astro_mount::ControllerState_MountStatus_IDLE:     stateName = "IDLE";     break;
        case astro_mount::ControllerState_MountStatus_SLEWING:  stateName = "SLEWING";  break;
        case astro_mount::ControllerState_MountStatus_TRACKING: stateName = "TRACKING"; break;
        case astro_mount::ControllerState_MountStatus_PARKED:   stateName = "PARKED";   break;
        case astro_mount::ControllerState_MountStatus_ERROR:    stateName = "ERROR";    break;
        default: break;
        }
        const bool inError = (state.status() == astro_mount::ControllerState_MountStatus_ERROR);
        IUSaveText(&MountStatusT[0], stateName);
        IUSaveText(&MountStatusT[1], inError ? "Mount in ERROR state" : "");
        MountStatusTP.s = inError ? IPS_ALERT : IPS_OK;
        IDSetText(&MountStatusTP, nullptr);
    }

    // Update time to meridian
    if (state.time_to_meridian() != 0)
    {
        // TODO: set time to meridian property if defined
    }

    // ============================================
    // Faza 3: TPOINT status
    // ============================================
    if (state.has_tpoint_params())
    {
        const auto& tp = state.tpoint_params();
        std::string coeffs;
        for (int i = 0; i < tp.coefficients_size(); ++i)
        {
            if (i > 0) coeffs += ", ";
            coeffs += std::to_string(tp.coefficients(i));
        }
        IUSaveText(&TPointStatusT[0], coeffs.empty() ? "none" : coeffs.c_str());
        IUSaveText(&TPointStatusT[1], std::to_string(tp.chi_squared()).c_str());
        IUSaveText(&TPointStatusT[2], tp.calibrated() ? "Yes" : "No");
        IDSetText(&TPointStatusTP, nullptr);
    }

    // ============================================
    // Faza 3: Environmental conditions
    // ============================================
    EnvironmentN[0].value = state.temperature();
    EnvironmentN[1].value = state.pressure();
    EnvironmentN[2].value = state.humidity();
    IDSetNumber(&EnvironmentNP, nullptr);

    // ============================================
    // Horizontal (Alt/Az) readback
    // ============================================
    double altDeg = 0, azDeg = 0;
    m_mapper->equatorialToHorizontal(raHours, decDegrees, altDeg, azDeg);
    HorizontalCoordN[0].value = altDeg;
    HorizontalCoordN[1].value = azDeg;
    IDSetNumber(&HorizontalCoordNP, nullptr);

    LOGF_DEBUG("Scope: RA=%.6f Dec=%.4f LST=%.6f Alt=%.4f Az=%.4f status=%d pier=%.1f",
               raHours, decDegrees, lst, altDeg, azDeg,
               state.status(), state.pier_side());
}

void AstroMountINDI::setEquatorialCoords(double raHours, double decDegrees)
{
    // Update EOD coordinates (JNow) — INDI 2.x standard is a single NewRaDec.
    NewRaDec(raHours, decDegrees);

    // Update J2000 coordinates. The incoming raHours/decDegrees are JNow, so
    // precess them before storing; previously the J2000 display showed JNow
    // values, which made "current object" calibrations double-precess.
    double raJ2000 = 0.0, decJ2000 = 0.0;
    m_mapper->jnowToJ2000(raHours, decDegrees, raJ2000, decJ2000);
    EquatorialCoordsJ2000N[0].value = raJ2000;
    EquatorialCoordsJ2000N[1].value = decJ2000;
    IDSetNumber(&EquatorialCoordsJ2000NP, nullptr);
}

bool AstroMountINDI::addSyncMeasurement(double raHours, double decDegrees)
{
    LOGF_DEBUG("addSyncMeasurement: client RA=%.6f Dec=%.4f", raHours, decDegrees);
    auto state = m_grpc->getState();

    // The server's AddBootstrapMeasurement treats MountPosition.axis1/axis2 as
    // TELESCOPE degrees (HA/Dec or alt/az in [0°,360°)) and feeds them through
    // sin/cos to build a unit vector for the Wahba/SVD orientation fit. Sending
    // current_position() (raw SERVO degrees, i.e. telescope degrees × gear ratio)
    // therefore produces an essentially random unit vector and corrupts the
    // computed orientation — which shows up as a random on-sky position after
    // calibration. Use telescope_axis1/2 instead.
    astro_mount::MountPosition mountPos;
    mountPos.set_axis1(state.telescope_axis1());
    mountPos.set_axis2(state.telescope_axis2());

    // Catalog coordinates: the client provides JNow; toGrpcCoordinates now
    // passes JNow through unchanged (the controller works in JNow).
    auto expected = m_mapper->toGrpcCoordinates(raHours, decDegrees);

    // Observed coordinates: current telescope pointing (JNow).
    double lst = m_mapper->computeLst();
    double curRa = 0, curDec = 0;
    if (!m_mapper->toIndiRaDec(state, lst, curRa, curDec))
        return false;

    astro_mount::Coordinates observed;
    observed.set_ra(curRa);
    observed.set_dec(curDec);
    observed.set_epoch(0.0);  // JNow (of date)

    astro_mount::BootstrapMeasurement measurement;
    *measurement.mutable_observed() = observed;
    *measurement.mutable_expected() = expected;
    *measurement.mutable_mount_position() = mountPos;
    measurement.set_use_for_initial_alignment(true);

    LOGF_DEBUG("addSyncMeasurement: expected RA=%.6f Dec=%.4f, observed RA=%.6f Dec=%.4f",
               expected.ra(), expected.dec(), observed.ra(), observed.dec());
    m_grpc->addBootstrapMeasurement(measurement);
    return true;
}

bool AstroMountINDI::performGoto(double ra, double dec)
{
    return GotoRaDec(ra, dec);
}

void AstroMountINDI::applyConnectionConfig()
{
    // Recreate the gRPC client from the UI-configured endpoint. Used at
    // Connect() so GRPC_CONNECTION / GRPC_TLS changes take effect.
    LOGF_DEBUG("applyConnectionConfig: %s:%d (ssl=%d)",
               m_grpcHost.c_str(), m_grpcPort, m_grpcUseSsl);
    m_grpc = std::make_unique<MountGrpcClient>(m_grpcHost, m_grpcPort, m_grpcUseSsl);
}

void AstroMountINDI::updateConnectionStatus()
{
    // Reflect the current endpoint + link state in the read-only status text.
    std::string status = isConnected()
        ? "Connected to " + m_grpcHost + ":" + std::to_string(m_grpcPort)
        : "Not connected (" + m_grpcHost + ":" + std::to_string(m_grpcPort) + ")";
    IUSaveText(&ConnectionStatusT[0], status.c_str());
    IDSetText(&ConnectionStatusTP, nullptr);
}
