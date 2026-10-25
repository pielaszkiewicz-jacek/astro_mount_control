#include "indi_camera_client.h"
#include "logging/logger.h"
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace astro_camera {

namespace {
std::shared_ptr<spdlog::logger> cameraLog()
{
    return astro_mount::logging::Logger::get("camera");
}
}

IndiCameraClient::IndiCameraClient()
{
    reconnect_running_ = true;
    reconnect_thread_ = std::thread([this]() { reconnectLoop(); });
}

IndiCameraClient::~IndiCameraClient()
{
    reconnect_running_ = false;
    if (reconnect_thread_.joinable())
        reconnect_thread_.join();
    disconnectFromServer();
}

void IndiCameraClient::configure(const std::string& host, int port)
{
    host_ = host;
    port_ = port > 0 ? port : 7624;
}

bool IndiCameraClient::connectToServer()
{
    std::lock_guard<std::recursive_mutex> lock(connect_mutex_);
    setServer(host_.c_str(), static_cast<unsigned int>(port_));
    return INDI::BaseClient::connectServer();
}

std::vector<std::string> IndiCameraClient::listCameras()
{
    std::vector<std::string> cameras;
    if (!isServerConnected())
        return cameras;

    std::vector<INDI::BaseDevice> devices;
    if (!getDevices(devices, INDI::BaseDevice::CCD_INTERFACE))
        return cameras;

    for (const auto& device : devices)
        if (device.getDeviceName())
            cameras.emplace_back(device.getDeviceName());
    return cameras;
}

bool IndiCameraClient::connectCamera(const std::string& device)
{
    std::lock_guard<std::recursive_mutex> lock(connect_mutex_);
    if (device.empty() || !isServerConnected())
        return false;

    {
        std::lock_guard<std::mutex> lock(mtx_);
        device_ = device;
        info_ = Info{};
        info_valid_ = false;
        connected_ = false;
    }

    watchDevice(device.c_str());
    setBLOBMode(B_ALSO, device.c_str(), "CCD1");
    connectDevice(device.c_str());

    // connectDevice() is asynchronous: it sends CONNECT and returns
    // immediately. Wait for the driver to actually report CONNECTION=CONNECT
    // before treating the camera as usable. Some drivers connect and then
    // immediately disconnect when hardware initialisation fails (e.g. the
    // PlayerOne SDK returning POA_ERROR_NOT_OPENED), so re-check after a short
    // settle delay to catch that transient connect→disconnect.
    bool connected = false;
    for (int i = 0; i < 50; ++i)   // up to ~5 s
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto dev = getDevice(device.c_str());
        if (dev.isValid() && dev.isConnected())
        {
            connected = true;
            break;
        }
    }

    if (connected)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        auto dev = getDevice(device.c_str());
        connected = dev.isValid() && dev.isConnected();
    }

    if (!connected)
    {
        cameraLog()->error("connectCamera: driver '{}' did not stay connected (CONNECTION=DISCONNECT)", device);
        {
            std::lock_guard<std::mutex> lock(mtx_);
            device_.clear();
            connected_ = false;
        }
        return false;
    }

    connected_ = true;
    refreshInfoFromDevice(device);
    cameraLog()->info("connectCamera: driver '{}' is connected", device);
    return true;
}

std::string IndiCameraClient::activeDevice() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return device_;
}

