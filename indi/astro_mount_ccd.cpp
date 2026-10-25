#include "astro_mount_ccd.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <stdexcept>

// indiserver entry points — mirrors the mount driver loader pattern.
static std::unique_ptr<AstroMountCCD> s_driver;

void ISGetProperties(const char* dev)
{
    if (!s_driver)
    {
        const char* grpcHost = std::getenv("GRPC_HOST");
        if (!grpcHost) grpcHost = "localhost";

        const char* grpcPortStr = std::getenv("GRPC_PORT");
        int grpcPort = 50051;
        if (grpcPortStr) grpcPort = std::stoi(grpcPortStr);

        s_driver = std::make_unique<AstroMountCCD>(grpcHost, grpcPort);
    }
    s_driver->ISGetProperties(dev);
}

void ISNewSwitch(const char* dev, const char* name, ISState* states, char* names[], int n)
{
    if (s_driver) s_driver->ISNewSwitch(dev, name, states, names, n);
}

void ISNewNumber(const char* dev, const char* name, double values[], char* names[], int n)
{
    if (s_driver) s_driver->ISNewNumber(dev, name, values, names, n);
}

void ISNewText(const char* dev, const char* name, char* texts[], char* names[], int n)
{
    if (s_driver) s_driver->ISNewText(dev, name, texts, names, n);
}

void ISNewBLOB(const char* dev, const char* name, int sizes[], int blobsizes[],
               char* blobs[], char* formats[], char* names[], int n)
{
    if (s_driver) s_driver->ISNewBLOB(dev, name, sizes, blobsizes, blobs, formats, names, n);
}

void ISSnoopDevice(XMLEle* root)
{
    if (s_driver) s_driver->ISSnoopDevice(root);
}

// ============================================
// AstroMountCCD implementation
// ============================================

AstroMountCCD::AstroMountCCD(const char* grpcHost, int grpcPort)
    : INDI::CCD()
    , m_grpcHost(grpcHost ? grpcHost : "localhost")
    , m_grpcPort(grpcPort > 0 ? grpcPort : 50051)
{
    setVersion(2, 0);

    // The transport is gRPC, not a local USB/serial camera.  addAuxControls()
    // still supplies the CONNECT/DISCONNECT switch, but Connect()/Disconnect()
    // below bypass the serial/TCP plugins entirely.
    SetCCDCapability(CCD_CAN_ABORT | CCD_CAN_BIN | CCD_CAN_SUBFRAME |
                     CCD_HAS_COOLER | CCD_HAS_SHUTTER);

    m_client = std::make_unique<CameraGrpcClient>(m_grpcHost, m_grpcPort, m_grpcUseSsl);
}

AstroMountCCD::~AstroMountCCD()
{
    if (m_exposureThread.joinable())
        m_exposureThread.join();
}

const char* AstroMountCCD::getDefaultName()
{
    return "AstroMount CCD";
}

void AstroMountCCD::ISGetProperties(const char* dev)
{
    INDI::CCD::ISGetProperties(dev);
}

bool AstroMountCCD::initProperties()
{
    INDI::CCD::initProperties();
    addAuxControls();
    addDebugControl();

    IUFillNumber(&GainN[0], "GAIN", "Gain", "%.0f", 0, 100, 1, 0);
    IUFillNumberVector(&GainNP, GainN, 1, getDeviceName(), "CCD_GAIN", "Gain",
                       MAIN_CONTROL_TAB, IP_RW, 60, IPS_IDLE);

    IUFillNumber(&OffsetN[0], "OFFSET", "Offset", "%.0f", 0, 500, 1, 0);
    IUFillNumberVector(&OffsetNP, OffsetN, 1, getDeviceName(), "CCD_OFFSET", "Offset",
                       MAIN_CONTROL_TAB, IP_RW, 60, IPS_IDLE);

    IUFillNumber(&FilterSlotN[0], "FILTER_SLOT_VALUE", "Filter Slot", "%.0f", 1, 5, 1, 1);
    IUFillNumberVector(&FilterSlotNP, FilterSlotN, 1, getDeviceName(), "FILTER_SLOT",
                       "Filter Slot", MAIN_CONTROL_TAB, IP_RW, 60, IPS_IDLE);

    return true;
}

