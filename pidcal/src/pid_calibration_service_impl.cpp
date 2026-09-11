#include "pidcal/include/pid_calibration_service_impl.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "controllers/mount_controller.h"
#include "logging/logger.h"

namespace astro_pidcal {

using json = nlohmann::json;
using namespace astro_mount;

namespace {

int64_t nowUnixMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
               system_clock::now().time_since_epoch())
        .count();
}

} // namespace

PidCalibrationServiceImpl::PidCalibrationServiceImpl(
    astro_mount::controllers::MountController& controller)
    : controller_(controller) {
    status_.set_state("IDLE");
    status_.set_progress_percent(0.0);
}

PidCalibrationServiceImpl::~PidCalibrationServiceImpl() {
    stop_requested_ = true;
    if (worker_.joinable()) {
        worker_.join();
    }
}

grpc::Status PidCalibrationServiceImpl::StartCalibration(
    grpc::ServerContext*,
    const astro_mount::PidCalibrationRequest* request,
    astro_mount::PidCalibrationStartResponse* response) {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (running_) {
            response->set_started(false);
            response->set_message("Calibration already running");
            return grpc::Status::OK;
        }
        if (worker_.joinable()) {
            worker_.join();
        }
        running_ = true;
        stop_requested_ = false;

        status_.Clear();
        result_.Clear();
        status_.set_state("RUNNING");
        status_.set_running(true);
        status_.set_progress_percent(0.0);
        status_.set_tested_combinations(0);
        status_.set_total_combinations(0);
        status_.set_message("");
    }

    astro_mount::PidCalibrationRequest req = *request;
    worker_ = std::thread(&PidCalibrationServiceImpl::runCalibration, this, req);

    response->set_started(true);
    response->set_message("Calibration started");
    return grpc::Status::OK;
}

grpc::Status PidCalibrationServiceImpl::StopCalibration(
    grpc::ServerContext*, const google::protobuf::Empty*,
    google::protobuf::Empty*) {
    stop_requested_ = true;
    return grpc::Status::OK;
}

grpc::Status PidCalibrationServiceImpl::GetCalibrationStatus(
    grpc::ServerContext*, const google::protobuf::Empty*,
    astro_mount::PidCalibrationStatus* response) {
    std::lock_guard<std::mutex> lock(mtx_);
    *response = status_;
    return grpc::Status::OK;
}

grpc::Status PidCalibrationServiceImpl::SaveCalibrationResults(
    grpc::ServerContext*, const astro_mount::PidCalibrationSaveRequest* request,
    astro_mount::PidCalibrationSaveResponse* response) {
    astro_mount::PidCalibrationResult result;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        result = result_;
    }

    std::string error;
    std::string path = saveResultToFile(result, request->file_path(), error);
    if (path.empty()) {
        response->set_saved(false);
        response->set_message(error);
        return grpc::Status::OK;
    }

    {
        std::lock_guard<std::mutex> lock(mtx_);
        last_saved_file_ = path;
    }

    response->set_saved(true);
    response->set_file_path(path);
    response->set_message("Calibration results saved");
    return grpc::Status::OK;
}

std::vector<double> PidCalibrationServiceImpl::buildSweepValues(
    double min_v, double max_v, double step) {
    std::vector<double> values;
    if (min_v > max_v) {
        std::swap(min_v, max_v);
    }
    if (step <= 0.0) {
        values.push_back(min_v);
        return values;
    }

    // Include both endpoints even when the range is not an exact multiple of
    // the step.  A small epsilon avoids floating-point round-off dropping the
    // final point.
    const double eps = step * 1e-9;
    for (double v = min_v; v <= max_v + eps; v += step) {
        values.push_back(v);
        if (v > max_v - eps) {
            break;
        }
    }
    if (values.empty() || values.back() < max_v - eps) {
        values.push_back(max_v);
    }
    return values;
}