void IndiCameraClient::refreshInfoFromDevice(const std::string& device)
{
    auto base = getDevice(device.c_str());
    if (!base.isValid())
        return;

    std::lock_guard<std::mutex> lock(mtx_);
    info_.name = device;

    auto ccdInfo = base.getNumber("CCD_INFO");
    if (ccdInfo.isValid()) {
        auto* n = static_cast<INumberVectorProperty*>(ccdInfo.getProperty());
        if (n) {
            for (int i = 0; i < n->nnp; ++i) {
                const auto& e = n->np[i];
                if (!strcmp(e.name, "CCD_MAX_X")) info_.width = static_cast<int>(e.value);
                else if (!strcmp(e.name, "CCD_MAX_Y")) info_.height = static_cast<int>(e.value);
                else if (!strcmp(e.name, "CCD_PIXEL_SIZE")) info_.pixel_size_um = e.value;
                else if (!strcmp(e.name, "CCD_PIXEL_SIZE_X")) info_.pixel_size_um = e.value;
                else if (!strcmp(e.name, "CCD_BITSPERPIXEL")) info_.bit_depth = static_cast<int>(e.value);
            }
            info_valid_ = (info_.width > 0 && info_.height > 0);
        }
    }

    auto filterNames = base.getText("FILTER_NAME");
    if (filterNames.isValid()) {
        auto* t = static_cast<ITextVectorProperty*>(filterNames.getProperty());
        if (t) {
            info_.filter_names.clear();
            for (int i = 0; i < t->ntp; ++i)
                info_.filter_names.emplace_back(t->tp[i].text ? t->tp[i].text : "");
        }
    }
}

void IndiCameraClient::disconnectFromServer()
{
    std::lock_guard<std::recursive_mutex> lock(connect_mutex_);
    if (isServerConnected())
        INDI::BaseClient::disconnectServer();
    connected_ = false;
}

void IndiCameraClient::ensureConnected()
{
    std::lock_guard<std::recursive_mutex> lock(connect_mutex_);
    const std::string device = activeDevice();

    try
    {
        if (!isServerConnected())
        {
            cameraLog()->info("ensureConnected: connecting to INDI server at {}:{}", host_, port_);
            if (!connectToServer())
                return;
        }

        if (!device.empty() && !connected_)
        {
            cameraLog()->info("ensureConnected: reconnecting device '{}'", device);
            connectCamera(device);
        }
    }
    catch (const std::exception& e)
    {
        cameraLog()->warn("ensureConnected failed: {}", e.what());
    }
}

bool IndiCameraClient::selectDevice(const std::string& device)
{
    std::lock_guard<std::recursive_mutex> lock(connect_mutex_);

    if (device.empty())
    {
        disconnectFromServer();
        return true;
    }

    if (!isServerConnected() && !connectToServer())
        return false;

    return connectCamera(device);
}