bool AstroMountCCD::updateProperties()
{
    INDI::CCD::updateProperties();

    if (isConnected())
    {
        defineProperty(&GainNP);
        defineProperty(&OffsetNP);

        if (!m_filterNames.empty())
        {
            int count = 0;
            for (const auto& name : m_filterNames)
            {
                if (count >= 8) break;
                IUFillText(&FilterNameT[count], name.c_str(), name.c_str(), name.c_str());
                ++count;
            }
            if (count > 0)
            {
                IUFillTextVector(&FilterNameTP, FilterNameT, count, getDeviceName(),
                                 "FILTER_NAME", "Filter Name", MAIN_CONTROL_TAB,
                                 IP_RO, 60, IPS_IDLE);
                defineProperty(&FilterNameTP);

                FilterSlotN[0].min = 1;
                FilterSlotN[0].max = count;
                FilterSlotN[0].step = 1;
                FilterSlotN[0].value = 1;
                IDSetNumber(&FilterSlotNP, nullptr);
                defineProperty(&FilterSlotNP);
            }
        }

        SetTimer(1000);  // poll cooler/temperature once per second
    }
    else
    {
        deleteProperty(GainNP.name);
        deleteProperty(OffsetNP.name);
        deleteProperty(FilterNameTP.name);
        deleteProperty(FilterSlotNP.name);
    }

    return true;
}

bool AstroMountCCD::ISNewNumber(const char* dev, const char* name,
                                double values[], char* names[], int n)
{
    if (dev != nullptr && !strcmp(dev, getDeviceName()))
    {
        if (!strcmp(name, GainNP.name))
        {
            IUUpdateNumber(&GainNP, values, names, n);
            m_gain = GainN[0].value;
            GainNP.s = IPS_OK;
            IDSetNumber(&GainNP, nullptr);
            return true;
        }

        if (!strcmp(name, OffsetNP.name))
        {
            IUUpdateNumber(&OffsetNP, values, names, n);
            m_offset = OffsetN[0].value;
            OffsetNP.s = IPS_OK;
            IDSetNumber(&OffsetNP, nullptr);
            return true;
        }

        if (!strcmp(name, FilterSlotNP.name))
        {
            IUUpdateNumber(&FilterSlotNP, values, names, n);
            int slot = static_cast<int>(FilterSlotN[0].value);
            try
            {
                // INDI slots are 1-based; the camera service is 0-based.
                m_client->setFilter(slot - 1);
                m_filterSlot = slot - 1;
                FilterSlotNP.s = IPS_OK;
            }
            catch (const std::exception& e)
            {
                LOGF_ERROR("SetFilter failed: %s", e.what());
                FilterSlotNP.s = IPS_ALERT;
            }
            IDSetNumber(&FilterSlotNP, nullptr);
            return true;
        }
    }

    return INDI::CCD::ISNewNumber(dev, name, values, names, n);
}

bool AstroMountCCD::ISNewSwitch(const char* dev, const char* name,
                                ISState* states, char* names[], int n)
{
    return INDI::CCD::ISNewSwitch(dev, name, states, names, n);
}

bool AstroMountCCD::ISNewText(const char* dev, const char* name,
                              char* texts[], char* names[], int n)
{
    return INDI::CCD::ISNewText(dev, name, texts, names, n);
}

bool AstroMountCCD::Connect()
{
    try
    {
        m_client->connect();
        auto info = m_client->getCameraInfo();

        m_filterNames = info.filter_names;

        int bpp = static_cast<int>(info.bit_depth);
        if (bpp <= 0) bpp = 16;
        SetCCDParams(info.width, info.height, bpp,
                     static_cast<float>(info.pixel_size_um),
                     static_cast<float>(info.pixel_size_um));
    }
    catch (const std::exception& e)
    {
        LOGF_ERROR("Failed to connect to camera service: %s", e.what());
        return false;
    }

    setConnected(true, IPS_OK, nullptr);
    LOG_INFO("Connected to camera service (gRPC)");
    return true;
}

bool AstroMountCCD::Disconnect()
{
    try
    {
        m_client->disconnect();
    }
    catch (const std::exception& e)
    {
        LOGF_ERROR("Failed to disconnect gRPC: %s", e.what());
    }

    setConnected(false, IPS_OK, nullptr);
    LOG_INFO("Disconnected from camera service (gRPC)");
    return true;
}

