#ifndef CAMERA_CONTROL_H
#define CAMERA_CONTROL_H

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <chrono>

namespace astro_mount {
namespace hal {

/**
 * @brief Camera sensor information
 */
struct CameraInfo {
    std::string name;
    std::string manufacturer;
    std::string sensor_name;
    int32_t width{0};
    int32_t height{0};
    double pixel_size_um{0.0};
    int32_t max_bin{1};
    bool has_filter_wheel{false};
    int32_t num_filters{0};
    std::vector<std::string> filter_names;
    double e_ad{1.0};
    double read_noise_e{0.0};
    double full_well_capacity_e{0.0};
    double bit_depth{16.0};
    bool has_cooler{false};
    double min_cooling_c{0.0};
    bool connected{false};
};

/**
 * @brief Camera exposure configuration
 */
struct ExposureConfig {
    double exposure_time_s{1.0};
    int32_t gain{0};
    int32_t offset{0};
    int32_t binning{1};
    int32_t roi_x{0};
    int32_t roi_y{0};
    int32_t roi_width{0};    // 0 = full
    int32_t roi_height{0};   // 0 = full
    bool save_to_disk{false};
    std::string save_path;
};

/**
 * @brief Image data and statistics
 */
struct ImageData {
    int32_t width{0};
    int32_t height{0};
    int32_t bit_depth{16};
    std::vector<uint16_t> pixels;           // Raw pixel data (16-bit)
    double exposure_time_s{0.0};
    int32_t gain{0};
    int32_t binning{1};
    double temperature_c{20.0};
    std::chrono::system_clock::time_point timestamp;

    // Statistics
    double mean_adu{0.0};
    double median_adu{0.0};
    double std_dev_adu{0.0};
    double min_adu{0.0};
    double max_adu{0.0};
    double hfd{0.0};                        // Half Flux Diameter
    int32_t star_count{0};
};

/**
 * @brief Abstract camera HAL interface
 *
 * Supports ZWO ASI cameras via SDK. Can be extended for QHY, Altair, etc.
 *
 * Camera lifecycle:
 *   listCameras() → connect(id) → getCameraInfo() → startExposure()/abort()
 *   → setFilter() → startVideo() → disconnect()
 */
class CameraControl {
public:
    virtual ~CameraControl() = default;

    /// @brief List available camera IDs
    virtual std::vector<std::string> listCameras() = 0;

    /// @brief Connect to camera by ID
    virtual bool connect(const std::string& camera_id) = 0;

    /// @brief Disconnect camera
    virtual void disconnect() = 0;

    /// @brief Check if camera is connected
    virtual bool isConnected() const = 0;

    /// @brief Get camera information
    virtual CameraInfo getCameraInfo() = 0;

    /// @brief Start exposure (non-blocking)
    /// @return true if exposure started
    virtual bool startExposure(const ExposureConfig& config) = 0;

    /// @brief Abort current exposure
    virtual void abortExposure() = 0;

    /// @brief Wait for exposure to complete and download image
    /// @param timeout Maximum wait time
    /// @return Image data, or empty if timeout/error
    virtual ImageData downloadImage(std::chrono::seconds timeout = std::chrono::seconds(60)) = 0;

    /// @brief Check if exposure is in progress
    virtual bool isExposing() const = 0;

    /// @brief Get exposure progress (0.0 - 1.0)
    virtual double getExposureProgress() const = 0;

    /// @brief Set cooler target temperature
    /// @param target_c Target temperature [°C]
    virtual bool setCoolerTarget(double target_c) = 0;

    /// @brief Get current sensor temperature
    virtual double getSensorTemperature() = 0;

    /// @brief Get cooler power level (0-100%)
    virtual double getCoolerPower() = 0;

    // ─── Filter Wheel ──────────────────────────────────────────────────────

    /// @brief Check if camera has filter wheel
    virtual bool hasFilterWheel() const = 0;

    /// @brief Set filter position (0-based)
    virtual bool setFilterPosition(int32_t position) = 0;

    /// @brief Get current filter position
    virtual int32_t getFilterPosition() = 0;

    /// @brief Get number of filter slots
    virtual int32_t getFilterCount() = 0;

    /// @brief Get filter names
    virtual std::vector<std::string> getFilterNames() = 0;

    // ─── Video / Preview ───────────────────────────────────────────────────

    /// @brief Start live video mode
    virtual bool startVideo(const ExposureConfig& config) = 0;

    /// @brief Stop live video mode
    virtual void stopVideo() = 0;

    /// @brief Get next video frame (blocking)
    virtual ImageData getVideoFrame() = 0;

    /// @brief Check if video mode is active
    virtual bool isVideoActive() const = 0;

    /// @brief Set callback for new video frames
    virtual void setFrameCallback(std::function<void(const ImageData&)> callback);

protected:
    std::function<void(const ImageData&)> frame_callback_;
};

} // namespace hal
} // namespace astro_mount

#endif // CAMERA_CONTROL_H
