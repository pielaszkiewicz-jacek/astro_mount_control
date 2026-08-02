#include "hal/camera_control.h"
#include <random>
#include <cmath>
#include <thread>
#include <chrono>
#include <algorithm>

namespace astro_mount {
namespace hal {

/**
 * @brief Simulated camera for testing
 *
 * Generates synthetic images with configurable:
 * - Star field (random positions, brightness)
 * - Noise (Gaussian read noise, Poisson shot noise)
 * - HFD (simulated defocus for auto-focus testing)
 * - Temperature response
 */
class SimulatedCamera : public CameraControl {
public:
    SimulatedCamera() = default;

    std::vector<std::string> listCameras() override {
        return {"simulated_camera_0"};
    }

    bool connect(const std::string& camera_id) override {
        connected_ = true;
        info_.name = "Simulated Camera";
        info_.manufacturer = "Simulation";
        info_.sensor_name = "IMX571 Sim";
        info_.width = 6248;
        info_.height = 4176;
        info_.pixel_size_um = 3.76;
        info_.max_bin = 4;
        info_.has_filter_wheel = true;
        info_.num_filters = 5;
        info_.filter_names = {"R", "G", "B", "L", "Ha"};
        info_.e_ad = 0.5;
        info_.read_noise_e = 1.5;
        info_.full_well_capacity_e = 50000;
        info_.bit_depth = 16;
        info_.has_cooler = true;
        info_.min_cooling_c = 35.0;
        info_.connected = true;
        return true;
    }

    void disconnect() override {
        connected_ = false;
        exposing_ = false;
        video_active_ = false;
    }

    bool isConnected() const override { return connected_; }

    CameraInfo getCameraInfo() override { return info_; }

    bool startExposure(const ExposureConfig& config) override {
        if (!connected_) return false;
        config_ = config;
        exposing_ = true;
        exposure_start_ = std::chrono::steady_clock::now();
        return true;
    }

    void abortExposure() override {
        exposing_ = false;
    }

    ImageData downloadImage(std::chrono::seconds timeout) override {
        if (!exposing_) return {};

        // Simulate exposure time
        auto elapsed = std::chrono::steady_clock::now() - exposure_start_;
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::milliseconds(static_cast<int>(config_.exposure_time_s * 1000)) - elapsed);

        if (remaining.count() > 0 && remaining < std::chrono::duration_cast<std::chrono::milliseconds>(timeout)) {
            std::this_thread::sleep_for(remaining);
        }

        exposing_ = false;
        return generateImage();
    }

    bool isExposing() const override { return exposing_; }

    double getExposureProgress() const override {
        if (!exposing_) return 0.0;
        auto elapsed = std::chrono::steady_clock::now() - exposure_start_;
        double progress = elapsed.count() / (config_.exposure_time_s * 1'000'000'000.0);
        return std::min(progress, 1.0);
    }

    bool setCoolerTarget(double target_c) override {
        cooler_target_ = target_c;
        return true;
    }

    double getSensorTemperature() override {
        // Simulate cooling towards target
        current_temp_ += (cooler_target_ - current_temp_) * 0.01;
        return current_temp_;
    }

    double getCoolerPower() override {
        double diff = std::abs(current_temp_ - cooler_target_);
        return std::min(diff * 10.0, 100.0);
    }

    bool hasFilterWheel() const override { return true; }

    bool setFilterPosition(int32_t position) override {
        if (position >= 0 && position < info_.num_filters) {
            current_filter_ = position;
            return true;
        }
        return false;
    }

    int32_t getFilterPosition() override { return current_filter_; }

    int32_t getFilterCount() override { return info_.num_filters; }

    std::vector<std::string> getFilterNames() override { return info_.filter_names; }

    bool startVideo(const ExposureConfig& config) override {
        if (!connected_) return false;
        video_config_ = config;
        video_active_ = true;
        video_thread_ = std::make_unique<std::thread>([this]() { videoLoop(); });
        return true;
    }

    void stopVideo() override {
        video_active_ = false;
        if (video_thread_ && video_thread_->joinable()) {
            video_thread_->join();
        }
        video_thread_.reset();
    }

    ImageData getVideoFrame() override {
        return generateImage();
    }

    bool isVideoActive() const override { return video_active_; }

private:
    void videoLoop() {
        while (video_active_) {
            auto frame = generateImage();
            if (frame_callback_) {
                frame_callback_(frame);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    ImageData generateImage() {
        static std::default_random_engine gen(std::random_device{}());

        int w = info_.width / config_.binning;
        int h = info_.height / config_.binning;

        ImageData img;
        img.width = w;
        img.height = h;
        img.bit_depth = 16;
        img.pixels.resize(w * h, 0);

        // Background sky level
        double sky_level = 500.0 + 200.0 * (current_hfd_ / 5.0);

        // Add stars
        std::poisson_distribution<int> star_count(50);
        int n_stars = star_count(gen);
        img.star_count = n_stars;

        for (int s = 0; s < n_stars; ++s) {
            std::uniform_int_distribution<int> x_dist(0, w - 1);
            std::uniform_int_distribution<int> y_dist(0, h - 1);
            std::normal_distribution<double> brightness(3000.0, 1000.0);

            int sx = x_dist(gen);
            int sy = y_dist(gen);
            double intensity = std::max(brightness(gen), 100.0);
            double hfd = current_hfd_ * (1.0 + 0.2 * std::normal_distribution<double>(0, 1)(gen));

            // Gaussian PSF
            for (int dy = -5; dy <= 5; ++dy) {
                for (int dx = -5; dx <= 5; ++dx) {
                    int px = sx + dx;
                    int py = sy + dy;
                    if (px >= 0 && px < w && py >= 0 && py < h) {
                        double r2 = dx * dx + dy * dy;
                        double star_val = intensity * std::exp(-r2 / (2.0 * hfd * hfd));
                        img.pixels[py * w + px] += static_cast<uint16_t>(star_val);
                    }
                }
            }
        }

        // Add noise and sky background
        std::normal_distribution<double> noise(0.0, 50.0);
        for (auto& pixel : img.pixels) {
            double val = sky_level + noise(gen);
            pixel = static_cast<uint16_t>(std::clamp(val, 0.0, 65535.0));
        }

        // Calculate statistics
        double sum = 0, sum2 = 0;
        for (const auto& p : img.pixels) {
            sum += p;
            sum2 += p * p;
        }
        img.mean_adu = sum / img.pixels.size();
        img.std_dev_adu = std::sqrt(sum2 / img.pixels.size() - img.mean_adu * img.mean_adu);

        // Simulate HFD measurement
        img.hfd = current_hfd_;

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
    double current_hfd_{3.0};  // Default HFD of 3 pixels
    std::unique_ptr<std::thread> video_thread_;
};

} // namespace hal
} // namespace astro_mount