void PidCalibrationServiceImpl::computeStatistics(
    const std::vector<double>& samples, double& mean, double& stddev,
    int& sample_count) {
    sample_count = static_cast<int>(samples.size());
    mean = 0.0;
    stddev = 0.0;
    if (samples.empty()) {
        return;
    }

    double sum = 0.0;
    for (double s : samples) {
        sum += s;
    }
    mean = sum / static_cast<double>(samples.size());

    if (samples.size() > 1) {
        double sq = 0.0;
        for (double s : samples) {
            double d = s - mean;
            sq += d * d;
        }
        // Sample standard deviation (n − 1).
        stddev = std::sqrt(sq / static_cast<double>(samples.size() - 1));
    }
}

void PidCalibrationServiceImpl::runCalibration(
    const astro_mount::PidCalibrationRequest& request) {
    const int axis = request.axis_id();
    const int loop = request.loop();
    const int coefficient = request.coefficient();

    result_.set_axis_id(axis);
    result_.set_loop(static_cast<PidCalibrationLoop>(loop));
    result_.set_coefficient(
        static_cast<PidCalibrationCoefficient>(coefficient));
    result_.set_started_at_unix_ms(nowUnixMs());

    std::vector<double> speeds = buildSweepValues(
        request.min_speed_dps(), request.max_speed_dps(),
        request.speed_step_dps());

    // Build the ordered list of (Kp, Ki, Kd) gain combinations to test.
    // In single-coefficient mode only the selected coefficient is swept and
    // the other two are held at their base values; in PID_COEFF_ALL mode the
    // full Cartesian product of the three ranges is swept.
    struct GainCombo {
        double kp;
        double ki;
        double kd;
    };
    std::vector<GainCombo> combos;

    if (coefficient == PID_COEFF_ALL) {
        std::vector<double> kp_values = buildSweepValues(
            request.min_kp(), request.max_kp(), request.kp_step());
        std::vector<double> ki_values = buildSweepValues(
            request.min_ki(), request.max_ki(), request.ki_step());
        std::vector<double> kd_values = buildSweepValues(
            request.min_kd(), request.max_kd(), request.kd_step());
        for (double kp : kp_values) {
            for (double ki : ki_values) {
                for (double kd : kd_values) {
                    combos.push_back({kp, ki, kd});
                }
            }
        }
    } else {
        double c_min = 0.0, c_max = 0.0, c_step = 0.0;
        switch (coefficient) {
            case PID_COEFF_KP:
                c_min = request.min_kp(); c_max = request.max_kp();
                c_step = request.kp_step(); break;
            case PID_COEFF_KI:
                c_min = request.min_ki(); c_max = request.max_ki();
                c_step = request.ki_step(); break;
            case PID_COEFF_KD:
                c_min = request.min_kd(); c_max = request.max_kd();
                c_step = request.kd_step(); break;
            default:
                c_min = 0.0; c_max = 0.0; c_step = 0.0; break;
        }
        std::vector<double> coeff_values = buildSweepValues(c_min, c_max, c_step);
        for (double value : coeff_values) {
            double kp = request.base_kp();
            double ki = request.base_ki();
            double kd = request.base_kd();
            switch (coefficient) {
                case PID_COEFF_KP: kp = value; break;
                case PID_COEFF_KI: ki = value; break;
                case PID_COEFF_KD: kd = value; break;
                default: break;
            }
            combos.push_back({kp, ki, kd});
        }
    }

    const int total = static_cast<int>(speeds.size() * combos.size());
    {
        std::lock_guard<std::mutex> lock(mtx_);
        status_.set_total_combinations(total);
        status_.set_message("Preparing calibration");
    }

    astro_mount::logging::Logger::get("pidcal")
        ->info("PID calibration started: axis={} loop={} coefficient={} combinations={}",
               axis, loop, coefficient, total);

    if (coefficient == PID_COEFF_KD) {
        astro_mount::logging::Logger::get("pidcal")
            ->warn("Kd calibration requested, but the MF7025v2 0x31 RAM write "
                   "does not carry Kd — Kd sweeps will not change the drive gains");
    }

    controller_.enableAxis(axis);

    std::vector<astro_mount::PidCalibrationPoint> winners;
    std::vector<astro_mount::PidCalibrationPoint> all_points;

    const double settle_time = request.settle_time_s() > 0.0
                                   ? request.settle_time_s()
                                   : 0.3;
    const double measure_time = request.measurement_time_s() > 0.0
                                    ? request.measurement_time_s()
                                    : 1.0;
    const double acceleration = 2.0;   // deg/s²
    const int tested_axis = axis;

    for (double speed : speeds) {
        if (stop_requested_) {
            break;
        }

        astro_mount::PidCalibrationPoint speed_winner;
        bool have_winner = false;

        for (const GainCombo& combo : combos) {
            if (stop_requested_) {
                break;
            }

            const double kp = combo.kp;
            const double ki = combo.ki;
            const double kd = combo.kd;

            // When calibrating the speed or position loop, first write the
            // user-specified fixed current-loop gains so the current loop is
            // NOT modified by the sweep.  0x31 overwrites all three loops at
            // once, so this must be written before the selected loop's gains.
            if (loop != PID_LOOP_CURRENT) {
                controller_.setAxisPid(tested_axis, PID_LOOP_CURRENT,
                                       request.current_kp(), request.current_ki(),
                                       0.0);
            }

            controller_.setAxisPid(tested_axis, loop, kp, ki, kd);

            // Command motion using a position profile (0xA4) with a far
            // relative target for every loop.  The drive ramps to the profile
            // velocity and holds it during the measurement window, which is
            // exactly what the speed sampling needs.  Position control is used
            // even for the speed/current loops because the MF7025v2 0xA2
            // velocity command is unreliable on some firmware revisions (it is
            // acknowledged but the shaft does not rotate).
            const double direction = speed >= 0.0 ? 1.0 : -1.0;
            const double distance = std::abs(speed) *
                                        (settle_time + measure_time + 0.5) +
                                    2.0;
            const bool motion_ok = controller_.controlAxis(
                tested_axis, 0 /*position*/, direction * distance,
                std::abs(speed), acceleration, true /*relative*/);

            astro_mount::logging::Logger::get("pidcal")
                ->info("PID calibration: axis={} speed={:.2f} °/s -> motion command {}",
                       tested_axis, speed, motion_ok ? "OK" : "FAILED");

            if (settle_time > 0.0) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(
                        static_cast<long>(settle_time * 1000.0)));
            }

            // ── Sample actual velocity ────────────────────────────────
            std::vector<double> samples;
            auto t0 = std::chrono::steady_clock::now();
            double elapsed = 0.0;
            while (elapsed < measure_time) {
                if (stop_requested_) {
                    break;
                }
                samples.push_back(controller_.getAxisVelocity(tested_axis));
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                elapsed = std::chrono::duration<double>(
                              std::chrono::steady_clock::now() - t0)
                              .count();
            }

            double mean = 0.0, stddev = 0.0;
            int sample_count = 0;
            computeStatistics(samples, mean, stddev, sample_count);
            double score = std::abs(mean - speed) + stddev;

            astro_mount::PidCalibrationPoint point;
            point.set_kp(kp);
            point.set_ki(ki);
            point.set_kd(kd);
            point.set_target_speed_dps(speed);
            point.set_mean_speed_dps(mean);
            point.set_stddev_speed_dps(stddev);
            point.set_score(score);
            point.set_sample_count(sample_count);

            all_points.push_back(point);

            if (!have_winner || point.score() < speed_winner.score()) {
                speed_winner = point;
                have_winner = true;
            }

            {
                std::lock_guard<std::mutex> lock(mtx_);
                *result_.add_points() = point;
                status_.set_tested_combinations(
                    static_cast<int>(all_points.size()));
                if (total > 0) {
                    status_.set_progress_percent(
                        100.0 * all_points.size() / total);
                }
                status_.set_current_speed_dps(speed);
                status_.set_current_coefficient_value(kp);
                if (have_winner) {
                    *status_.mutable_current_speed_best() = speed_winner;
                }
                if (coefficient == PID_COEFF_ALL) {
                    status_.set_message(
                        "Kp=" + std::to_string(kp) +
                        " Ki=" + std::to_string(ki) +
                        " Kd=" + std::to_string(kd));
                } else {
                    status_.set_message("Testing combinations");
                }
            }
        }

        if (have_winner) {
            winners.push_back(speed_winner);
        }
    }

    // Stop the axis and let it settle before finishing.
    controller_.stopAxis(tested_axis, false, 0.0);

    astro_mount::PidCalibrationPoint overall_winner;
    bool have_overall = false;
    for (const auto& p : all_points) {
        if (!have_overall || p.score() < overall_winner.score()) {
            overall_winner = p;
            have_overall = true;
        }
    }

    {
        std::lock_guard<std::mutex> lock(mtx_);
        for (const auto& w : winners) {
            *result_.add_winners() = w;
        }
        if (have_overall) {
            *result_.mutable_overall_winner() = overall_winner;
        }
        result_.set_finished_at_unix_ms(nowUnixMs());

        if (stop_requested_) {
            status_.set_state("STOPPED");
            status_.set_running(false);
            status_.set_message("Calibration stopped by user");
        } else if (!all_points.empty()) {
            status_.set_state("COMPLETED");
            status_.set_running(false);
            status_.set_progress_percent(100.0);
            status_.set_message("Calibration completed");
        } else {
            status_.set_state("ERROR");
            status_.set_running(false);
            result_.set_error_message("No combinations tested");
            status_.set_message("No combinations tested");
        }
    }

    running_ = false;
}

