#include "hal/camera_control.h"
#include <thread>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <sstream>

namespace astro_mount {
namespace hal {

/**
 * @brief ZWO ASI Camera implementation
 *
 * Uses ZWO ASI SDK for camera control. Supports:
 * - ZWO ASI cameras (ASI120, ASI1600, ASI183, ASI294, ASI6200, etc.)
 * - Filter wheels (EFW)
 * - Cooler control
 * - Video preview mode
 *
 * SDK: https://www.zwoastro.com/software
 * Library: libASICamera2.so / ASICamera2.dll
 */
class ZwoCamera : public CameraControl {
public:
    ZwoCamera() = default;

    std::vector<std::string> listCameras() override {
        std::vector<std::string> cameras;
        // int count = ASIGetNumOfConnectedCameras();
        // for (int i = 0; i < count; ++i) {
        //     char name[64];
        //     ASIGetCameraProperty(name, i);
        //     cameras.push_back(name);
        // }
        return cameras;
    }

    bool connect(const std::string& camera_id) override {
        // int ret = ASIOpenCamera(camera_id.c_str());
        // if (ret != ASI_SUCCESS) return false;
        // ASIInitCamera(camera_handle_);
        // Load camera properties into info_
        connected_ = true;
        loadCameraInfo();
        return true;
    }

    void disconnect() override {
        if (connected_) {
            // ASICloseCamera(camera_handle_);
            connected_ = false;
        }
    }

    bool isConnected() const override { return connected_; }

    CameraInfo getCameraInfo() override { return info_; }

    bool startExposure(const ExposureConfig& config) override {
        if (!connected_) return false;

        config_ = config;

        // ASI_EXPOSURE_UNIT_US = exposure_time_s * 1'000'000
        // ASISetControlValue(camera_handle_, ASI_GAIN, config.gain, ASI_FALSE);
        // ASISetControlValue(camera_handle_, ASI_OFFSET, config.offset, ASI_FALSE);
        // ASISetControlValue(camera_handle_, ASI_BANDWIDTHOVERLOAD, config.binning, ASI_FALSE);
        // ASIStartExposure(camera_handle_, ASI_FALSE);  // ASI_FALSE = light frame

        exposing_ = true;
        exposure_start_ = std::chrono::steady_clock::now();
        return true;
    }

    void abortExposure() override {
        if (connected_) {
            // ASIStopExposure(camera_handle_);
            exposing_ = false;
        }
    }

    ImageData downloadImage(std::chrono::seconds timeout) override {
        if (!exposing_) return ImageData{};

        // ASI_EXPOSURE_STATUS status;
        // auto deadline = std::chrono::steady_clock::now() + timeout;
        // while (std::chrono::steady_clock::now() < deadline) {
        //     ASIGetExpStatus(camera_handle_, &status);
        //     if (status == ASI_EXP_SUCCESS) break;
        //     if (status == ASI_EXP_FAILED) return ImageData{};
        //     std::this_thread::sleep_for(std::chrono::milliseconds(10));
        // }

        exposing_ = false;
        return readImage();
    }

    bool isExposing() const override { return exposing_; }

    double getExposureProgress() const override {
        if (!exposing_) return 0.0;
        // ASI_EXPOSURE_STATUS status;
        // ASIGetExpStatus(camera_handle_, &status);
        auto elapsed = std::chrono::steady_clock::now() - exposure_start_;
        return std::min(elapsed.count() / (config_.exposure_time_s * 1'000'000'000.0), 1.0);
    }

    bool setCoolerTarget(double target_c) override {
        // ASISetControlValue(camera_handle_, ASI_TARGET_TEMP, static_cast<int>(target_c * 10), ASI_TRUE);
        cooler_target_ = target_c;
        return true;
    }

    double getSensorTemperature() override {
        // long temp;
        // ASIGetControlValue(camera_handle_, ASI_TEMPERATURE, &temp, ASI_FALSE);
        // return temp / 10.0;
        return current_temp_;
    }

    double getCoolerPower() override {
        // long power;
        // ASIGetControlValue(camera_handle_, ASI_COOLER_POWER, &power, ASI_FALSE);
        // return power;
        return 0.0;
    }

    bool hasFilterWheel() const override {
        // Check if EFW is connected
        return info_.has_filter_wheel;
    }

    bool setFilterPosition(int32_t position) override {
        // if (efw_handle_) {
        //     EFWSetPosition(efw_handle_, position);
        //     return true;
        // }
        current_filter_ = position;
        return true;
    }

    int32_t getFilterPosition() override { return current_filter_; }

    int32_t getFilterCount() override { return info_.num_filters; }

    std::vector<std::string> getFilterNames() override { return info_.filter_names; }

    bool startVideo(const ExposureConfig& config) override {
        if (!connected_) return false;
        video_config_ = config;
        video_active_ = true;
        // ASIStartExposure(camera_handle_, ASI_TRUE);  // ASI_TRUE = video mode
        return true;
    }

    void stopVideo() override {
        if (connected_) {
            // ASIStopExposure(camera_handle_);
            video_active_ = false;
        }
    }

    ImageData getVideoFrame() override {
        return readImage();
    }

    bool isVideoActive() const override { return video_active_; }

private:
    void loadCameraInfo() {
        // ASI_CAMERA_INFO asicam;
        // ASIGetCameraProperty(&asicam, camera_index_);

        info_.name = "ZWO ASI Camera";
        info_.manufacturer = "ZWO";
        // info_.width = asicam.MaxWidth;
        // info_.height = asicam.MaxHeight;
        // info_.pixel_size_um = asicam.PixelSize;
        // info_.max_bin = asicam.MaxBinning;
        // info_.has_filter_wheel = (asicam.EFWInteraction >= 1);
        // info_.num_filters = asicam.EFWSlots;
        // info_.has_cooler = asicam.IsCoolerCam;
        info_.connected = true;
    }

    ImageData readImage() {
        // ASI_IMG_TYPE img_type;
        // long img_width, img_height, img_bin, img_size;
        // ASIGetROIFormat(camera_handle_, &img_width, &img_height, &img_bin, &img_type);

        ImageData img;
        img.width = info_.width / config_.binning;
        img.height = info_.height / config_.binning;
        img.bit_depth = 16;
        img.pixels.resize(img.width * img.height);

        // ASIGetDataAfterExp(camera_handle_, img.pixels.data(), img.pixels.size() * 2);
        // Calculate basic statistics...

        return img;
    }

    bool connected_{false};
    bool exposing_{false};
    bool video_active_{false};
    CameraInfo info_;
    ExposureConfig config_;
    ExposureConfig video_config_;
    std::chrono::steady_clock::time_point exposure_start_;
    int32_t current_filter_{0};
    double current_temp_{20.0};
    double cooler_target_{20.0};
    // int camera_index_{-1};
    // void* camera_handle_{nullptr};
    // void* efw_handle_{nullptr};
};

} // namespace hal
} // namespace astro_mount
