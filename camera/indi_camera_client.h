#ifndef INDI_CAMERA_CLIENT_H
#define INDI_CAMERA_CLIENT_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <baseclient.h>
#include <basedevice.h>

namespace astro_camera {

/**
 * @brief INDI client bridge for a camera device (e.g. PlayerOne indi_playerone).
 *
 * Connects to a running indiserver, watches a CCD device, and exposes the
 * minimal camera operations needed by CameraServiceImpl: info, exposure
 * (image blob), abort, filter wheel, cooler.
 *
 * The public API is intentionally plain (no protobuf) so it can be used from
 * the controller's camera service without pulling generated headers into this
 * translation unit.
 */
class IndiCameraClient : public INDI::BaseClient
{
public:
    struct Info
    {
        std::string name;
        int width{0};
        int height{0};
        double pixel_size_um{0.0};
        int bit_depth{16};
        bool has_cooler{false};
        std::vector<std::string> filter_names;
    };

    struct Exposure
    {
        bool complete{false};
        bool aborted{false};  // operator aborted the exposure
        std::string format;   // e.g. "fits"
        std::string bytes;    // raw image bytes
    };

    struct Cooler
    {
        bool enabled{false};
        double target_c{0.0};
        double current_c{0.0};
        double power_percent{0.0};
        bool reached_target{false};
    };

    struct PropertyElement
    {
        std::string name;
        std::string label;
        double number_value{0.0};
        double number_min{0.0};
        double number_max{0.0};
        double number_step{0.0};
        std::string switch_state;  // "ON" / "OFF"
        std::string text_value;
    };

    struct PropertyDesc
    {
        std::string name;
        std::string label;
        std::string group;
        std::string type;          // "number" | "switch" | "text"
        bool writable{false};
        std::vector<PropertyElement> elements;
    };

    IndiCameraClient();
    ~IndiCameraClient() override;

    void configure(const std::string& host, int port);

    bool connectToServer();
    void disconnectFromServer();
    bool isReady() const;

    /** @return names of camera devices (CCD interface) on the INDI server. */
    std::vector<std::string> listCameras();

    /** Select/connect a specific INDI camera device. */
    bool connectCamera(const std::string& device);

    /** Ensure the INDI server and the last selected device are connected.
     *  Idempotent and safe to call from any thread. */
    void ensureConnected();

    /** Select and connect a camera device (empty string disconnects). */
    bool selectDevice(const std::string& device);

    /** @return the currently selected device name (empty if none). */
    std::string activeDevice() const;

    Info getInfo();

    /** Start an exposure and block until the CCD1 blob arrives (or timeout). */
    Exposure capture(double duration_s, double timeout_s,
                     const std::function<void(double remaining_s)>& onProgress = {},
                     int binning = 1, double gain = 0.0);

    void abortCapture();

    void setFilter(int position);          // 0-based
    void setCooler(double target_c, bool enabled);
    Cooler getCooler();

    /** @return all INDI properties of the active camera device. */
    std::vector<PropertyDesc> listProperties();

    /** Set an INDI property element (number/switch/text) on the active device. */
    void setProperty(const std::string& property, const std::string& element,
                     const std::string& value);

protected:
    // INDI::BaseMediator callbacks (INDI 2.x signatures).
    void newDevice(INDI::BaseDevice device) override;
    void removeDevice(INDI::BaseDevice device) override;
    void newProperty(INDI::Property property) override;
    void updateProperty(INDI::Property property) override;
    void removeProperty(INDI::Property property) override;
    void newMessage(INDI::BaseDevice device, int messageID) override;
    void serverConnected() override;
    void serverDisconnected(int exit_code) override;

private:
    void handlePropertyUpdate(INDI::Property property);
    void refreshInfoFromDevice(const std::string& device);
    void reconnectLoop();

    std::string host_{"localhost"};
    int port_{7624};
    std::string device_;

    std::atomic<bool> connected_{false};
    std::atomic<bool> capture_abort_{false};

    std::atomic<bool> reconnect_running_{false};
    std::recursive_mutex connect_mutex_;
    std::thread reconnect_thread_;

    mutable std::mutex mtx_;
    std::condition_variable capture_cv_;

    Info info_;
    bool info_valid_{false};

    bool capture_done_{false};
    Exposure capture_result_;

    Cooler cooler_;
};

} // namespace astro_camera

#endif // INDI_CAMERA_CLIENT_H
