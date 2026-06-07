#include "astro_mount_rotator_driver.h"

#include <indicom.h>
#include <memory>
#include <cstring>
#include <algorithm>

// ============================================
// C-linkage loader functions for indiserver
// ============================================

/**
 * @brief indiserver entry point: create and return a new AstroMountRotatorINDI instance.
 *
 * Usage:
 *   indiserver -v astro_mount_indi_rotator_driver
 *
 * Environment variables:
 *   GRPC_HOST  — gRPC server hostname (default: "localhost")
 *   GRPC_PORT  — gRPC server port     (default: "50051")
 */
static std::unique_ptr<AstroMountRotatorINDI> s_rotator(nullptr);

void ISGetProperties(const char* dev)
{
    if (!s_rotator)
    {
        const char* host = std::getenv("GRPC_HOST");
        const char* portStr = std::getenv("GRPC_PORT");
        int port = 50051;
        if (portStr) port = std::stoi(portStr);
        s_rotator = std::make_unique<AstroMountRotatorINDI>(
            host ? host : "localhost", port);
    }
    s_rotator->ISGetProperties(dev);
}

void ISNewSwitch(const char* dev, const char* name,
                 ISState* states, char* names[], int n)
{
    if (s_rotator)
        s_rotator->ISNewSwitch(dev, name, states, names, n);
}

void ISNewNumber(const char* dev, const char* name,
                 double values[], char* names[], int n)
{
    if (s_rotator)
        s_rotator->ISNewNumber(dev, name, values, names, n);
}

void ISNewText(const char* dev, const char* name,
               char* texts[], char* names[], int n)
{
    if (s_rotator)
        s_rotator->ISNewText(dev, name, texts, names, n);
}

void ISNewBLOB(const char* dev, const char* name,
               int sizes[], int blobsizes[], char* blobs[],
               char* formats[], char* names[], int n)
{
    if (s_rotator)
        s_rotator->ISNewBLOB(dev, name, sizes, blobsizes, blobs, formats, names, n);
}

void ISSnoopDevice(XMLEle* root)
{
    if (s_rotator)
        s_rotator->ISSnoopDevice(root);
}

// ============================================
// AstroMountRotatorINDI implementation
// ============================================

AstroMountRotatorINDI::AstroMountRotatorINDI(const char* grpcHost, int grpcPort)
    : m_grpcHost(grpcHost ? grpcHost : "localhost")
    , m_grpcPort(grpcPort > 0 ? grpcPort : 50051)
{
    // Use gRPC connection only — no serial/TCP plugins
    setRotatorConnection(CONNECTION_NONE);

    // Set capabilities: abort + home (no reverse/sync/backlash yet)
    SetCapability(ROTATOR_CAN_ABORT | ROTATOR_CAN_HOME);

    // Set version
    setVersion(2, 0);
}

bool AstroMountRotatorINDI::initProperties()
{
    // Call parent first — this defines ROTATOR_ANGLE, ROTATOR_SPEED,
    // ROTATOR_ABORT, ROTATOR_HOME, etc.
    INDI::Rotator::initProperties();

    // Set the default group/tab for rotator properties
    setDefaultGroup(MAIN_CONTROL_TAB);

    // Add debug and configuration controls
    addDebugControl();
    addConfigurationControl();

    return true;
}

bool AstroMountRotatorINDI::updateProperties()
{
    // Call parent to define/delete RotatorInterface properties
    INDI::Rotator::updateProperties();

    if (isConnected())
    {
        // RotatorInterface properties are already defined by parent
        // Start the polling timer
        SetTimer(POLL_INTERVAL_MS);
    }
    else
    {
        // Properties are cleaned up by parent
    }

    return true;
}

void AstroMountRotatorINDI::ISGetProperties(const char* dev)
{
    INDI::Rotator::ISGetProperties(dev);
}

bool AstroMountRotatorINDI::ISNewSwitch(const char* dev, const char* name,
                                         ISState* states, char* names[], int n)
{
    // First, try the RotatorInterface processing (handles ROTATOR_ABORT, ROTATOR_HOME, etc.)
    if (processSwitch(dev, name, states, names, n))
        return true;

    // Fall back to parent
    return INDI::Rotator::ISNewSwitch(dev, name, states, names, n);
}

bool AstroMountRotatorINDI::ISNewNumber(const char* dev, const char* name,
                                         double values[], char* names[], int n)
{
    // First, try the RotatorInterface processing (handles ROTATOR_ANGLE)
    if (processNumber(dev, name, values, names, n))
        return true;

    // Fall back to parent
    return INDI::Rotator::ISNewNumber(dev, name, values, names, n);
}