void IndiCameraClient::reconnectLoop()
{
    while (reconnect_running_)
    {
        for (int i = 0; i < 50 && reconnect_running_; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (!reconnect_running_)
            break;

        try
        {
            ensureConnected();
        }
        catch (...)
        {
        }
    }
}

bool IndiCameraClient::isReady() const
{
    return connected_ && info_valid_;
}

IndiCameraClient::Info IndiCameraClient::getInfo()
{
    std::lock_guard<std::mutex> lock(mtx_);
    return info_;
}

void IndiCameraClient::handlePropertyUpdate(INDI::Property property)
{
    if (!property.isValid())
        return;

    const char* name = property.getName();
    if (!name)
        return;

    // Diagnostic: trace CCD*/BLOB/CONNECTION/UPLOAD property updates so we can
    // see whether the CCD1 image blob actually arrives and in which state the
    // driver is during an exposure.
    const auto propType = property.getType();
    const bool isCcd = strlen(name) >= 3 && !strncmp(name, "CCD", 3);
    const bool isConn = !strcmp(name, "CONNECTION");
    const bool isUpload = !strcmp(name, "UPLOAD_MODE");
    if (propType == INDI_BLOB || isCcd || isConn || isUpload)
    {
        cameraLog()->info("handlePropertyUpdate: property '{}' type={} state={}",
                          name,
                          property.getTypeAsString() ? property.getTypeAsString() : "?",
                          property.getStateAsString() ? property.getStateAsString() : "?");
    }

    if (!strcmp(name, "CCD_INFO"))
    {
        auto* n = static_cast<INumberVectorProperty*>(property.getProperty());
        if (n)
        {
            std::lock_guard<std::mutex> lock(mtx_);
            for (int i = 0; i < n->nnp; ++i)
            {
                const auto& e = n->np[i];
                if (!strcmp(e.name, "CCD_MAX_X")) info_.width = static_cast<int>(e.value);
                else if (!strcmp(e.name, "CCD_MAX_Y")) info_.height = static_cast<int>(e.value);
                else if (!strcmp(e.name, "CCD_PIXEL_SIZE")) info_.pixel_size_um = e.value;
                else if (!strcmp(e.name, "CCD_PIXEL_SIZE_X")) info_.pixel_size_um = e.value;
                else if (!strcmp(e.name, "CCD_BITSPERPIXEL")) info_.bit_depth = static_cast<int>(e.value);
            }
            info_valid_ = (info_.width > 0 && info_.height > 0);
        }
    }
    else if (!strcmp(name, "FILTER_NAME"))
    {
        auto* t = static_cast<ITextVectorProperty*>(property.getProperty());
        if (t)
        {
            std::lock_guard<std::mutex> lock(mtx_);
            info_.filter_names.clear();
            for (int i = 0; i < t->ntp; ++i)
                info_.filter_names.emplace_back(t->tp[i].text ? t->tp[i].text : "");
        }
    }
    else if (!strcmp(name, "CCD_TEMPERATURE"))
    {
        auto* n = static_cast<INumberVectorProperty*>(property.getProperty());
        if (n && n->nnp > 0)
        {
            std::lock_guard<std::mutex> lock(mtx_);
            cooler_.current_c = n->np[0].value;
        }
    }
    else if (!strcmp(name, "CCD_COOLER"))
    {
        auto* s = static_cast<ISwitchVectorProperty*>(property.getProperty());
        if (s && s->sp)
        {
            std::lock_guard<std::mutex> lock(mtx_);
            for (int i = 0; i < s->nsp; ++i)
                if (s->sp[i].s == ISS_ON && !strcmp(s->sp[i].name, "COOLER_ON"))
                    cooler_.enabled = true;
        }
    }
    else if (!strcmp(name, "CCD1") && property.getType() == INDI_BLOB)
    {
        auto* b = static_cast<IBLOBVectorProperty*>(property.getProperty());
        if (b && b->bp && b->bp->blob && b->bp->bloblen > 0)
        {
            cameraLog()->info("handlePropertyUpdate: received CCD1 blob {} bytes (format '{}')",
                              b->bp->bloblen, b->bp->format ? b->bp->format : "");
            std::lock_guard<std::mutex> lock(mtx_);
            capture_result_.format = b->bp->format ? b->bp->format : "";
            capture_result_.bytes.assign(static_cast<const char*>(b->bp->blob),
                                         static_cast<size_t>(b->bp->bloblen));
            capture_result_.complete = true;
            capture_done_ = true;
        }
        else
        {
            cameraLog()->warn("handlePropertyUpdate: CCD1 blob update without data");
        }
        capture_cv_.notify_all();
    }
}

void IndiCameraClient::newDevice(INDI::BaseDevice device)
{
    if (!device_.empty() && device.getDeviceName() &&
        device_ == device.getDeviceName())
    {
        connected_ = true;
    }
}

void IndiCameraClient::removeDevice(INDI::BaseDevice)
{
    connected_ = false;
}

void IndiCameraClient::newProperty(INDI::Property property)
{
    handlePropertyUpdate(property);
}

void IndiCameraClient::updateProperty(INDI::Property property)
{
    handlePropertyUpdate(property);
}

void IndiCameraClient::removeProperty(INDI::Property)
{
}

void IndiCameraClient::newMessage(INDI::BaseDevice device, int)
{
    if (device.isValid())
    {
        const std::string& msg = device.lastMessage();
        if (!msg.empty())
            cameraLog()->warn("INDI driver message: {}", msg);
    }
}

void IndiCameraClient::serverConnected()
{
    cameraLog()->info("serverConnected: connected to INDI server");
}

void IndiCameraClient::serverDisconnected(int)
{
    connected_ = false;
    capture_cv_.notify_all();
}

IndiCameraClient::Exposure IndiCameraClient::capture(
    double duration_s, double timeout_s,
    const std::function<void(double)>& onProgress, int binning, double gain)
{
    if (device_.empty())
        throw std::runtime_error("IndiCameraClient: no camera selected");

    {
        std::lock_guard<std::mutex> lock(mtx_);
        capture_done_ = false;
        capture_result_ = Exposure{};
    }
    capture_abort_ = false;

    // Make sure the driver returns the image to this client (not to disk) and
    // that a light frame is captured. Without UPLOAD_CLIENT many drivers write
    // the FITS locally and never send a CCD1 blob, so the wait below times out.
    auto device = getDevice(device_.c_str());
    if (device.isValid())
    {
        if (device.getSwitch("UPLOAD_MODE").isValid())
            sendNewSwitch(device_.c_str(), "UPLOAD_MODE", "UPLOAD_CLIENT");
        if (device.getText("CCD_FRAME_TYPE").isValid())
            sendNewText(device_.c_str(), "CCD_FRAME_TYPE", "FRAME_LIGHT", nullptr);
        if (device.getNumber("CCD_BINNING").isValid())
        {
            sendNewNumber(device_.c_str(), "CCD_BINNING", "HOR_BIN", binning);
            sendNewNumber(device_.c_str(), "CCD_BINNING", "VER_BIN", binning);
        }
        if (device.getNumber("CCD_GAIN").isValid())
            sendNewNumber(device_.c_str(), "CCD_GAIN", "GAIN", gain);
    }

    cameraLog()->info("capture: starting {:.3f}s exposure (bin={}, gain={:.0f}) on '{}'",
                      duration_s, binning, gain, device_);
    sendNewNumber(device_.c_str(), "CCD_EXPOSURE", "CCD_EXPOSURE_VALUE", duration_s);

    const auto start = std::chrono::steady_clock::now();

    std::unique_lock<std::mutex> lock(mtx_);
    while (!capture_done_)
    {
        if (capture_abort_)
        {
            lock.unlock();
            sendNewSwitch(device_.c_str(), "CCD_ABORT_EXPOSURE", "ABORT");
            lock.lock();
            cameraLog()->info("capture: exposure aborted by operator");
            Exposure result;
            result.complete = false;
            result.aborted = true;
            return result;
        }

        if (capture_cv_.wait_for(lock, std::chrono::milliseconds(200),
                                 [this]() { return capture_done_; }))
        {
            break;
        }

        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeout_s)
        {
            cameraLog()->warn("capture: timed out after {:.1f}s waiting for CCD1 blob on '{}'",
                              elapsed, device_);
            lock.unlock();
            sendNewSwitch(device_.c_str(), "CCD_ABORT_EXPOSURE", "ABORT");
            lock.lock();
            throw std::runtime_error("IndiCameraClient: exposure timed out");
        }

        if (onProgress)
            onProgress(std::max(0.0, duration_s - elapsed));
    }

    return capture_result_;
}