bool AstroMountCCD::StartExposure(float duration)
{
    if (!isConnected())
        return false;

    m_exposureAbort = false;
    m_exposureRunning = true;
    PrimaryCCD.setExposureDuration(duration);

    if (m_exposureThread.joinable())
        m_exposureThread.join();

    m_exposureThread = std::thread([this, duration]() { runExposure(duration); });
    return true;
}

void AstroMountCCD::runExposure(float duration)
{
    try
    {
        auto result = m_client->startExposure(
            duration,
            static_cast<int>(m_gain),
            static_cast<int>(m_offset),
            PrimaryCCD.getBinX(),
            m_filterSlot,
            [this](float remaining_s, double) {
                if (m_exposureAbort) return;
                PrimaryCCD.setExposureLeft(remaining_s);
            });

        if (m_exposureAbort)
        {
            PrimaryCCD.setExposureFailed();
            m_exposureRunning = false;
            return;
        }

        if (result.image_bytes.empty())
        {
            LOG_ERROR("Camera service returned an empty image");
            PrimaryCCD.setExposureFailed();
            m_exposureRunning = false;
            return;
        }

        const size_t bytes = result.image_bytes.size();

        std::unique_lock<std::mutex> guard(ccdBufferLock);
        PrimaryCCD.setFrameBufferSize(static_cast<uint32_t>(bytes), true);
        std::memcpy(PrimaryCCD.getFrameBuffer(), result.image_bytes.data(), bytes);
        guard.unlock();

        PrimaryCCD.setResolution(result.width, result.height);
        PrimaryCCD.setFrame(0, 0, result.width, result.height);
        PrimaryCCD.setBPP(16);
        PrimaryCCD.setNAxis(2);
        PrimaryCCD.setExposureLeft(0.0);
        ExposureComplete(&PrimaryCCD);
    }
    catch (const std::exception& e)
    {
        if (!m_exposureAbort)
            LOGF_ERROR("Exposure failed: %s", e.what());
        PrimaryCCD.setExposureFailed();
    }

    m_exposureRunning = false;
}

bool AstroMountCCD::AbortExposure()
{
    m_exposureAbort = true;
    try
    {
        m_client->abortExposure();
    }
    catch (const std::exception& e)
    {
        LOGF_DEBUG("AbortExposure gRPC error (ignored): %s", e.what());
    }
    return true;
}

int AstroMountCCD::SetTemperature(double temperature)
{
    try
    {
        auto status = m_client->setCooler(temperature, true);
        TemperatureNP[0].setValue(status.current_c);
        TemperatureNP.setState(IPS_BUSY);
        TemperatureNP.apply();
        return 0;  // async — TimerHit() keeps polling until target reached
    }
    catch (const std::exception& e)
    {
        LOGF_ERROR("SetTemperature failed: %s", e.what());
        TemperatureNP.setState(IPS_ALERT);
        TemperatureNP.apply();
        return -1;
    }
}

bool AstroMountCCD::UpdateCCDFrame(int x, int y, int w, int h)
{
    // The simulated camera always produces a full-frame image; the frame
    // geometry is tracked by the base class for FITS header purposes.
    (void)x; (void)y; (void)w; (void)h;
    return true;
}

bool AstroMountCCD::UpdateCCDBin(int hor, int ver)
{
    (void)hor; (void)ver;
    return true;
}

bool AstroMountCCD::UpdateCCDFrameType(INDI::CCDChip::CCD_FRAME fType)
{
    (void)fType;
    return true;
}

void AstroMountCCD::TimerHit()
{
    if (!isConnected())
        return;

    if (HasCooler())
    {
        try
        {
            auto status = m_client->getCoolerStatus();
            const double old = TemperatureNP[0].getValue();
            if (std::abs(status.current_c - old) > 0.05)
            {
                TemperatureNP[0].setValue(status.current_c);
                TemperatureNP.apply();
            }
            if (status.reached_target && TemperatureNP.getState() != IPS_OK)
            {
                TemperatureNP.setState(IPS_OK);
                TemperatureNP.apply();
            }
        }
        catch (const std::exception&)
        {
            // Best-effort telemetry; ignore transient gRPC failures.
        }
    }

    SetTimer(1000);
}