std::string PidCalibrationServiceImpl::saveResultToFile(
    const astro_mount::PidCalibrationResult& result,
    const std::string& requested_path, std::string& error) {
    json out;
    out["axis_id"] = result.axis_id();
    out["loop"] = static_cast<int>(result.loop());
    out["coefficient"] = static_cast<int>(result.coefficient());
    out["started_at_unix_ms"] = result.started_at_unix_ms();
    out["finished_at_unix_ms"] = result.finished_at_unix_ms();
    out["error_message"] = result.error_message();

    json points = json::array();
    for (const auto& p : result.points()) {
        json jp;
        jp["kp"] = p.kp();
        jp["ki"] = p.ki();
        jp["kd"] = p.kd();
        jp["target_speed_dps"] = p.target_speed_dps();
        jp["mean_speed_dps"] = p.mean_speed_dps();
        jp["stddev_speed_dps"] = p.stddev_speed_dps();
        jp["score"] = p.score();
        jp["sample_count"] = p.sample_count();
        points.push_back(std::move(jp));
    }
    out["points"] = std::move(points);

    json winners = json::array();
    for (const auto& w : result.winners()) {
        json jw;
        jw["kp"] = w.kp();
        jw["ki"] = w.ki();
        jw["kd"] = w.kd();
        jw["target_speed_dps"] = w.target_speed_dps();
        jw["mean_speed_dps"] = w.mean_speed_dps();
        jw["stddev_speed_dps"] = w.stddev_speed_dps();
        jw["score"] = w.score();
        winners.push_back(std::move(jw));
    }
    out["winners"] = std::move(winners);

    if (result.has_overall_winner()) {
        const auto& w = result.overall_winner();
        out["overall_winner"] = {
            {"kp", w.kp()},
            {"ki", w.ki()},
            {"kd", w.kd()},
            {"target_speed_dps", w.target_speed_dps()},
            {"mean_speed_dps", w.mean_speed_dps()},
            {"stddev_speed_dps", w.stddev_speed_dps()},
            {"score", w.score()},
        };
    }

    std::string path = requested_path;
    if (path.empty()) {
        path = "config/pid_calibration_results.json";
    }

    std::ofstream file(path);
    if (!file.is_open()) {
        error = "Cannot open file for writing: " + path;
        return "";
    }
    file << out.dump(2) << std::endl;
    file.close();

    return path;
}

} // namespace astro_pidcal