void IndiCameraClient::abortCapture()
{
    capture_abort_ = true;
    capture_cv_.notify_all();
    try
    {
        sendNewSwitch(device_.c_str(), "CCD_ABORT_EXPOSURE", "ABORT");
    }
    catch (...)
    {
    }
}

void IndiCameraClient::setFilter(int position)
{
    if (device_.empty())
        throw std::runtime_error("IndiCameraClient: no camera selected");
    // INDI filter slots are 1-based.
    sendNewNumber(device_.c_str(), "FILTER_SLOT", "FILTER_SLOT_VALUE", position + 1);
}

void IndiCameraClient::setCooler(double target_c, bool enabled)
{
    if (device_.empty())
        throw std::runtime_error("IndiCameraClient: no camera selected");

    if (enabled)
        sendNewSwitch(device_.c_str(), "CCD_COOLER", "COOLER_ON");
    else
        sendNewSwitch(device_.c_str(), "CCD_COOLER", "COOLER_OFF");

    sendNewNumber(device_.c_str(), "CCD_TEMPERATURE", "CCD_TEMPERATURE_VALUE", target_c);

    std::lock_guard<std::mutex> lock(mtx_);
    cooler_.enabled = enabled;
    cooler_.target_c = target_c;
}

IndiCameraClient::Cooler IndiCameraClient::getCooler()
{
    std::lock_guard<std::mutex> lock(mtx_);
    return cooler_;
}

