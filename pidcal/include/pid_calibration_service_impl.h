#ifndef PID_CALIBRATION_SERVICE_IMPL_H
#define PID_CALIBRATION_SERVICE_IMPL_H

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include <grpcpp/grpcpp.h>
#include "proto/pid_calibration.grpc.pb.h"

namespace astro_mount {
namespace controllers {
class MountController;
}
} // namespace astro_mount

/**
 * @brief gRPC implementation of the PidCalibrationService.
 *
 * Hosted in-process on the unified gRPC port (50051).  The service runs a
 * calibration session on a background thread: for every speed point and every
 * candidate value of the selected coefficient it writes the PID gains to the
 * drive RAM (via MountController::setAxisPid → MotorControl::writePidLoopRam),
 * commands the axis to move, samples the actual velocity, and computes the
 * arithmetic mean and standard deviation.  The best combination minimises:
 *
 *     score = |mean_speed − target_speed| + stddev_speed
 */
namespace astro_pidcal {

class PidCalibrationServiceImpl final
    : public astro_mount::PidCalibrationService::Service {
public:
    explicit PidCalibrationServiceImpl(
        astro_mount::controllers::MountController& controller);
    ~PidCalibrationServiceImpl() override;

    grpc::Status StartCalibration(
        grpc::ServerContext* context,
        const astro_mount::PidCalibrationRequest* request,
        astro_mount::PidCalibrationStartResponse* response) override;

    grpc::Status StopCalibration(
        grpc::ServerContext* context,
        const google::protobuf::Empty* request,
        google::protobuf::Empty* response) override;

    grpc::Status GetCalibrationStatus(
        grpc::ServerContext* context,
        const google::protobuf::Empty* request,
        astro_mount::PidCalibrationStatus* response) override;

    grpc::Status SaveCalibrationResults(
        grpc::ServerContext* context,
        const astro_mount::PidCalibrationSaveRequest* request,
        astro_mount::PidCalibrationSaveResponse* response) override;

private:
    // Runs the calibration session on the worker thread.
    void runCalibration(const astro_mount::PidCalibrationRequest& request);

    // Builds the ordered list of speed points / coefficient values to sweep.
    static std::vector<double> buildSweepValues(double min_v, double max_v,
                                                double step);

    // Statistics helper: fills mean / stddev / sample count from samples.
    static void computeStatistics(const std::vector<double>& samples,
                                  double& mean, double& stddev,
                                  int& sample_count);

    // Serialise the current result to a JSON file.  Returns empty path on
    // failure (the error is placed in `error`).
    static std::string saveResultToFile(
        const astro_mount::PidCalibrationResult& result,
        const std::string& requested_path, std::string& error);

    astro_mount::controllers::MountController& controller_;

    mutable std::mutex mtx_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};

    astro_mount::PidCalibrationStatus status_;
    astro_mount::PidCalibrationResult result_;
    std::string last_saved_file_;
};

} // namespace astro_pidcal

#endif // PID_CALIBRATION_SERVICE_IMPL_H
