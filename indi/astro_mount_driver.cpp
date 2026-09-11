#include "astro_mount_driver.h"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <algorithm>

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
    , m_isParked(false)
    , m_targetRA(0)
    , m_targetDec(0)
    , m_grpcHost(grpcHost ? grpcHost : "localhost")
    , m_grpcPort(grpcPort > 0 ? grpcPort : 50051)
{
    setVersion(2, 0);
    // INDI 2.x: telescope type is expressed through capability flags.
    SetTelescopeCapability(INDI::Telescope::TELESCOPE_CAN_GOTO |
                               INDI::Telescope::TELESCOPE_CAN_SYNC |
                               INDI::Telescope::TELESCOPE_CAN_PARK |
                               INDI::Telescope::TELESCOPE_CAN_ABORT |
                               INDI::Telescope::TELESCOPE_HAS_TRACK_MODE |
                               INDI::Telescope::TELESCOPE_CAN_CONTROL_TRACK,
                           4); // GUIDE / CENTERING / FIND / MAX slew rates

    m_grpc = std::make_unique<MountGrpcClient>(m_grpcHost, m_grpcPort, m_grpcUseSsl);
    m_mapper = std::make_unique<IndiPropertyMapper>();

    m_lastPoll = std::chrono::steady_clock::now();
}

bool AstroMountINDI::Connect()
{
    // Establish the gRPC channel to the mount controller. The base
    // INDI::Telescope::Connect() only flips the connection state and defines
    // properties — it knows nothing about the gRPC link, so without this the
    // "Connect" switch in the INDI client never reached the controller.
    // Recreate the client from the UI-configured host/port/TLS first, so any
    // GRPC_CONNECTION / GRPC_TLS changes are applied on every connect.
    applyConnectionConfig();
    try
    {
        m_grpc->connect();
    }
    catch (const std::exception& e)
    {
        LOGF_ERROR("Failed to connect to mount controller: %s", e.what());
        return false;
    }

    if (!INDI::Telescope::Connect())
        return false;

    LOG_INFO("Connected to mount controller (gRPC)");
    updateConnectionStatus();
    return true;
}

bool AstroMountINDI::Disconnect()
{
    bool ok = INDI::Telescope::Disconnect();
    try
    {
        m_grpc->disconnect();
    }
    catch (const std::exception& e)
    {
        LOGF_ERROR("Failed to disconnect gRPC: %s", e.what());
        ok = false;
    }
    LOG_INFO("Disconnected from mount controller (gRPC)");
    updateConnectionStatus();
    return ok;
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
    defineText(&ConnectionTP);
    defineSwitch(&ConnectionSslSP);
    defineText(&ConnectionStatusTP);
    updateConnectionStatus();
}