std::vector<IndiCameraClient::PropertyDesc> IndiCameraClient::listProperties()
{
    std::vector<PropertyDesc> out;
    if (device_.empty())
        return out;

    auto base = getDevice(device_.c_str());
    if (!base.isValid())
        return out;

    auto properties = base.getProperties();
    for (auto& property : properties)
    {
        if (!property.isValid())
            continue;

        PropertyDesc desc;
        desc.name = property.getName() ? property.getName() : "";
        desc.label = property.getLabel() ? property.getLabel() : desc.name;
        desc.group = property.getGroupName() ? property.getGroupName() : "";
        desc.writable = (property.getPermission() == IP_RW || property.getPermission() == IP_WO);

        switch (property.getType())
        {
        case INDI_NUMBER:
        {
            auto* n = static_cast<INumberVectorProperty*>(property.getProperty());
            if (!n) break;
            desc.type = "number";
            for (int i = 0; i < n->nnp; ++i)
            {
                PropertyElement e;
                e.name = n->np[i].name ? n->np[i].name : "";
                e.label = n->np[i].label ? n->np[i].label : e.name;
                e.number_value = n->np[i].value;
                e.number_min = n->np[i].min;
                e.number_max = n->np[i].max;
                e.number_step = n->np[i].step;
                desc.elements.push_back(std::move(e));
            }
            break;
        }
        case INDI_SWITCH:
        {
            auto* s = static_cast<ISwitchVectorProperty*>(property.getProperty());
            if (!s) break;
            desc.type = "switch";
            for (int i = 0; i < s->nsp; ++i)
            {
                PropertyElement e;
                e.name = s->sp[i].name ? s->sp[i].name : "";
                e.label = s->sp[i].label ? s->sp[i].label : e.name;
                e.switch_state = (s->sp[i].s == ISS_ON) ? "ON" : "OFF";
                desc.elements.push_back(std::move(e));
            }
            break;
        }
        case INDI_TEXT:
        {
            auto* t = static_cast<ITextVectorProperty*>(property.getProperty());
            if (!t) break;
            desc.type = "text";
            for (int i = 0; i < t->ntp; ++i)
            {
                PropertyElement e;
                e.name = t->tp[i].name ? t->tp[i].name : "";
                e.label = t->tp[i].label ? t->tp[i].label : e.name;
                e.text_value = t->tp[i].text ? t->tp[i].text : "";
                desc.elements.push_back(std::move(e));
            }
            break;
        }
        default:
            continue;
        }

        out.push_back(std::move(desc));
    }
    return out;
}

void IndiCameraClient::setProperty(const std::string& property, const std::string& element,
                                   const std::string& value)
{
    if (device_.empty())
        throw std::runtime_error("IndiCameraClient: no camera device selected");

    auto base = getDevice(device_.c_str());
    if (!base.isValid())
        throw std::runtime_error("IndiCameraClient: camera device not found");

    if (base.getNumber(property.c_str()).isValid())
    {
        sendNewNumber(device_.c_str(), property.c_str(), element.c_str(), std::stod(value));
        return;
    }
    if (base.getSwitch(property.c_str()).isValid())
    {
        sendNewSwitch(device_.c_str(), property.c_str(), element.c_str());
        return;
    }
    if (base.getText(property.c_str()).isValid())
    {
        sendNewText(device_.c_str(), property.c_str(), element.c_str(), value.c_str());
        return;
    }
    throw std::runtime_error("IndiCameraClient: unknown property: " + property);
}

} // namespace astro_camera