void AstroMountRotatorINDI::TimerHit()
{
    if (!isConnected())
        return;

    // Poll derotator status from gRPC
    pollStatus();

    // Schedule next timer tick
    SetTimer(POLL_INTERVAL_MS);
}

// ============================================
// RotatorInterface overrides
// ============================================

IPState AstroMountRotatorINDI::MoveRotator(double angle)
{
    LOG_DEBUG("MoveRotator(%g)", angle);

    try
    {
        astro_mount::FieldRotationControlRequest req;
        req.set_mode(astro_mount::FieldRotationControlRequest::FIXED_ANGLE);
        req.set_target_angle(angle);
        req.set_wait_for_completion(false);

        m_grpc->controlFieldRotation(req);

        // Motion started — return BUSY so INDI knows movement is in progress
        // The actual completion will be detected in pollStatus() via GetDerotatorStatus
        return IPS_BUSY;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("MoveRotator failed: %s", e.what());
        return IPS_ALERT;
    }
}

bool AstroMountRotatorINDI::AbortRotator()
{
    LOG_DEBUG("AbortRotator()");

    try
    {
        astro_mount::FieldRotationControlRequest req;
        req.set_mode(astro_mount::FieldRotationControlRequest::DISABLED);

        m_grpc->controlFieldRotation(req);
        return true;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("AbortRotator failed: %s", e.what());
        return false;
    }
}

IPState AstroMountRotatorINDI::HomeRotator()
{
    LOG_DEBUG("HomeRotator()");

    try
    {
        astro_mount::DerotatorHomingRequest req;
        req.set_method(astro_mount::DerotatorHomingRequest::AUTO);
        req.set_calibrate_after(true);

        m_grpc->homeDerotator(req);

        // Homing started — return BUSY
        return IPS_BUSY;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("HomeRotator failed: %s", e.what());
        return IPS_ALERT;
    }
}

bool AstroMountRotatorINDI::Handshake()
{
    LOG_DEBUG("Handshake — connecting to gRPC server %s:%d",
              m_grpcHost.c_str(), m_grpcPort);

    try
    {
        // Create gRPC client if needed
        if (!m_grpc)
        {
            m_grpc = std::make_unique<MountGrpcClient>(m_grpcHost, m_grpcPort);
        }

        // Connect (throws on failure)
        m_grpc->connect();

        // Verify health
        auto health = m_grpc->checkHealth("mount_controller");
        if (health.status() != astro_mount::HealthCheckResponse::SERVING)
        {
            LOG_ERROR("gRPC service is not serving (status=%d)", health.status());
            return false;
        }

        LOG_INFO("Connected to gRPC server at %s:%d",
                 m_grpcHost.c_str(), m_grpcPort);

        // Initial poll to cache status
        pollStatus();

        return true;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Handshake failed: %s", e.what());
        return false;
    }
}

// ============================================
// Internal helpers
// ============================================

bool AstroMountRotatorINDI::pollStatus()
{
    if (!m_grpc || !m_grpc->isConnected())
        return false;

    try
    {
        m_lastStatus = m_grpc->getDerotatorStatus();
        m_lastPoll = std::chrono::steady_clock::now();

        // Update INDI properties with new status
        updateRotatorProperties();

        return true;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("pollStatus failed: %s", e.what());
        return false;
    }
}

void AstroMountRotatorINDI::updateRotatorProperties()
{
    // Update the angle from the latest status
    double currentAngle = m_lastStatus.current_angle();
    setRotatorAngle(currentAngle);

    // Update ROTATOR_ANGLE property state based on whether rotator is moving
    if (m_lastStatus.moving())
    {
        GotoRotatorNP.s = IPS_BUSY;
    }
    else
    {
        GotoRotatorNP.s = IPS_OK;
    }

    // Update ABORT state
    AbortRotatorSP.s = IPS_IDLE;

    // Update HOME state
    HomeRotatorSP.s = m_lastStatus.homed() ? IPS_OK : IPS_IDLE;

    // Send all updates to clients
    IDSetNumber(&GotoRotatorNP, nullptr);
    IDSetSwitch(&AbortRotatorSP, nullptr);
    IDSetSwitch(&HomeRotatorSP, nullptr);
}

void AstroMountRotatorINDI::setRotatorAngle(double angleDegrees)
{
    // Normalize angle to 0-360 range for INDI
    double normalized = std::fmod(angleDegrees, 360.0);
    if (normalized < 0) normalized += 360.0;

    GotoRotatorN[0].value = normalized;
}