bool AstroMountINDI::initProperties()
{
    INDI::Telescope::initProperties();

    // Primary axis: EQUATORIAL_EOD_COORD (RA/Dec JNow)
    // We add J2000 as additional coordinate display
    IUFillNumber(&EquatorialCoordsJ2000N[0], "RA_J2000", "RA J2000", "%10.6f",
                 0, 24, 0.001, 0);
    IUFillNumber(&EquatorialCoordsJ2000N[1], "DEC_J2000", "DEC J2000", "%10.6f",
                 -90, 90, 0.001, 0);
    IUFillNumberVector(&EquatorialCoordsJ2000NP, EquatorialCoordsJ2000N, 2,
                       getDeviceName(), "EQUATORIAL_J2000", "Eq J2000",
                       MAIN_CONTROL_TAB, IP_RO, 60, IPS_IDLE);

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

    // Connection configuration (GRPC_CONNECTION / GRPC_TLS / GRPC_CONNECTION_STATUS)
    // is always defined so the endpoint can be configured before connecting.
    defineText(&ConnectionTP);
    defineSwitch(&ConnectionSslSP);
    defineText(&ConnectionStatusTP);
    updateConnectionStatus();

    if (isConnected())
    {
        defineNumber(&EquatorialCoordsJ2000NP);
        defineSwitch(&BootstrapCalibrationSP);
        defineText(&BootstrapStatusTP);
        defineText(&TPointStatusTP);
        defineNumber(&EnvironmentNP);

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
        // Handle J2000 coordinate input
        if (!strcmp(name, EquatorialCoordsJ2000NP.name))
        {
            // Read-only in Faza 2; will be writable in Faza 3
            IDSetNumber(&EquatorialCoordsJ2000NP, nullptr);
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
                // Add current position as bootstrap measurement and calibrate
                try
                {
                    auto state = m_grpc->getState();
                    auto mountPos = state.current_position();

                    // Use current J2000 coords from client
                    double ra = EquatorialCoordsJ2000N[0].value;
                    double dec = EquatorialCoordsJ2000N[1].value;

                    if (ra == 0 && dec == 0)
                    {
                        // Use from EQUATORIAL_EOD_COORD instead
                        ra = EquatorialCoordsJ2000N[0].value;
                        dec = EquatorialCoordsJ2000N[1].value;
                    }

                    auto coords = m_mapper->toGrpcCoordinates(ra, dec);

                    astro_mount::BootstrapMeasurement measurement;
                    *measurement.mutable_observed() = coords;
                    *measurement.mutable_expected() = coords;
                    *measurement.mutable_mount_position() = mountPos;
                    measurement.set_use_for_initial_alignment(true);

                    m_grpc->addBootstrapMeasurement(measurement);
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
    }

    return INDI::Telescope::ISNewSwitch(dev, name, states, names, n);
}

bool AstroMountINDI::ISNewText(const char* dev, const char* name,
                                char* texts[], char* names[], int n)
{
    if (dev && !strcmp(dev, getDeviceName()))
    {
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
        auto state = m_grpc->getState();
        auto mountPos = state.current_position();

        auto coords = m_mapper->toGrpcCoordinates(ra, dec);

        astro_mount::BootstrapMeasurement measurement;
        *measurement.mutable_observed() = coords;
        *measurement.mutable_expected() = coords;
        *measurement.mutable_mount_position() = mountPos;
        measurement.set_use_for_initial_alignment(true);

        m_grpc->addBootstrapMeasurement(measurement);
        auto result = m_grpc->runBootstrapCalibration();

        if (result.success())
        {
            LOGF_INFO("Sync successful. Error: %.2f arcsec",
                     result.alignment_error_arcsec());
            return true;
        }
        else
        {
            LOGF_ERROR("Sync failed: %s", result.error_message().c_str());
            return false;
        }
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
            // Move Dec axis (axis_id=1) at a fixed rate
            double rate = (dir == DIRECTION_NORTH) ? 1.0 : -1.0; // deg/s
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
            // Move RA axis (axis_id=0) at a fixed rate
            double rate = (dir == DIRECTION_WEST) ? 1.0 : -1.0; // deg/s
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

    double lst = IndiPropertyMapper::computeLst(m_mapper->latitude());

    // Convert mount position to RA/Dec
    double raHours = 0, decDegrees = 0;
    if (!m_mapper->toIndiRaDec(state.current_position(), state, lst,
                                raHours, decDegrees))
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
}

void AstroMountINDI::setEquatorialCoords(double raHours, double decDegrees)
{
    // Update EOD coordinates (JNow) — INDI 2.x standard is a single NewRaDec.
    NewRaDec(raHours, decDegrees);

    // Update J2000 coordinates
    EquatorialCoordsJ2000N[0].value = raHours;
    EquatorialCoordsJ2000N[1].value = decDegrees;
    IDSetNumber(&EquatorialCoordsJ2000NP, nullptr);
}

bool AstroMountINDI::performGoto(double ra, double dec)
{
    return GotoRaDec(ra, dec);
}

void AstroMountINDI::applyConnectionConfig()
{
    // Recreate the gRPC client from the UI-configured endpoint. Used at
    // Connect() so GRPC_CONNECTION / GRPC_TLS changes take effect.
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
