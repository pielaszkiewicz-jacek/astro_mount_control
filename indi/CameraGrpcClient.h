#ifndef CAMERA_GRPC_CLIENT_H
#define CAMERA_GRPC_CLIENT_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief gRPC client wrapper for CameraService.
 *
 * Connects to the CameraService hosted in-process on the mount controller's
 * unified gRPC port (50051).
 *
 * The public API is deliberately protobuf-free (plain C++ structs).  This keeps
 * the INDI CCD driver translation unit free of generated protobuf headers —
 * INDI's <dsp.h> defines a `Max` macro that conflicts with absl when both are
 * included in the same translation unit.
 */
class CameraGrpcClient
{
public:
    struct CameraInfoData
    {
        std::string name;
        std::string manufacturer;
        std::string sensor_name;
        std::string driver_version;
        int width{0};
        int height{0};
        int max_bin{1};
        int num_filters{0};
        double pixel_size_um{0.0};
        double bit_depth{16.0};
        bool has_cooler{false};
        bool connected{false};
        std::vector<std::string> filter_names;
    };

    struct ExposureResult
    {
        bool complete{false};
        int width{0};
        int height{0};
        int bit_depth{16};
        std::string format;
        std::string image_bytes;
        double exposure_time_s{0.0};
        int gain{0};
        int binning{1};
    };

    struct CoolerStatusData
    {
        bool enabled{false};
        double target_c{0.0};
        double current_c{0.0};
        double power_percent{0.0};
        bool reached_target{false};
    };

    CameraGrpcClient(const std::string& host = "localhost", int port = 50051,
                     bool useSsl = false);
    ~CameraGrpcClient();

    // Move-only
    CameraGrpcClient(CameraGrpcClient&&) noexcept;
    CameraGrpcClient& operator=(CameraGrpcClient&&) noexcept;
    CameraGrpcClient(const CameraGrpcClient&) = delete;
    CameraGrpcClient& operator=(const CameraGrpcClient&) = delete;

    void connect();
    void disconnect();
    bool isConnected() const;
    void reconnect();

    CameraInfoData getCameraInfo();

    /** Start an exposure and block until completion; returns the final image. */
    ExposureResult startExposure(
        double exposure_time_s, int gain, int offset, int binning, int filter_position,
        const std::function<void(float remaining_s, double progress_percent)>& onProgress = {});

    void abortExposure();
    void setFilter(int position);
    CoolerStatusData setCooler(double target_c, bool enabled);
    CoolerStatusData getCoolerStatus();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif // CAMERA_GRPC_CLIENT_H
