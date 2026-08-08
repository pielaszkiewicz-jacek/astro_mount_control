#include <deque>
#include "api/service_impl.h"
#include "controllers/mount_controller.h"
#include "logging/logger.h"
#include "core/astronomical_calculations.h"
#include "proto/mount_controller.pb.h"
#include "proto/mount_controller.grpc.pb.h"
#include <google/protobuf/util/time_util.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <sys/sysinfo.h>
#endif

namespace astro_mount {
namespace api {

using google::protobuf::util::TimeUtil;
using json = nlohmann::json;

MountControllerServiceImpl::MountControllerServiceImpl(controllers::MountController& controller)
    : controller_(controller) {}

// Basic mount control
grpc::Status MountControllerServiceImpl::SlewToCoordinates(grpc::ServerContext* context,
                                                          const astro_mount::Coordinates* request,
                                                          google::protobuf::Empty* response) {
    try {
        if (controller_.slewToEquatorial(request->ra(), request->dec())) {
            return grpc::Status::OK;
        } else {
            return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to start slew");
        }
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::TrackObject(grpc::ServerContext* context,
                                                    const astro_mount::Coordinates* request,
                                                    google::protobuf::Empty* response) {
    try {
        // Map proto tracking_mode (int32) to MountController::TrackingMode enum.
        // Proto field defaults to 0 (SIDEREAL) when not set.
        auto mode = config::TrackingMode::SIDEREAL;
        switch (request->tracking_mode()) {
            case 0: mode = config::TrackingMode::SIDEREAL; break;
            case 1: mode = config::TrackingMode::SOLAR;    break;
            case 2: mode = config::TrackingMode::LUNAR;    break;
            case 3: mode = config::TrackingMode::CUSTOM;   break;
            case 4: mode = config::TrackingMode::OFF;      break;
            default:
                API_LOG_WARN("TrackObject: unknown tracking_mode={}, defaulting to SIDEREAL",
                            request->tracking_mode());
                break;
        }
        
        if (controller_.startTracking(request->ra(), request->dec(), mode)) {
            return grpc::Status::OK;
        } else {
            return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to start tracking");
        }
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::Stop(grpc::ServerContext* context,
                                             const google::protobuf::Empty* request,
                                             google::protobuf::Empty* response) {
    try {
        controller_.stop();
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::Park(grpc::ServerContext* context,
                                             const google::protobuf::Empty* request,
                                             google::protobuf::Empty* response) {
    try {
        controller_.park();
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

// State management
grpc::Status MountControllerServiceImpl::GetState(grpc::ServerContext* context,
                                                 const google::protobuf::Empty* request,
                                                 astro_mount::ControllerState* response) {
    try {
        auto status = controller_.getStatus();
        
        // Convert status to proto
        response->set_status(convertStatus(static_cast<int>(status.state)));
        response->set_encoders_enabled(status.encoders_active);
        response->set_guider_active(status.guider_active);
        response->set_tracking_rate_ra(status.axis1_rate * 3600.0);   // deg/s → arcsec/s
        response->set_tracking_rate_dec(status.axis2_rate * 3600.0);  // deg/s → arcsec/s
        response->set_actual_rate_axis1(status.actual_axis1_rate);       // deg/s (CANopen)
        response->set_actual_rate_axis2(status.actual_axis2_rate);       // deg/s (CANopen)
        
        // current_position = servo/motor degrees (raw, before gear_ratio division)
        auto* pos = response->mutable_current_position();
        pos->set_axis1(status.axis1_position);
        pos->set_axis2(status.axis2_position);
        
        // Telescope axis positions — normalized telescope degrees [0°, 360°).
        // Computed as servo_position / gear_ratio, then normalized per mount type.
        response->set_telescope_axis1(status.telescope_axis1_position);
        response->set_telescope_axis2(status.telescope_axis2_position);
        
        // Meridian flip status
        response->set_pier_side(status.pier_side);
        response->set_meridian_flipped(status.meridian_flip_in_progress);
        response->set_time_to_meridian(status.time_to_meridian);
        
        // Set timestamp
        *response->mutable_state_time() = TimeUtil::GetCurrentTime();

        // Environmental conditions
        response->set_temperature(status.env_temperature);
        response->set_pressure(status.env_pressure);
        response->set_humidity(status.env_humidity);
        
        // Tracking target info
        if (status.tracking_active) {
            auto* tracked = response->mutable_tracked_object();
            auto* coords = tracked->mutable_coordinates();
            coords->set_ra(status.tracking_target_ra);
            coords->set_dec(status.tracking_target_dec);
            tracked->set_tracking_error_ra(status.tracking_error_ra);
            tracked->set_tracking_error_dec(status.tracking_error_dec);
        }

        // === NEW: Bootstrap status in GetState (plan §5.6) ===
        auto* bs = response->mutable_bootstrap_status();
        bs->set_calibrated(status.bootstrap_calibrated);
        bs->set_measurement_count(status.bootstrap_measurement_count);
        bs->set_bootstrap_mode(
            static_cast<astro_mount::BootstrapMode>(status.bootstrap_mode));
        bs->set_encoder_type_absolute(status.encoders_absolute);
        bs->set_reference_position_known(status.encoders_absolute || status.bootstrap_calibrated);
        if (status.bootstrap_calibrated) {
            bs->set_state(astro_mount::BootstrapStatus_CalibrationState_CALIBRATED);
            bs->set_state_message("Bootstrap calibration complete");
        } else if (status.bootstrap_measurement_count >= 2) {
            bs->set_state(astro_mount::BootstrapStatus_CalibrationState_MEASUREMENTS_COLLECTING);
            bs->set_state_message("Collecting bootstrap measurements");
        } else {
            bs->set_state(astro_mount::BootstrapStatus_CalibrationState_NEEDS_MORE_MEASUREMENTS);
            bs->set_state_message("Need at least 2 measurements for calibration");
        }
        bs->set_min_measurements_required(2.0);
        bs->set_min_measurements_for_tpoint(3.0);
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::WatchState(grpc::ServerContext* context,
                                                    const google::protobuf::Empty* request,
                                                    grpc::ServerWriter<astro_mount::ControllerState>* writer) {
    try {
        // Stream state updates at ~10 Hz until the client disconnects
        while (!context->IsCancelled()) {
            auto status = controller_.getStatus();
            
            astro_mount::ControllerState state;
            state.set_status(convertStatus(static_cast<int>(status.state)));
            state.set_encoders_enabled(status.encoders_active);
            state.set_guider_active(status.guider_active);
            state.set_tracking_rate_ra(status.tracking_error_ra);
            state.set_tracking_rate_dec(status.tracking_error_dec);
            
            // current_position = servo/motor degrees (raw, before gear_ratio division)
            auto* pos = state.mutable_current_position();
            pos->set_axis1(status.axis1_position);
            pos->set_axis2(status.axis2_position);
            
            // Telescope axis positions — normalized telescope degrees [0°, 360°).
            state.set_telescope_axis1(status.telescope_axis1_position);
            state.set_telescope_axis2(status.telescope_axis2_position);

            // Environmental conditions
            state.set_temperature(status.env_temperature);
            state.set_pressure(status.env_pressure);
            state.set_humidity(status.env_humidity);
            
            *state.mutable_state_time() = TimeUtil::GetCurrentTime();
            
            if (!writer->Write(state)) {
                // Client disconnected
                break;
            }
            
            // Sleep for ~100ms
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::SaveState(grpc::ServerContext* context,
                                                   const astro_mount::StateSaveRequest* request,
                                                   astro_mount::StateSaveResponse* response) {
    try {
        std::string file_path = request->file_path();
        if (file_path.empty()) {
            file_path = "data/mount_state.json";
        }
        
        if (controller_.saveState(file_path)) {
            response->set_file_path(file_path);
            uintmax_t actual_size = std::filesystem::file_size(file_path);
            response->set_file_size(static_cast<int64_t>(actual_size));
            return grpc::Status::OK;
        } else {
            return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to save state");
        }
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::LoadState(grpc::ServerContext* context,
                                                  const astro_mount::StateLoadRequest* request,
                                                  google::protobuf::Empty* response) {
    try {
        std::string file_path = request->file_path();
        if (file_path.empty()) {
            file_path = "data/mount_state.json";
        }
        
        if (controller_.loadState(file_path)) {
            return grpc::Status::OK;
        } else {
            return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to load state");
        }
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::ClearErrors(grpc::ServerContext* context,
                                                     const google::protobuf::Empty* request,
                                                     google::protobuf::Empty* response) {
    try {
        controller_.clearErrors();
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

// Measurement and calibration
// Bootstrap calibration API
grpc::Status MountControllerServiceImpl::AddBootstrapMeasurement(grpc::ServerContext* context,
                                                                const astro_mount::BootstrapMeasurement* request,
                                                                google::protobuf::Empty* response) {
    try {
        // Extract mount position (optional for bootstrap)
        double mount_ha = 0.0;
        double mount_dec = 0.0;
        if (request->has_mount_position()) {
            // MountPosition.axis1/axis2 are documented as telescope/mount degrees.
            // runBootstrapCalibration() treats mount_ha as altitude-like degrees
            // and mount_dec as azimuth-like degrees.  Do NOT divide by 15 here —
            // the bootstrap SVD solver expects degrees, not hours.
            mount_ha = request->mount_position().axis1();  // axis1 in telescope/mount degrees
            mount_dec = request->mount_position().axis2(); // axis2 in telescope/mount degrees
        } else {
            // Mount position not provided by the client — auto-populate from
            // the current controller telescope position.  If this is also
            // zero (e.g. incremental encoders before any motion), the
            // bootstrap SVD will be degenerate (all mount vectors identical),
            // producing a suspiciously perfect RMS=0.000 fit.
            auto status = controller_.getStatus();
            mount_ha = status.telescope_axis1_position;
            mount_dec = status.telescope_axis2_position;
            API_LOG_WARN("AddBootstrapMeasurement: client did not provide mount_position — "
                        "auto-populated from controller state: mount_ha={:.4f}°, mount_dec={:.4f}°. "
                        "If these are all zeros, bootstrap calibration will produce a "
                        "degenerate (RMS=0.000) result.",
                        mount_ha, mount_dec);
        }
        
        // For bootstrap, we use simpler parameters - default environmental values
        if (controller_.addBootstrapMeasurement(
                request->observed().ra(),
                request->observed().dec(),
                request->expected().ra(),
                request->expected().dec(),
                mount_ha, mount_dec)) {
            return grpc::Status::OK;
        } else {
            return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to add bootstrap measurement");
        }
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::RunBootstrapCalibration(grpc::ServerContext* context,
                                                                const google::protobuf::Empty* request,
                                                                astro_mount::BootstrapCalibrationResult* response) {
    try {
        if (controller_.runBootstrapCalibration()) {
            int count = static_cast<int>(controller_.getBootstrapMeasurementCount());
            double residual_rms = controller_.getBootstrapQuaternionErrorArcsec();
            double ra_corr = controller_.getBootstrapRaCorrectionArcsec();
            double dec_corr = controller_.getBootstrapDecCorrectionArcsec();
            
            // Combined alignment error from both axes
            double alignment_error = std::sqrt(ra_corr * ra_corr + dec_corr * dec_corr);
            // Estimate max residual as 2x RMS as a reasonable upper bound
            double max_residual = residual_rms * 2.0;
            // Initial rotation angle estimate from RA correction (converted to degrees)
            double rotation_angle = ra_corr / 3600.0;
            
            response->set_success(true);
            response->set_measurement_count(count);
            response->set_initial_rotation_angle_deg(rotation_angle);
            response->set_alignment_error_arcsec(alignment_error);
            response->set_residual_rms_arcsec(residual_rms);
            response->set_max_residual_arcsec(max_residual);
            response->set_ready_for_tpoint(count >= 3);
            *response->mutable_calibrated_at() = TimeUtil::GetCurrentTime();
            
            // For CASUAL mount: populate estimated orientation quaternion
            auto mount_orientation = controller_.getMountOrientation();
            auto* ori = response->mutable_estimated_orientation();
            ori->set_qx(mount_orientation.quaternion[0]);
            ori->set_qy(mount_orientation.quaternion[1]);
            ori->set_qz(mount_orientation.quaternion[2]);
            ori->set_qw(mount_orientation.quaternion[3]);
            // Estimated quaternion error (combined RMS from both axes)
            response->set_estimated_quaternion_error(residual_rms);
            
            return grpc::Status::OK;
        } else {
            response->set_success(false);
            response->set_error_message("Bootstrap calibration failed - need at least 2 measurements");
            return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                              "Need at least 2 measurements for bootstrap calibration");
        }
    } catch (const std::exception& e) {
        response->set_success(false);
        response->set_error_message(std::string("Error: ") + e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::GetBootstrapStatus(grpc::ServerContext* context,
                                                           const google::protobuf::Empty* request,
                                                           astro_mount::BootstrapStatus* response) {
    try {
        auto status = controller_.getStatus();
        bool calibrated = controller_.isBootstrapCalibrated();
        int count = static_cast<int>(controller_.getBootstrapMeasurementCount());
        double ra_corr = controller_.getBootstrapRaCorrectionArcsec();
        double dec_corr = controller_.getBootstrapDecCorrectionArcsec();
        double alignment_error = std::sqrt(ra_corr * ra_corr + dec_corr * dec_corr);
        
        response->set_calibrated(calibrated);
        *response->mutable_last_calibration() = TimeUtil::GetCurrentTime();
        response->set_measurement_count(count);
        response->set_current_alignment_error_arcsec(calibrated ? alignment_error : 999.0);
        response->set_ready_for_tpoint(calibrated && count >= 3);
        
        // Set state based on calibration status
        if (calibrated) {
            response->set_state(astro_mount::BootstrapStatus_CalibrationState_CALIBRATED);
            response->set_state_message("Bootstrap calibration complete");
        } else if (count >= 2) {
            response->set_state(astro_mount::BootstrapStatus_CalibrationState_MEASUREMENTS_COLLECTING);
            response->set_state_message("Collecting bootstrap measurements");
        } else {
            response->set_state(astro_mount::BootstrapStatus_CalibrationState_NEEDS_MORE_MEASUREMENTS);
            response->set_state_message("Need at least 2 measurements for calibration");
        }
        
        response->set_min_measurements_required(2.0);
        response->set_min_measurements_for_tpoint(3.0);
        
        // === NEW: Bootstrap mode and encoder type fields (plan §5.6) ===
        response->set_bootstrap_mode(
            static_cast<astro_mount::BootstrapMode>(status.bootstrap_mode));
        response->set_encoder_type_absolute(status.encoders_absolute);
        response->set_reference_position_known(status.encoders_absolute || calibrated);
        response->set_estimated_encoder_offset_deg(0.0);  // Inferred from rotation Q
        int manual_needed = (status.bootstrap_mode == 1) ? std::max(0, 3 - count) : 0;
        response->set_manual_measurements_needed(manual_needed);
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::ClearBootstrapMeasurements(grpc::ServerContext* context,
                                                                   const google::protobuf::Empty* request,
                                                                   google::protobuf::Empty* response) {
    try {
        controller_.clearBootstrapMeasurements();
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

// === NEW: Bootstrap mode and auto-bootstrap (plan §5.4, §5.6) ===

grpc::Status MountControllerServiceImpl::SetBootstrapMode(
    grpc::ServerContext* context,
    const astro_mount::BootstrapModeRequest* request,
    google::protobuf::Empty* response) {
    try {
        auto proto_mode = request->mode();
        auto cpp_mode = static_cast<config::BootstrapMode>(
            static_cast<int>(proto_mode));
        
        if (cpp_mode < config::BootstrapMode::BOOTSTRAP_MANUAL ||
            cpp_mode > config::BootstrapMode::BOOTSTRAP_AUTOMATIC) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                              "Invalid bootstrap mode value");
        }
        
        controller_.setBootstrapMode(cpp_mode);
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::RunAutomaticBootstrap(
    grpc::ServerContext* context,
    const astro_mount::AutoBootstrapRequest* request,
    google::protobuf::Empty* response) {
    try {
        // Validate request parameters
        int min_measurements = request->min_measurements();
        if (min_measurements <= 0) min_measurements = 3;  // Default
        
        double max_error = request->max_alignment_error_arcsec();
        if (max_error <= 0.0) max_error = 60.0;  // Default: 1 arcmin
        
        // Verify controller is in a valid state for auto-bootstrap
        auto status = controller_.getStatus();
        if (status.state == controllers::MountController::MountStatus::State::UNINITIALIZED ||
            status.state == controllers::MountController::MountStatus::State::ERROR) {
            return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                              "Mount is in UNINITIALIZED or ERROR state");
        }
        
        // Verify bootstrap mode is set to automatic or hybrid
        if (status.bootstrap_mode != static_cast<int>(
                config::BootstrapMode::BOOTSTRAP_AUTOMATIC) &&
            status.bootstrap_mode != static_cast<int>(
                config::BootstrapMode::BOOTSTRAP_HYBRID)) {
            return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                              "Bootstrap mode must be HYBRID or AUTOMATIC for auto-bootstrap");
        }
        
        // Verify absolute encoders or reference position for automatic mode
        bool has_reference = status.encoders_absolute;
        if (!has_reference && status.bootstrap_mode == static_cast<int>(
                config::BootstrapMode::BOOTSTRAP_AUTOMATIC)) {
            return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                              "AUTOMATIC bootstrap requires absolute encoders or prior calibration");
        }
        
        // Set the bootstrap mode on the controller
        auto mode = static_cast<config::BootstrapMode>(status.bootstrap_mode);
        controller_.setBootstrapMode(mode);
        
        // The actual orchestration (slewing, plate solving, measurement) is handled
        // by the proxy orchestrator (server.js) per plan §7.1 Level 2.
        // The C++ layer validates preconditions and sets the mode.
        // Return OK — the proxy will call SlewToCoordinates, AddBootstrapMeasurement,
        // and RunBootstrapCalibration iteratively through existing RPCs.
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::GetAutoBootstrapStatus(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    astro_mount::AutoBootstrapStatus* response) {
    try {
        // Auto-bootstrap state is tracked by the proxy orchestrator (server.js)
        // per plan §7.1 Level 2. The C++ layer reports the underlying mount state.
        auto status = controller_.getStatus();
        
        if (controller_.isBootstrapCalibrated()) {
            response->set_state(astro_mount::AutoBootstrapStatus::COMPLETED);
            response->set_state_message("Bootstrap calibration completed");
            response->set_measurements_collected(
                static_cast<int>(controller_.getBootstrapMeasurementCount()));
            response->set_measurements_target(
                static_cast<int>(controller_.getBootstrapMeasurementCount()));
            response->set_progress_percent(100.0);
        } else if (controller_.getBootstrapMeasurementCount() >= 2) {
            response->set_state(astro_mount::AutoBootstrapStatus::ADDING_MEASUREMENT);
            response->set_state_message("Measurements collected, ready for calibration");
            response->set_measurements_collected(
                static_cast<int>(controller_.getBootstrapMeasurementCount()));
            response->set_measurements_target(
                std::max(3, static_cast<int>(controller_.getBootstrapMeasurementCount()) + 1));
            // Calculate progress: collecting phase (0-80%), then calibration (80-100%)
            double measure_progress = std::min(80.0,
                80.0 * controller_.getBootstrapMeasurementCount() / 5.0);
            response->set_progress_percent(measure_progress);
        } else {
            response->set_state(astro_mount::AutoBootstrapStatus::IDLE);
            response->set_state_message("Auto-bootstrap not started");
            response->set_measurements_collected(
                static_cast<int>(controller_.getBootstrapMeasurementCount()));
            response->set_measurements_target(3);
            response->set_progress_percent(0.0);
        }
        
        response->set_current_target_star("");
        response->set_error_message("");
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

// TPOINT calibration API
grpc::Status MountControllerServiceImpl::AddTPointMeasurement(grpc::ServerContext* context,
                                                             const astro_mount::Measurement* request,
                                                             google::protobuf::Empty* response) {
    try {
        // Extract mount position (required for TPOINT)
        double mount_ha = 0.0;
        double mount_dec = 0.0;
        if (request->has_mount_position()) {
            mount_ha = request->mount_position().axis1() / 15.0; // degrees to hours
            mount_dec = request->mount_position().axis2();       // degrees
        }
        
        // Extract environmental parameters with defaults
        double temperature = request->temperature();
        double pressure = request->pressure();
        double humidity = request->humidity();
        
        // Apply defaults if values are zero (proto3 default)
        if (temperature == 0.0) temperature = 15.0;
        if (pressure == 0.0) pressure = 1013.25;
        if (humidity == 0.0) humidity = 0.5;
        
        // Extract astrometric parameters from expected coordinates
        double proper_motion_ra = request->expected().pm_ra();
        double proper_motion_dec = request->expected().pm_dec();
        double parallax = request->expected().parallax();
        double epoch = request->expected().epoch();
        
        // Apply defaults for astrometric parameters
        if (epoch == 0.0) epoch = 2000.0;
        
        // Use the full TPOINT API with mount position
        if (controller_.addTPointMeasurement(
                request->observed().ra(),
                request->observed().dec(),
                request->expected().ra(),
                request->expected().dec(),
                mount_ha, mount_dec,
                temperature, pressure, humidity,
                proper_motion_ra, proper_motion_dec,
                parallax, epoch)) {
            return grpc::Status::OK;
        } else {
            return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to add TPOINT measurement");
        }
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::ClearTPointMeasurements(grpc::ServerContext* context,
                                                                const google::protobuf::Empty* request,
                                                                google::protobuf::Empty* response) {
    try {
        controller_.clearTPointMeasurements();
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::GetTPointParameters(grpc::ServerContext* context,
                                                            const google::protobuf::Empty* request,
                                                            astro_mount::TPointParameters* response) {
    try {
        // Get TPOINT calibration status from the controller
        auto status = controller_.getStatus();
        response->set_calibrated(status.tpoint_calibrated);

        // Parse JSON parameters for chi_squared and other stats
        auto params_str = controller_.getTPointParameters();
        auto j = json::parse(params_str);
        response->set_chi_squared(j.value("chi_squared", 0.0));
        *response->mutable_last_update() = TimeUtil::GetCurrentTime();
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::GetRotationMatrix(grpc::ServerContext* context,
                                                          const google::protobuf::Empty* request,
                                                          astro_mount::RotationMatrix* response) {
    try {
        auto rotation = controller_.getRotationMatrix();
        if (rotation.size() >= 4) {
            response->set_q0(rotation[0]);
            response->set_q1(rotation[1]);
            response->set_q2(rotation[2]);
            response->set_q3(rotation[3]);
            *response->mutable_valid_from() = TimeUtil::GetCurrentTime();
        }
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

// Pole position determination
grpc::Status MountControllerServiceImpl::DeterminePolePosition(
    grpc::ServerContext* context,
    const astro_mount::PoleDeterminationRequest* request,
    astro_mount::PolePosition* response) {
    try {
        auto pole = controller_.determinePolePosition(request->duration_hours());
        response->set_latitude(std::get<0>(pole));
        response->set_longitude(std::get<1>(pole));
        response->set_altitude(0.0); // Not implemented yet
        response->set_accuracy(std::get<2>(pole));
        *response->mutable_determined_at() = TimeUtil::GetCurrentTime();
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

// Encoder control
grpc::Status MountControllerServiceImpl::EnableEncoders(grpc::ServerContext* context,
                                                       const astro_mount::EncoderConfig* request,
                                                       google::protobuf::Empty* response) {
    try {
        bool absolute = (request->type() == astro_mount::EncoderConfig::ABSOLUTE);
        controller_.setEncoderType(absolute);
        controller_.setEncodersEnabled(true);
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::DisableEncoders(grpc::ServerContext* context,
                                                        const google::protobuf::Empty* request,
                                                        google::protobuf::Empty* response) {
    try {
        controller_.setEncodersEnabled(false);
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

// Guider control
grpc::Status MountControllerServiceImpl::ConnectGuider(grpc::ServerContext* context,
                                                      const astro_mount::GuiderConfig* request,
                                                      google::protobuf::Empty* response) {
    try {
        if (controller_.connectGuider(request->connection_string())) {
            return grpc::Status::OK;
        } else {
            return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to connect guider");
        }
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::DisconnectGuider(grpc::ServerContext* context,
                                                         const google::protobuf::Empty* request,
                                                         google::protobuf::Empty* response) {
    try {
        controller_.disconnectGuider();
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::SendGuiderCorrection(grpc::ServerContext* context,
                                                             const astro_mount::GuiderCorrection* request,
                                                             google::protobuf::Empty* response) {
    try {
        controller_.applyGuiderCorrection(request->ra_correction(), request->dec_correction());
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

    // Configuration
    grpc::Status MountControllerServiceImpl::GetConfiguration(grpc::ServerContext* context,
                                                         const google::protobuf::Empty* request,
                                                         astro_mount::Configuration* response) {
    try {
        auto config = controller_.getConfiguration();
        
        // Fill response with configuration
        response->set_latitude(config.mount_config.latitude);
        response->set_longitude(config.mount_config.longitude);
        response->set_altitude(config.mount_config.altitude);
        response->set_focal_length(config.focal_length);
        response->set_aperture(config.aperture);
        response->set_tube_length(config.tube_length);
        response->set_camera_model(config.camera_model);
        response->set_pixel_size(config.pixel_size);
        response->set_sensor_width(config.sensor_width);
        response->set_sensor_height(config.sensor_height);

        // LX200 serial interface config
        auto* lx200 = response->mutable_lx200_config();
        lx200->set_enabled(config.lx200_enabled);
        lx200->set_port(config.lx200_port);
        lx200->set_baud_rate(config.lx200_baud_rate);

        response->set_default_temperature(config.mount_config.default_temperature);
        response->set_default_pressure(config.mount_config.default_pressure);
        response->set_default_humidity(config.mount_config.default_humidity);
        response->set_process_noise(config.mount_config.process_noise);
        response->set_measurement_noise(config.mount_config.measurement_noise);
        response->set_log_level(config.log_level);
        response->set_log_directory(config.log_directory);
        response->set_log_console_output(config.log_console_output);
        response->set_log_rotation_days(config.log_rotation_days);
        response->set_log_max_file_size_mb(config.log_max_file_size_mb);
        response->set_grpc_address(config.grpc_address);
        response->set_grpc_port(config.grpc_port);
        response->set_network_max_connections(config.network_max_connections);
        response->set_network_enable_ssl(config.network_enable_ssl);
        response->set_network_ssl_cert_path(config.network_ssl_cert_path);
        response->set_network_ssl_key_path(config.network_ssl_key_path);
        
        // Mount control parameters
        response->set_max_slew_rate(config.mount_config.max_slew_rate);
        response->set_max_tracking_rate(config.mount_config.max_tracking_rate);
        response->set_slew_acceleration(config.mount_config.slew_acceleration);
        response->set_tracking_acceleration(config.mount_config.tracking_acceleration);
        
        // Park position
        response->set_park_position_axis1(config.safety_config.park_position_axis1);
        response->set_park_position_axis2(config.safety_config.park_position_axis2);
        
        // Encoder configuration
        response->set_use_encoders(config.mount_config.use_encoders);
        response->set_encoders_absolute(config.mount_config.encoders_absolute);
        response->set_encoder_resolution_config(config.mount_config.encoder_resolution);
        
        // TPOINT configuration
        response->set_tpoint_enabled_terms(config.calibration_config.tpoint_enabled_terms);
        
        // Guider configuration
        response->set_enable_guider(config.tracking_config.enable_guider);
        response->set_guider_max_correction(config.tracking_config.guider_max_correction);
        response->set_guider_aggression(config.tracking_config.guider_aggression);
        
        // Atmospheric refraction correction
        response->set_enable_refraction_correction(config.safety_config.enable_refraction_correction);
        
        // Mount type configuration
        response->set_mount_type(static_cast<astro_mount::MountType>(config.mount_config.mount_type));
        
        // Mount orientation quaternion (for CASUAL mount type)
        {
            auto* orientation = response->mutable_mount_orientation();
            orientation->set_qx(config.mount_config.mount_orientation.quaternion[0]);
            orientation->set_qy(config.mount_config.mount_orientation.quaternion[1]);
            orientation->set_qz(config.mount_config.mount_orientation.quaternion[2]);
            orientation->set_qw(config.mount_config.mount_orientation.quaternion[3]);
        }
        
        // Loop timing
        response->set_controller_poll_ms(config.tracking_config.controller_poll_ms);
        response->set_tracking_update_ms(config.tracking_config.tracking_update_ms);
        
        // Position/rate tolerances for slew operations
        response->set_position_tolerance(config.mount_config.position_tolerance);
        response->set_rate_tolerance(config.mount_config.rate_tolerance);
        
        // Equatorial tracking mode (position vs velocity)
        response->set_equatorial_tracking_velocity_mode(config.mount_config.equatorial_tracking_velocity_mode);

        // Per-axis rotation direction inversion
        // Meridian flip configuration
        response->set_meridian_flip_enabled(config.safety_config.meridian_flip_enabled);
        response->set_meridian_flip_delay_minutes(config.safety_config.meridian_flip_delay_minutes);
        response->set_meridian_flip_hysteresis_degrees(config.safety_config.meridian_flip_hysteresis_degrees);
        response->set_meridian_flip_timeout_seconds(config.safety_config.meridian_flip_timeout_seconds);
        
        // Soft limits configuration
        response->set_soft_limits_enabled(config.safety_config.soft_limits_enabled);
        response->set_soft_limit_axis1_min(config.safety_config.soft_limit_axis1_min);
        response->set_soft_limit_axis1_max(config.safety_config.soft_limit_axis1_max);
        response->set_soft_limit_axis2_min(config.safety_config.soft_limit_axis2_min);
        response->set_soft_limit_axis2_max(config.safety_config.soft_limit_axis2_max);
        response->set_soft_limit_warning_degrees(config.safety_config.soft_limit_warning_degrees);
        response->set_soft_limit_deceleration_degrees(config.safety_config.soft_limit_deceleration_degrees);
        response->set_soft_limit_tracking_rate_factor(config.safety_config.soft_limit_tracking_rate_factor);
        
        // HA axis physical parameters
        auto* ha_params = response->mutable_ha_axis_params();
        ha_params->set_position_counts_per_degree(config.mount_config.ha_axis_params.position_counts_per_degree);
        ha_params->set_velocity_counts_per_deg_s(config.mount_config.ha_axis_params.velocity_counts_per_deg_s);
        ha_params->set_encoder_resolution(config.mount_config.ha_axis_params.encoder_resolution);
        ha_params->set_encoder_counts_per_arcsec(config.mount_config.ha_axis_params.encoder_counts_per_arcsec);
        ha_params->set_encoder_quantization_error(config.mount_config.ha_axis_params.encoder_quantization_error);
        ha_params->set_gear_ratio(config.mount_config.ha_axis_params.gear_ratio);
        ha_params->set_worm_ratio(config.mount_config.ha_axis_params.worm_ratio);
        ha_params->set_worm_teeth(config.mount_config.ha_axis_params.worm_teeth);
        ha_params->set_worm_wheel_teeth(config.mount_config.ha_axis_params.worm_wheel_teeth);
        ha_params->set_cyclic_error_amplitude(config.mount_config.ha_axis_params.cyclic_error_amplitude);
        ha_params->set_cyclic_error_period(config.mount_config.ha_axis_params.cyclic_error_period);
        
        // Copy cyclic harmonics
        for (const auto& harmonic : config.mount_config.ha_axis_params.cyclic_harmonics) {
            ha_params->add_cyclic_harmonics(harmonic);
        }
        
        ha_params->set_backlash(config.mount_config.ha_axis_params.backlash);
        ha_params->set_backlash_temp_coeff(config.mount_config.ha_axis_params.backlash_temp_coeff);
        ha_params->set_axis_stiffness(config.mount_config.ha_axis_params.axis_stiffness);
        ha_params->set_torsional_compliance(config.mount_config.ha_axis_params.torsional_compliance);
        ha_params->set_expansion_coeff(config.mount_config.ha_axis_params.expansion_coeff);
        ha_params->set_temp_gear_error_coeff(config.mount_config.ha_axis_params.temp_gear_error_coeff);
        ha_params->set_calibration_temp(config.mount_config.ha_axis_params.calibration_temp);
        
        // Copy calibration table
        for (const auto& value : config.mount_config.ha_axis_params.calibration_table) {
            ha_params->add_calibration_table(value);
        }
        
        // Dec axis physical parameters
        auto* dec_params = response->mutable_dec_axis_params();
        dec_params->set_position_counts_per_degree(config.mount_config.dec_axis_params.position_counts_per_degree);
        dec_params->set_velocity_counts_per_deg_s(config.mount_config.dec_axis_params.velocity_counts_per_deg_s);
        dec_params->set_encoder_resolution(config.mount_config.dec_axis_params.encoder_resolution);
        dec_params->set_encoder_counts_per_arcsec(config.mount_config.dec_axis_params.encoder_counts_per_arcsec);
        dec_params->set_encoder_quantization_error(config.mount_config.dec_axis_params.encoder_quantization_error);
        dec_params->set_gear_ratio(config.mount_config.dec_axis_params.gear_ratio);
        dec_params->set_worm_ratio(config.mount_config.dec_axis_params.worm_ratio);
        dec_params->set_worm_teeth(config.mount_config.dec_axis_params.worm_teeth);
        dec_params->set_worm_wheel_teeth(config.mount_config.dec_axis_params.worm_wheel_teeth);
        dec_params->set_cyclic_error_amplitude(config.mount_config.dec_axis_params.cyclic_error_amplitude);
        dec_params->set_cyclic_error_period(config.mount_config.dec_axis_params.cyclic_error_period);
        
        // Copy cyclic harmonics for dec axis
        for (const auto& harmonic : config.mount_config.dec_axis_params.cyclic_harmonics) {
            dec_params->add_cyclic_harmonics(harmonic);
        }
        
        dec_params->set_backlash(config.mount_config.dec_axis_params.backlash);
        dec_params->set_backlash_temp_coeff(config.mount_config.dec_axis_params.backlash_temp_coeff);
        dec_params->set_axis_stiffness(config.mount_config.dec_axis_params.axis_stiffness);
        dec_params->set_torsional_compliance(config.mount_config.dec_axis_params.torsional_compliance);
        dec_params->set_expansion_coeff(config.mount_config.dec_axis_params.expansion_coeff);
        dec_params->set_temp_gear_error_coeff(config.mount_config.dec_axis_params.temp_gear_error_coeff);
        dec_params->set_calibration_temp(config.mount_config.dec_axis_params.calibration_temp);
        
        // Copy calibration table for dec axis
        for (const auto& value : config.mount_config.dec_axis_params.calibration_table) {
            dec_params->add_calibration_table(value);
        }
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::UpdateConfiguration(grpc::ServerContext* context,
                                                       const astro_mount::Configuration* request,
                                                       google::protobuf::Empty* response) {
                                                   try {
                                                       API_LOG_INFO("UpdateConfiguration called: log_level='{}' log_console_output={}",
                                                                    request->log_level(),
                                                                    request->log_console_output() ? "true" : "false");
                                                   
                                                       // Start from the EXISTING configuration to prevent partial updates
                                                       // from overwriting untouched fields with proto3 defaults (0, false, "").
                                                       // The previous approach created a fresh ControllerConfig and only filled
                                                       // fields present in the request, causing all other fields to revert to
                                                       // defaults — this was the root cause of config corruption where
                                                       // gear_ratio and other axis parameters got overwritten with garbage.
                                                       auto config = controller_.getConfiguration();
    
    // ── Basic configuration ──────────────────────────────────────
    // Only override if the request provides a non-default value.
    // Proto3 defaults: double→0.0, int32→0, string→"", bool→false.
    if (request->latitude() != 0.0) config.mount_config.latitude = request->latitude();
    if (request->longitude() != 0.0) config.mount_config.longitude = request->longitude();
    if (request->altitude() != 0.0) config.mount_config.altitude = request->altitude();
    if (request->default_temperature() != 0.0) config.mount_config.default_temperature = request->default_temperature();
    if (request->default_pressure() != 0.0) config.mount_config.default_pressure = request->default_pressure();
    if (request->default_humidity() != 0.0) config.mount_config.default_humidity = request->default_humidity();
    if (request->process_noise() != 0.0) config.mount_config.process_noise = request->process_noise();
    if (request->measurement_noise() != 0.0) config.mount_config.measurement_noise = request->measurement_noise();
    if (!request->grpc_address().empty()) config.grpc_address = request->grpc_address();
    if (request->grpc_port() != 0) config.grpc_port = request->grpc_port();
    if (request->network_max_connections() != 0) config.network_max_connections = request->network_max_connections();
    if (request->has_network_enable_ssl()) config.network_enable_ssl = request->network_enable_ssl();
    if (!request->network_ssl_cert_path().empty()) config.network_ssl_cert_path = request->network_ssl_cert_path();
    if (!request->network_ssl_key_path().empty()) config.network_ssl_key_path = request->network_ssl_key_path();
    if (!request->log_level().empty()) {
        API_LOG_INFO("UpdateConfiguration: log_level '{}' -> '{}'",
                     config.log_level, request->log_level());
        config.log_level = request->log_level();
    }
    if (!request->log_directory().empty()) config.log_directory = request->log_directory();
    if (request->has_log_console_output()) config.log_console_output = request->log_console_output();
    if (request->log_rotation_days() != 0) config.log_rotation_days = request->log_rotation_days();
    if (request->log_max_file_size_mb() != 0) config.log_max_file_size_mb = request->log_max_file_size_mb();
    if (request->focal_length() != 0.0) config.focal_length = request->focal_length();
    if (request->aperture() != 0.0) config.aperture = request->aperture();
    if (request->tube_length() != 0.0) config.tube_length = request->tube_length();
    if (!request->camera_model().empty()) config.camera_model = request->camera_model();
    if (request->pixel_size() != 0.0) config.pixel_size = request->pixel_size();
    if (request->sensor_width() != 0) config.sensor_width = request->sensor_width();
    if (request->sensor_height() != 0) config.sensor_height = request->sensor_height();

    // ── LX200 serial interface config ────────────────────────────
    if (request->has_lx200_config()) {
        const auto& lx200 = request->lx200_config();
        config.lx200_enabled = lx200.enabled();
        if (!lx200.port().empty()) config.lx200_port = lx200.port();
        if (lx200.baud_rate() != 0) config.lx200_baud_rate = lx200.baud_rate();
    }
    
    // ── Mount control parameters ─────────────────────────────────
    if (request->max_slew_rate() != 0.0) config.mount_config.max_slew_rate = request->max_slew_rate();
    if (request->max_tracking_rate() != 0.0) config.mount_config.max_tracking_rate = request->max_tracking_rate();
    if (request->slew_acceleration() != 0.0) config.mount_config.slew_acceleration = request->slew_acceleration();
    if (request->tracking_acceleration() != 0.0) config.mount_config.tracking_acceleration = request->tracking_acceleration();
    
    // Park position — 0.0 is a legitimate value, always apply if present in non-empty request.
    // Heuristic: if any position-related field is non-zero, assume park position was intended.
    if (request->park_position_axis1() != 0.0 || request->park_position_axis2() != 0.0) {
        config.safety_config.park_position_axis1 = request->park_position_axis1();
        config.safety_config.park_position_axis2 = request->park_position_axis2();
    }
    
    // Position/rate tolerances
    if (request->position_tolerance() != 0.0) config.mount_config.position_tolerance = request->position_tolerance();
    if (request->rate_tolerance() != 0.0) config.mount_config.rate_tolerance = request->rate_tolerance();
    
    // ── Encoder configuration ────────────────────────────────────
    if (request->has_use_encoders()) config.mount_config.use_encoders = request->use_encoders();
    if (request->has_encoders_absolute()) config.mount_config.encoders_absolute = request->encoders_absolute();
    if (request->encoder_resolution_config() != 0.0) config.mount_config.encoder_resolution = request->encoder_resolution_config();
    
    // ── TPOINT configuration ─────────────────────────────────────
    if (request->tpoint_enabled_terms() != 0) config.calibration_config.tpoint_enabled_terms = request->tpoint_enabled_terms();
    
    // ── Guider configuration ─────────────────────────────────────
    if (request->has_enable_guider()) config.tracking_config.enable_guider = request->enable_guider();
    if (request->guider_max_correction() != 0.0) config.tracking_config.guider_max_correction = request->guider_max_correction();
    if (request->guider_aggression() != 0.0) config.tracking_config.guider_aggression = request->guider_aggression();
    
    // ── Atmospheric refraction correction ────────────────────────
    if (request->has_enable_refraction_correction()) config.safety_config.enable_refraction_correction = request->enable_refraction_correction();
    
    // ── Mount type ───────────────────────────────────────────────
    // Always apply — unlike numeric proto3 fields, enum defaults (EQUATORIAL=0)
    // are legitimate user choices and cannot be distinguished from "not sent".
    config.mount_config.mount_type = static_cast<config::MountType>(
        request->mount_type());
    
    // ── Mount orientation quaternion (for CASUAL mount type) ─────
    if (request->has_mount_orientation()) {
        config.mount_config.mount_orientation.quaternion[0] = request->mount_orientation().qx();
        config.mount_config.mount_orientation.quaternion[1] = request->mount_orientation().qy();
        config.mount_config.mount_orientation.quaternion[2] = request->mount_orientation().qz();
        config.mount_config.mount_orientation.quaternion[3] = request->mount_orientation().qw();
    }
    
    // ── Loop timing ───────────────────────────────────────────────
    if (request->controller_poll_ms() != 0) config.tracking_config.controller_poll_ms = request->controller_poll_ms();
    if (request->tracking_update_ms() != 0) config.tracking_config.tracking_update_ms = request->tracking_update_ms();
    
    // ── Equatorial tracking mode ─────────────────────────────────
    if (request->has_equatorial_tracking_velocity_mode()) config.mount_config.equatorial_tracking_velocity_mode = request->equatorial_tracking_velocity_mode();

    // ── Meridian flip configuration ──────────────────────────────
    if (request->has_meridian_flip_enabled()) config.safety_config.meridian_flip_enabled = request->meridian_flip_enabled();
    if (request->meridian_flip_delay_minutes() != 0.0) config.safety_config.meridian_flip_delay_minutes = request->meridian_flip_delay_minutes();
    if (request->meridian_flip_hysteresis_degrees() != 0.0) config.safety_config.meridian_flip_hysteresis_degrees = request->meridian_flip_hysteresis_degrees();
    if (request->meridian_flip_timeout_seconds() != 0.0) config.safety_config.meridian_flip_timeout_seconds = request->meridian_flip_timeout_seconds();
    
    // ── Soft limits configuration ────────────────────────────────
    if (request->has_soft_limits_enabled()) config.safety_config.soft_limits_enabled = request->soft_limits_enabled();
    // Always apply soft limit axis values if any of them is non-zero
    // (proto3 defaults to 0.0, so non-zero means intentionally set)
    if (request->soft_limit_axis1_min() != 0.0) config.safety_config.soft_limit_axis1_min = request->soft_limit_axis1_min();
    if (request->soft_limit_axis1_max() != 0.0) config.safety_config.soft_limit_axis1_max = request->soft_limit_axis1_max();
    if (request->soft_limit_axis2_min() != 0.0) config.safety_config.soft_limit_axis2_min = request->soft_limit_axis2_min();
    if (request->soft_limit_axis2_max() != 0.0) config.safety_config.soft_limit_axis2_max = request->soft_limit_axis2_max();
    if (request->soft_limit_warning_degrees() != 0.0) config.safety_config.soft_limit_warning_degrees = request->soft_limit_warning_degrees();
    if (request->soft_limit_deceleration_degrees() != 0.0) config.safety_config.soft_limit_deceleration_degrees = request->soft_limit_deceleration_degrees();
    if (request->soft_limit_tracking_rate_factor() != 0.0) config.safety_config.soft_limit_tracking_rate_factor = request->soft_limit_tracking_rate_factor();
    
    // ── HA axis physical parameters ──────────────────────────────
    // Only override if the sub-message is present (has_ha_axis_params).
    // For each scalar inside, only override non-zero values to avoid
    // overwriting existing calibration data with proto3 defaults.
    if (request->has_ha_axis_params()) {
        const auto& ha_params = request->ha_axis_params();
        if (ha_params.position_counts_per_degree() != 0.0) config.mount_config.ha_axis_params.position_counts_per_degree = ha_params.position_counts_per_degree();
        if (ha_params.velocity_counts_per_deg_s() != 0.0) config.mount_config.ha_axis_params.velocity_counts_per_deg_s = ha_params.velocity_counts_per_deg_s();
        if (ha_params.encoder_resolution() != 0.0) config.mount_config.ha_axis_params.encoder_resolution = ha_params.encoder_resolution();
        if (ha_params.encoder_counts_per_arcsec() != 0.0) config.mount_config.ha_axis_params.encoder_counts_per_arcsec = ha_params.encoder_counts_per_arcsec();
        if (ha_params.encoder_quantization_error() != 0.0) config.mount_config.ha_axis_params.encoder_quantization_error = ha_params.encoder_quantization_error();
        if (ha_params.gear_ratio() != 0.0) config.mount_config.ha_axis_params.gear_ratio = ha_params.gear_ratio();
        if (ha_params.worm_ratio() != 0.0) config.mount_config.ha_axis_params.worm_ratio = ha_params.worm_ratio();
        if (ha_params.worm_teeth() != 0) config.mount_config.ha_axis_params.worm_teeth = ha_params.worm_teeth();
        if (ha_params.worm_wheel_teeth() != 0) config.mount_config.ha_axis_params.worm_wheel_teeth = ha_params.worm_wheel_teeth();
        if (ha_params.cyclic_error_amplitude() != 0.0) config.mount_config.ha_axis_params.cyclic_error_amplitude = ha_params.cyclic_error_amplitude();
        if (ha_params.cyclic_error_period() != 0.0) config.mount_config.ha_axis_params.cyclic_error_period = ha_params.cyclic_error_period();
        
        // Copy cyclic harmonics (only if non-empty)
        if (ha_params.cyclic_harmonics_size() > 0) {
            for (int i = 0; i < ha_params.cyclic_harmonics_size() && i < 8; ++i) {
                config.mount_config.ha_axis_params.cyclic_harmonics[i] = ha_params.cyclic_harmonics(i);
            }
        }
        
        if (ha_params.backlash() != 0.0) config.mount_config.ha_axis_params.backlash = ha_params.backlash();
        if (ha_params.backlash_temp_coeff() != 0.0) config.mount_config.ha_axis_params.backlash_temp_coeff = ha_params.backlash_temp_coeff();
        if (ha_params.axis_stiffness() != 0.0) config.mount_config.ha_axis_params.axis_stiffness = ha_params.axis_stiffness();
        if (ha_params.torsional_compliance() != 0.0) config.mount_config.ha_axis_params.torsional_compliance = ha_params.torsional_compliance();
        if (ha_params.expansion_coeff() != 0.0) config.mount_config.ha_axis_params.expansion_coeff = ha_params.expansion_coeff();
        if (ha_params.temp_gear_error_coeff() != 0.0) config.mount_config.ha_axis_params.temp_gear_error_coeff = ha_params.temp_gear_error_coeff();
        if (ha_params.calibration_temp() != 0.0) config.mount_config.ha_axis_params.calibration_temp = ha_params.calibration_temp();
        
        // Copy calibration table (only if non-empty)
        if (ha_params.calibration_table_size() > 0) {
            config.mount_config.ha_axis_params.calibration_table.clear();
            for (int i = 0; i < ha_params.calibration_table_size(); ++i) {
                config.mount_config.ha_axis_params.calibration_table.push_back(ha_params.calibration_table(i));
            }
        }
    }
    
    // ── Dec axis physical parameters ─────────────────────────────
    if (request->has_dec_axis_params()) {
        const auto& dec_params = request->dec_axis_params();
        if (dec_params.position_counts_per_degree() != 0.0) config.mount_config.dec_axis_params.position_counts_per_degree = dec_params.position_counts_per_degree();
        if (dec_params.velocity_counts_per_deg_s() != 0.0) config.mount_config.dec_axis_params.velocity_counts_per_deg_s = dec_params.velocity_counts_per_deg_s();
        if (dec_params.encoder_resolution() != 0.0) config.mount_config.dec_axis_params.encoder_resolution = dec_params.encoder_resolution();
        if (dec_params.encoder_counts_per_arcsec() != 0.0) config.mount_config.dec_axis_params.encoder_counts_per_arcsec = dec_params.encoder_counts_per_arcsec();
        if (dec_params.encoder_quantization_error() != 0.0) config.mount_config.dec_axis_params.encoder_quantization_error = dec_params.encoder_quantization_error();
        if (dec_params.gear_ratio() != 0.0) config.mount_config.dec_axis_params.gear_ratio = dec_params.gear_ratio();
        if (dec_params.worm_ratio() != 0.0) config.mount_config.dec_axis_params.worm_ratio = dec_params.worm_ratio();
        if (dec_params.worm_teeth() != 0) config.mount_config.dec_axis_params.worm_teeth = dec_params.worm_teeth();
        if (dec_params.worm_wheel_teeth() != 0) config.mount_config.dec_axis_params.worm_wheel_teeth = dec_params.worm_wheel_teeth();
        if (dec_params.cyclic_error_amplitude() != 0.0) config.mount_config.dec_axis_params.cyclic_error_amplitude = dec_params.cyclic_error_amplitude();
        if (dec_params.cyclic_error_period() != 0.0) config.mount_config.dec_axis_params.cyclic_error_period = dec_params.cyclic_error_period();
        
        // Copy cyclic harmonics (only if non-empty)
        if (dec_params.cyclic_harmonics_size() > 0) {
            for (int i = 0; i < dec_params.cyclic_harmonics_size() && i < 8; ++i) {
                config.mount_config.dec_axis_params.cyclic_harmonics[i] = dec_params.cyclic_harmonics(i);
            }
        }
        
        if (dec_params.backlash() != 0.0) config.mount_config.dec_axis_params.backlash = dec_params.backlash();
        if (dec_params.backlash_temp_coeff() != 0.0) config.mount_config.dec_axis_params.backlash_temp_coeff = dec_params.backlash_temp_coeff();
        if (dec_params.axis_stiffness() != 0.0) config.mount_config.dec_axis_params.axis_stiffness = dec_params.axis_stiffness();
        if (dec_params.torsional_compliance() != 0.0) config.mount_config.dec_axis_params.torsional_compliance = dec_params.torsional_compliance();
        if (dec_params.expansion_coeff() != 0.0) config.mount_config.dec_axis_params.expansion_coeff = dec_params.expansion_coeff();
        if (dec_params.temp_gear_error_coeff() != 0.0) config.mount_config.dec_axis_params.temp_gear_error_coeff = dec_params.temp_gear_error_coeff();
        if (dec_params.calibration_temp() != 0.0) config.mount_config.dec_axis_params.calibration_temp = dec_params.calibration_temp();
        
        // Copy calibration table (only if non-empty)
        if (dec_params.calibration_table_size() > 0) {
            config.mount_config.dec_axis_params.calibration_table.clear();
            for (int i = 0; i < dec_params.calibration_table_size(); ++i) {
                config.mount_config.dec_axis_params.calibration_table.push_back(dec_params.calibration_table(i));
            }
        }
    }
    
    if (controller_.updateConfiguration(config)) {
        return grpc::Status::OK;
    } else {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to update configuration");
    }
} catch (const std::exception& e) {
    return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
}
}

// Helper methods
astro_mount::Coordinates MountControllerServiceImpl::convertCoordinatesToProto(double ra, double dec) const {
    astro_mount::Coordinates coords;
    coords.set_ra(ra);
    coords.set_dec(dec);
    return coords;
}

astro_mount::MountPosition MountControllerServiceImpl::convertMountPositionToProto(double axis1, double axis2) const {
    astro_mount::MountPosition position;
    position.set_axis1(axis1);
    position.set_axis2(axis2);
    *position.mutable_timestamp() = TimeUtil::GetCurrentTime();
    return position;
}

astro_mount::ControllerState::MountStatus MountControllerServiceImpl::convertStatus(
    int status_int) const {
    auto status = static_cast<controllers::MountController::MountStatus::State>(status_int);
    switch (status) {
        case controllers::MountController::MountStatus::State::UNINITIALIZED:
            return astro_mount::ControllerState::UNKNOWN;
        case controllers::MountController::MountStatus::State::INITIALIZING:
            return astro_mount::ControllerState::UNKNOWN;
        case controllers::MountController::MountStatus::State::IDLE:
            return astro_mount::ControllerState::IDLE;
        case controllers::MountController::MountStatus::State::SLEWING:
            return astro_mount::ControllerState::SLEWING;
        case controllers::MountController::MountStatus::State::TRACKING:
            return astro_mount::ControllerState::TRACKING;
        case controllers::MountController::MountStatus::State::PARKING:
            return astro_mount::ControllerState::PARKED;
        case controllers::MountController::MountStatus::State::PARKED:
            return astro_mount::ControllerState::PARKED;
        case controllers::MountController::MountStatus::State::ERROR:
            return astro_mount::ControllerState::ERROR;
        default:
            return astro_mount::ControllerState::UNKNOWN;
    }
}

// Implementation of newly added RPC methods

grpc::Status MountControllerServiceImpl::SlewToHorizontal(grpc::ServerContext* context,
                                                         const astro_mount::HorizontalCoordinates* request,
                                                         google::protobuf::Empty* response) {
    try {
        if (controller_.slewToHorizontal(request->altitude(), request->azimuth())) {
            return grpc::Status::OK;
        } else {
            return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to start slew to horizontal coordinates");
        }
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::Unpark(grpc::ServerContext* context,
                                               const google::protobuf::Empty* request,
                                               google::protobuf::Empty* response) {
    try {
        controller_.unpark();
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::RunTPointCalibration(grpc::ServerContext* context,
                                                             const google::protobuf::Empty* request,
                                                             google::protobuf::Empty* response) {
    try {
        if (controller_.runTPointCalibration()) {
            return grpc::Status::OK;
        } else {
            return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to run TPOINT calibration");
        }
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

// Health check implementation
grpc::Status MountControllerServiceImpl::CheckHealth(grpc::ServerContext* context,
                                                    const astro_mount::HealthCheckRequest* request,
                                                    astro_mount::HealthCheckResponse* response) {
    try {
        API_LOG_INFO("Health check requested for service: {}", request->service());
        
        // Set basic response
        response->set_service(request->service());
        response->set_status(astro_mount::HealthCheckResponse::SERVING);
        
        // Collect metrics
        auto system_metrics = collectSystemMetrics();
        auto mount_metrics = collectMountMetrics();
        auto kalman_metrics = collectKalmanMetrics();
        auto tpoint_metrics = collectTPointMetrics();
        
        system_metrics.set_allocated_mount_metrics(new astro_mount::MountControllerMetrics(mount_metrics));
        system_metrics.set_allocated_kalman_metrics(new astro_mount::KalmanFilterMetrics(kalman_metrics));
        system_metrics.set_allocated_tpoint_metrics(new astro_mount::TPointMetrics(tpoint_metrics));
        
        response->set_allocated_metrics(new astro_mount::SystemMetrics(system_metrics));
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        API_LOG_ERROR("Health check failed: {}", e.what());
        response->set_service(request->service());
        response->set_status(astro_mount::HealthCheckResponse::NOT_SERVING);
        return grpc::Status::OK; // Still return OK so client gets the NOT_SERVING status
    }
}

// Health check helper methods
astro_mount::SystemMetrics MountControllerServiceImpl::collectSystemMetrics() const {
    astro_mount::SystemMetrics metrics;
    
    // Collect system metrics
    try {
        // Get CPU usage (simplified - would use system calls in real implementation)
        static double cpu_usage = 0.0;
        static int request_count = 0;
        static std::chrono::system_clock::time_point last_check = std::chrono::system_clock::now();
        
        auto now = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_check).count();
        
        // Simulate CPU usage based on activity
        if (elapsed > 1000) { // Update every second
            // Simple simulation: increase CPU usage with activity
            cpu_usage = std::min(100.0, cpu_usage + 0.1);
            if (request_count > 0) {
                cpu_usage = std::min(100.0, 5.0 + request_count * 0.5);
            }
            last_check = now;
        }
        
        // Get memory usage
        double memory_usage_mb = 0.0;
#ifdef __linux__
        // Read memory usage from /proc/self/statm
        std::ifstream statm("/proc/self/statm");
        if (statm.is_open()) {
            long size, resident, shared, text, lib, data, dt;
            statm >> size >> resident >> shared >> text >> lib >> data >> dt;
            memory_usage_mb = resident * sysconf(_SC_PAGESIZE) / (1024.0 * 1024.0);
        }
#elif defined(_WIN32)
        // Windows implementation would use GetProcessMemoryInfo
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            memory_usage_mb = pmc.WorkingSetSize / (1024.0 * 1024.0);
        }
#endif
        
        // Track connection and request statistics
        static std::atomic<uint64_t> total_requests_{0};
        static std::atomic<uint64_t> error_count_{0};
        static std::atomic<uint64_t> active_connections_{1};
        
        // Update metrics
        metrics.set_cpu_usage_percent(cpu_usage);
        metrics.set_memory_usage_mb(memory_usage_mb);
        metrics.set_active_connections(active_connections_.load());
        metrics.set_total_requests(total_requests_.load());
        metrics.set_error_count(error_count_.load());
        
        // Calculate average response time (simplified)
        static std::deque<double> response_times;
        static double total_response_time = 0.0;
        
        if (!response_times.empty()) {
            metrics.set_avg_response_time_ms(total_response_time / response_times.size());
        } else {
            metrics.set_avg_response_time_ms(0.0);
        }
        
        // Add system uptime
        static auto start_time = std::chrono::system_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
        // metrics.mutable_extra_metrics()->insert({"uptime_seconds", std::to_string(uptime)});
        
        // Add thread count
        unsigned int thread_count = std::thread::hardware_concurrency();
        // metrics.mutable_extra_metrics()->insert({"thread_count", std::to_string(thread_count)});
        
        // Add disk usage if database directory exists
        try {
            auto db_path = std::filesystem::path("db");
            if (std::filesystem::exists(db_path)) {
                auto space_info = std::filesystem::space(db_path);
                double free_gb = space_info.free / (1024.0 * 1024.0 * 1024.0);
                double total_gb = space_info.capacity / (1024.0 * 1024.0 * 1024.0);
                // metrics.mutable_extra_metrics()->insert({"db_free_gb", std::to_string(free_gb)});
                // metrics.mutable_extra_metrics()->insert({"db_total_gb", std::to_string(total_gb)});
            }
        } catch (const std::exception&) {
            // Ignore disk space errors
        }
        
    } catch (const std::exception& e) {
        API_LOG_WARN("Failed to collect system metrics: {}", e.what());
        // Set default values on error
        metrics.set_cpu_usage_percent(0.0);
        metrics.set_memory_usage_mb(0.0);
        metrics.set_active_connections(1);
        metrics.set_total_requests(0);
        metrics.set_error_count(0);
        metrics.set_avg_response_time_ms(0.0);
    }
    
    return metrics;
}

astro_mount::MountControllerMetrics MountControllerServiceImpl::collectMountMetrics() const {
    astro_mount::MountControllerMetrics metrics;
    
    auto status = controller_.getStatus();
    
    metrics.set_tracking_error_ra_avg(status.tracking_error_ra);
    metrics.set_tracking_error_dec_avg(status.tracking_error_dec);
    metrics.set_tracking_error_max(std::max(status.tracking_error_ra, status.tracking_error_dec));
    metrics.set_slew_count(static_cast<int32_t>(controller_.getSlewCount()));
    metrics.set_track_count(static_cast<int32_t>(controller_.getTrackCount()));
    metrics.set_calibration_count(static_cast<int32_t>(controller_.getCalibrationCount()));
    metrics.set_encoders_active(status.encoders_active);
    metrics.set_guider_active(status.guider_active);
    
    return metrics;
}

astro_mount::KalmanFilterMetrics MountControllerServiceImpl::collectKalmanMetrics() const {
    astro_mount::KalmanFilterMetrics metrics;
    
    // Use configured noise values and derive innovation from tracking error
    auto config = controller_.getConfiguration();
    metrics.set_process_noise(config.mount_config.process_noise);
    metrics.set_measurement_noise(config.mount_config.measurement_noise);
    
    // Innovation norm = Euclidean norm of tracking error vector (arcseconds)
    auto status = controller_.getStatus();
    double innovation = std::sqrt(
        status.tracking_error_ra * status.tracking_error_ra +
        status.tracking_error_dec * status.tracking_error_dec);
    metrics.set_innovation_norm(innovation);
    
    // Tracking loop iteration metrics
    size_t update_count = controller_.getTrackingIterationCount();
    metrics.set_update_count(static_cast<int32_t>(update_count));
    
    double total_time_ms = controller_.getTotalUpdateTimeMs();
    if (update_count > 0) {
        metrics.set_avg_update_time_ms(total_time_ms / static_cast<double>(update_count));
    } else {
        metrics.set_avg_update_time_ms(0.0);
    }
    
    return metrics;
}

astro_mount::TPointMetrics MountControllerServiceImpl::collectTPointMetrics() const {
    astro_mount::TPointMetrics metrics;
    
    auto status = controller_.getStatus();
    metrics.set_measurement_count(static_cast<int32_t>(controller_.getTPointMeasurementCount()));
    metrics.set_residual_max(controller_.getTPointResidualMaxArcsec());
    metrics.set_residual_rms(controller_.getTPointResidualRmsArcsec());
    metrics.set_chi_squared(controller_.getTPointChiSquared());
    metrics.set_calibrated(status.tpoint_calibrated);
    
    return metrics;
}

// ============================================
// Helper: convert protobuf Timestamp to system_clock::time_point
// ============================================
static std::chrono::system_clock::time_point protoTimestampToTimePoint(
    const google::protobuf::Timestamp& ts) {
    auto seconds = std::chrono::seconds(ts.seconds());
    auto nanos = std::chrono::nanoseconds(ts.nanos());
    return std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(seconds + nanos));
}

// ============================================
// Helper: convert EphemerisPoint repeated field to vector of tuples
// ============================================
static std::vector<std::tuple<std::chrono::system_clock::time_point,
                               double, double, double, double>>
ephemerisPointsToTuples(const google::protobuf::RepeatedPtrField<astro_mount::EphemerisPoint>& points) {
    std::vector<std::tuple<std::chrono::system_clock::time_point,
                            double, double, double, double>> result;
    result.reserve(points.size());
    for (const auto& point : points) {
        auto tp = protoTimestampToTimePoint(point.timestamp());
        result.emplace_back(tp, point.ra(), point.dec(), point.ra_rate(), point.dec_rate());
    }
    return result;
}

// ============================================
// Helper: convert EphemerisTrackStatus (from model) into proto response
// ============================================
static void fillEphemerisTrackStatusProto(
    const ::astro_mount::EphemerisTrackStatus& src,
    astro_mount::EphemerisTrackStatus* dst) {
    
    dst->set_state(src.state());
    dst->set_object_id(src.object_id());
    dst->set_object_name(src.object_name());
    
    if (src.has_current_time()) {
        dst->mutable_current_time()->CopyFrom(src.current_time());
    } else {
        // Set current time if not provided
        auto now = std::chrono::system_clock::now();
        auto ts = google::protobuf::Timestamp();
        ts.set_seconds(std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count());
        ts.set_nanos(std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch() % std::chrono::seconds(1)).count());
        dst->mutable_current_time()->CopyFrom(ts);
    }
    
    if (src.has_track_start_time()) {
        dst->mutable_track_start_time()->CopyFrom(src.track_start_time());
    }
    if (src.has_track_end_time()) {
        dst->mutable_track_end_time()->CopyFrom(src.track_end_time());
    }
    if (src.has_current_position()) {
        dst->mutable_current_position()->CopyFrom(src.current_position());
    }
    if (src.has_target_position()) {
        dst->mutable_target_position()->CopyFrom(src.target_position());
    }
    
    dst->set_position_error_arcsec(src.position_error_arcsec());
    dst->set_ra_rate(src.ra_rate());
    dst->set_dec_rate(src.dec_rate());
    dst->set_time_remaining_seconds(src.time_remaining_seconds());
    dst->set_earth_rotation_corrected(src.earth_rotation_corrected());
    dst->set_error_message(src.error_message());
    
    // Copy warnings
    for (int i = 0; i < src.warnings_size(); ++i) {
        dst->add_warnings(src.warnings(i));
    }
    
    // Copy tracking parameters map
    for (const auto& entry : src.tracking_parameters()) {
        (*dst->mutable_tracking_parameters())[entry.first] = entry.second;
    }
}

// ============================================
// Ephemeris-based tracking implementations
// ============================================

grpc::Status MountControllerServiceImpl::UploadEphemeris(
    grpc::ServerContext* context,
    const astro_mount::EphemerisData* request,
    google::protobuf::Empty* response) {
    
    try {
        API_LOG_INFO("UploadEphemeris called for object: {} ({})",
                    request->object_name(), request->object_id());
        
        auto points = ephemerisPointsToTuples(request->points());
        int interpolation_order = static_cast<int>(request->interpolation_order());
        if (interpolation_order < 1) interpolation_order = 3;
        
        if (!controller_.uploadEphemeris(
                request->object_id(),
                request->object_name(),
                request->object_type(),
                points,
                interpolation_order)) {
            return grpc::Status(grpc::StatusCode::INTERNAL,
                               "Failed to upload ephemeris data");
        }
        
        API_LOG_INFO("Ephemeris uploaded successfully: {} points for object '{}'",
                    points.size(), request->object_id());
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        API_LOG_ERROR("UploadEphemeris failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL,
                           std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::StartEphemerisTracking(
    grpc::ServerContext* context,
    const astro_mount::StartEphemerisTrackingRequest* request,
    astro_mount::EphemerisTrackStatus* response) {
    
    try {
        API_LOG_INFO("StartEphemerisTracking called for object: {}",
                    request->object_id());
        
        // Convert start_time from proto
        std::chrono::system_clock::time_point start_time;
        if (request->has_start_time()) {
            start_time = protoTimestampToTimePoint(request->start_time());
        } else {
            start_time = std::chrono::system_clock::now();
        }
        
        // Use slew_margin_seconds as lead_time_seconds
        double lead_time = request->slew_margin_seconds();
        if (lead_time <= 0.0) lead_time = 30.0;
        
        // Call controller API
        std::string tracker_id = controller_.startEphemerisTracking(
            request->object_id(),
            start_time,
            lead_time,
            request->wait_at_start(),
            false,        // enable_prediction (not in proto)
            1.0,          // prediction_interval_hours (default)
            "continuous", // tracking_mode (default)
            0.0,          // custom_rate_ra (default)
            0.0);         // custom_rate_dec (default)
        
        if (tracker_id.empty()) {
            response->set_state(astro_mount::EphemerisTrackStatus::ERROR);
            response->set_object_id(request->object_id());
            response->set_error_message("Failed to start ephemeris tracking: object not found or already tracking");
            
            auto now = std::chrono::system_clock::now();
            auto ts = google::protobuf::Timestamp();
            ts.set_seconds(std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count());
            ts.set_nanos(std::chrono::duration_cast<std::chrono::nanoseconds>(
                now.time_since_epoch() % std::chrono::seconds(1)).count());
            *response->mutable_current_time() = ts;
            
            return grpc::Status(grpc::StatusCode::NOT_FOUND,
                               "Object not found or tracking already active");
        }
        
        API_LOG_INFO("Ephemeris tracking started: tracker_id={}, object={}",
                    tracker_id, request->object_id());
        
        // Fetch and return the current tracking status
        auto status = controller_.getEphemerisTrackStatus(tracker_id);
        fillEphemerisTrackStatusProto(status, response);
        
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        API_LOG_ERROR("StartEphemerisTracking failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL,
                           std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::StartEphemerisTrackingWithData(
    grpc::ServerContext* context,
    const astro_mount::EphemerisTrackRequest* request,
    astro_mount::EphemerisTrackStatus* response) {
    
    try {
        API_LOG_INFO("StartEphemerisTrackingWithData called for object: {}",
                    request->ephemeris().object_name());
        
        const auto& ephemeris = request->ephemeris();
        auto points = ephemerisPointsToTuples(ephemeris.points());
        int interpolation_order = static_cast<int>(ephemeris.interpolation_order());
        if (interpolation_order < 1) interpolation_order = 3;
        
        // Convert start_time from proto
        std::chrono::system_clock::time_point start_time;
        if (request->auto_start()) {
            start_time = std::chrono::system_clock::now();
        } else {
            // Use the first point's timestamp as start time
            if (!points.empty()) {
                start_time = std::get<0>(points.front());
            } else {
                start_time = std::chrono::system_clock::now();
            }
        }
        
        double lead_time = request->lead_time_seconds();
        if (lead_time <= 0.0) lead_time = 30.0;
        
        std::string tracking_mode = request->tracking_mode();
        if (tracking_mode.empty()) tracking_mode = "continuous";
        
        // Call controller API (combines upload + start)
        std::string tracker_id = controller_.startEphemerisTrackingWithData(
            ephemeris.object_id(),
            ephemeris.object_name(),
            ephemeris.object_type(),
            points,
            start_time,
            lead_time,
            interpolation_order,
            tracking_mode);
        
        if (tracker_id.empty()) {
            response->set_state(astro_mount::EphemerisTrackStatus::ERROR);
            response->set_object_id(ephemeris.object_id());
            response->set_object_name(ephemeris.object_name());
            response->set_error_message("Failed to start ephemeris tracking with data");
            
            auto now = std::chrono::system_clock::now();
            auto ts = google::protobuf::Timestamp();
            ts.set_seconds(std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count());
            ts.set_nanos(std::chrono::duration_cast<std::chrono::nanoseconds>(
                now.time_since_epoch() % std::chrono::seconds(1)).count());
            *response->mutable_current_time() = ts;
            
            return grpc::Status(grpc::StatusCode::INTERNAL,
                               "Failed to start ephemeris tracking with data");
        }
        
        API_LOG_INFO("Ephemeris tracking started with data: tracker_id={}, object='{}' ({} points)",
                    tracker_id, ephemeris.object_name(), points.size());
        
        // Fetch and return the current tracking status
        auto status = controller_.getEphemerisTrackStatus(tracker_id);
        fillEphemerisTrackStatusProto(status, response);
        
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        API_LOG_ERROR("StartEphemerisTrackingWithData failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL,
                           std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::GetEphemerisTrackStatus(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    astro_mount::EphemerisTrackStatus* response) {
    
    try {
        API_LOG_INFO("GetEphemerisTrackStatus called");
        
        // Get all active tracker IDs
        auto active_trackers = controller_.getActiveEphemerisTrackers();
        
        if (active_trackers.empty()) {
            // No active tracking
            response->set_state(astro_mount::EphemerisTrackStatus::IDLE);
            response->set_object_id("");
            response->set_object_name("No active tracking");
            
            auto now = std::chrono::system_clock::now();
            auto ts = google::protobuf::Timestamp();
            ts.set_seconds(std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count());
            ts.set_nanos(std::chrono::duration_cast<std::chrono::nanoseconds>(
                now.time_since_epoch() % std::chrono::seconds(1)).count());
            *response->mutable_current_time() = ts;
            
            return grpc::Status::OK;
        }
        
        // Return status of the first active tracker
        auto status = controller_.getEphemerisTrackStatus(active_trackers[0]);
        fillEphemerisTrackStatusProto(status, response);
        
        // Add a warning if there are multiple active trackers
        if (active_trackers.size() > 1) {
            response->add_warnings(
                "Multiple trackers active (" + std::to_string(active_trackers.size()) +
                "); showing status for: " + active_trackers[0]);
        }
        
        API_LOG_DEBUG("EphemerisTrackStatus: state={}, object={}",
                     static_cast<int>(response->state()), response->object_id());
        
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        API_LOG_ERROR("GetEphemerisTrackStatus failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL,
                           std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::StopEphemerisTracking(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    google::protobuf::Empty* response) {
    
    try {
        API_LOG_INFO("StopEphemerisTracking called");
        
        // Get all active tracker IDs
        auto active_trackers = controller_.getActiveEphemerisTrackers();
        
        if (active_trackers.empty()) {
            API_LOG_INFO("No active ephemeris tracking to stop");
            return grpc::Status::OK;
        }
        
        // Stop all active trackers (proto request has no tracker_id field)
        size_t stopped_count = 0;
        for (const auto& tracker_id : active_trackers) {
            if (controller_.stopEphemerisTracking(tracker_id)) {
                ++stopped_count;
                API_LOG_DEBUG("Stopped ephemeris tracker: {}", tracker_id);
            } else {
                API_LOG_WARN("Failed to stop ephemeris tracker: {}", tracker_id);
            }
        }
        
        API_LOG_INFO("Ephemeris tracking stopped: {}/{} trackers",
                    stopped_count, active_trackers.size());
        
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        API_LOG_ERROR("StopEphemerisTracking failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL,
                           std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::GetEphemerisMetrics(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    astro_mount::EphemerisMetrics* response) {
    
    try {
        API_LOG_INFO("GetEphemerisMetrics called");
        
        // Delegate to controller which returns the EphemerisMetrics proto directly
        auto metrics = controller_.getEphemerisMetrics();
        response->CopyFrom(metrics);
        
        API_LOG_DEBUG("EphemerisMetrics: object={}, track_time={}s, avg_error={:.2f}arcsec",
                     metrics.object_id(), metrics.total_track_time_seconds(),
                     metrics.avg_position_error_arcsec());
        
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        API_LOG_ERROR("GetEphemerisMetrics failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL,
                           std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::ClearEphemerisCache(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    google::protobuf::Empty* response) {
    
    try {
        API_LOG_INFO("ClearEphemerisCache called");
        
        controller_.clearEphemerisCache();
        
        API_LOG_INFO("Ephemeris cache cleared successfully");
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        API_LOG_ERROR("ClearEphemerisCache failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL,
                           std::string("Error: ") + e.what());
    }
}

// ============================================
// Low-level axis control API for uncalibrated mounts
// ============================================

grpc::Status MountControllerServiceImpl::ControlAxis(
    grpc::ServerContext* context,
    const astro_mount::AxisControlRequest* request,
    google::protobuf::Empty* response) {
    
    try {
        API_LOG_INFO("ControlAxis called: axis_id={}, mode={} target_pos={:.4f} relative={}",
                     request->axis_id(),
                     request->mode() == astro_mount::AxisControlMode::POSITION_CONTROL ? "POSITION" : "VELOCITY",
                     request->target_position(),
                     (int)request->relative());
        
        // Validate axis ID
        int axis_id = request->axis_id();
        if (axis_id < 0 || axis_id > 1) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                              "Invalid axis_id. Must be 0 (HA/RA/Azimuth) or 1 (Dec/Altitude)");
        }
        
        // Get configuration for default values
        auto config = controller_.getConfiguration();
        double default_velocity = config.mount_config.max_slew_rate;
        double slew_accel = config.mount_config.slew_acceleration;
        double track_accel = config.mount_config.tracking_acceleration;
        
        int mode = 1;  // VELOCITY_CONTROL
        double target_position = 0.0;
        double target_velocity = 0.0;
        double acceleration = track_accel;
        bool relative = request->relative();
        
        if (request->mode() == astro_mount::AxisControlMode::POSITION_CONTROL) {
            // Position control mode – uses slew acceleration
            mode = 0;  // POSITION_CONTROL
            target_position = request->target_position();
            double max_velocity = request->max_velocity() > 0 ? request->max_velocity() : default_velocity;
            acceleration = request->acceleration() > 0 ? request->acceleration() : slew_accel;
            
            API_LOG_INFO("Setting position target: axis={}, position={}°, velocity={}°/s, acceleration={}°/s²",
                        axis_id, target_position, max_velocity, acceleration);
            
            if (!controller_.controlAxis(axis_id, mode, target_position, max_velocity,
                                         acceleration, relative)) {
                return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to set position target");
            }
        } else if (request->mode() == astro_mount::AxisControlMode::VELOCITY_CONTROL) {
            // Velocity control mode – uses tracking acceleration
            mode = 1;  // VELOCITY_CONTROL
            target_velocity = request->target_velocity();
            acceleration = request->acceleration() > 0 ? request->acceleration() : track_accel;
            
            // Clamp velocity to configured maximum
            if (std::abs(target_velocity) > default_velocity) {
                API_LOG_WARN("Velocity {:.2f}°/s exceeds max {:.2f}°/s, clamping",
                            target_velocity, default_velocity);
                target_velocity = std::copysign(default_velocity, target_velocity);
            }
            
            API_LOG_INFO("Setting velocity target: axis={}, velocity={}°/s, acceleration={}°/s²",
                        axis_id, target_velocity, acceleration);
            
            if (!controller_.controlAxis(axis_id, mode, target_position, target_velocity,
                                         acceleration, relative)) {
                return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to set velocity target");
            }
        }
        
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        API_LOG_ERROR("ControlAxis failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL,
                           std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::StopAxis(
    grpc::ServerContext* context,
    const astro_mount::AxisStopRequest* request,
    google::protobuf::Empty* response) {
    
    try {
        API_LOG_INFO("StopAxis called: axis_id={}, decelerate={}", 
                     request->axis_id(), request->decelerate());
        
        // Validate axis ID
        int axis_id = request->axis_id();
        if (axis_id < 0 || axis_id > 1) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                              "Invalid axis_id. Must be 0 (HA/RA/Azimuth) or 1 (Dec/Altitude)");
        }
        
        if (!controller_.stopAxis(axis_id, request->decelerate(), request->deceleration())) {
            return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to stop axis");
        }
        
        API_LOG_INFO("Axis {} stopped (decelerate={})", axis_id, request->decelerate());
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        API_LOG_ERROR("StopAxis failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL,
                           std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::EmergencyStop(
    grpc::ServerContext* context,
    const astro_mount::EmergencyStopRequest* request,
    google::protobuf::Empty* response) {
    
    try {
        API_LOG_INFO("EmergencyStop called: axis_id={}, reset_after={}", 
                     request->axis_id(), request->reset_after());
        
        // Validate axis ID
        int axis_id = request->axis_id();
        if (axis_id < -1 || axis_id > 1) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                              "Invalid axis_id. Must be -1 (all axes), 0 (HA/RA/Azimuth), or 1 (Dec/Altitude)");
        }
        
        if (!controller_.emergencyStop(axis_id, request->reset_after())) {
            return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to execute emergency stop");
        }
        
        if (axis_id == -1) {
            API_LOG_WARN("Emergency stop on all axes");
        } else {
            API_LOG_WARN("Emergency stop on axis {}", axis_id);
        }
        
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        API_LOG_ERROR("EmergencyStop failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL,
                           std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::GetAxisStatus(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    astro_mount::AxisStatus* response) {
    
    try {
        API_LOG_INFO("GetAxisStatus called");
        
        if (!controller_.getAxisStatus(*response)) {
            return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to retrieve axis status");
        }
        
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        API_LOG_ERROR("GetAxisStatus failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL,
                           std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::Home(
    grpc::ServerContext* context,
    const astro_mount::MountHomingRequest* request,
    google::protobuf::Empty* response) {
    
    try {
        API_LOG_INFO("Home called: axis1={:.4f}°, axis2={:.4f}°",
                     request->axis1(), request->axis2());
        
        if (!controller_.home(*request)) {
            return grpc::Status(grpc::StatusCode::INTERNAL,
                               "Failed to home mount — mount must be IDLE or ERROR state");
        }
        
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        API_LOG_ERROR("Home failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL,
                           std::string("Error: ") + e.what());
    }
}

// ============================================
// HAL Configuration RPC Handlers
// ============================================

grpc::Status MountControllerServiceImpl::GetHALConfig(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    astro_mount::HALConfig* response) {
    
    try {
        API_LOG_INFO("GetHALConfig called");
        
        if (!controller_.getHALConfig(*response)) {
            return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                               "HAL interface not available");
        }
        
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        API_LOG_ERROR("GetHALConfig failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL,
                           std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::SetHALConfig(
    grpc::ServerContext* context,
    const astro_mount::HALConfigRequest* request,
    google::protobuf::Empty* response) {
    
    try {
        API_LOG_INFO("SetHALConfig called");
        
        if (!controller_.setHALConfig(*request)) {
            return grpc::Status(grpc::StatusCode::INTERNAL,
                               "Failed to set HAL configuration");
        }
        
        API_LOG_INFO("HAL configuration updated successfully");
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        API_LOG_ERROR("SetHALConfig failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL,
                           std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::GetHALStatus(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    astro_mount::HALStatus* response) {
    
    try {
        API_LOG_INFO("GetHALStatus called");
        
        if (!controller_.getHALStatus(*response)) {
            return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                               "HAL interface not available");
        }
        
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        API_LOG_ERROR("GetHALStatus failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL,
                           std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::ReinitializeHAL(
    grpc::ServerContext* context,
    const astro_mount::HALReinitRequest* request,
    google::protobuf::Empty* response) {
    
    try {
        API_LOG_INFO("ReinitializeHAL called (force_restart={})", request->force_restart());
        
        if (!controller_.reinitializeHAL(*request)) {
            return grpc::Status(grpc::StatusCode::INTERNAL,
                               "Failed to reinitialize HAL");
        }
        
        API_LOG_INFO("HAL reinitialized successfully");
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        API_LOG_ERROR("ReinitializeHAL failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL,
                           std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::RestartController(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    google::protobuf::Empty* response) {
    try {
        API_LOG_INFO("RestartController (soft) called");
        if (!controller_.restart()) {
            return grpc::Status(grpc::StatusCode::INTERNAL, "Soft restart failed");
        }
        API_LOG_INFO("RestartController (soft) completed successfully");
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        API_LOG_ERROR("RestartController failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::HardRestartController(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    google::protobuf::Empty* response) {
    try {
        API_LOG_INFO("HardRestartController called");
        if (!controller_.hardRestart()) {
            return grpc::Status(grpc::StatusCode::INTERNAL, "Hard restart failed");
        }
        API_LOG_INFO("HardRestartController completed successfully");
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        API_LOG_ERROR("HardRestartController failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::StartGamepad(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    google::protobuf::Empty* response) {
    
    try {
        API_LOG_INFO("StartGamepad called");
        controller_.startGamepadLoop();
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        API_LOG_ERROR("StartGamepad failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL,
                           std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::StopGamepad(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    google::protobuf::Empty* response) {
    
    try {
        API_LOG_INFO("StopGamepad called");
        controller_.stopGamepad();
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        API_LOG_ERROR("StopGamepad failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL,
                           std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::SetGamepadMode(
    grpc::ServerContext* context,
    const astro_mount::GamepadModeRequest* request,
    google::protobuf::Empty* response) {
    
    try {
        API_LOG_INFO("SetGamepadMode called with mode={}", static_cast<int>(request->mode()));
        auto mode = static_cast<controllers::MountController::GamepadMode>(request->mode());
        controller_.setGamepadMode(mode);
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        API_LOG_ERROR("SetGamepadMode failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL,
                           std::string("Error: ") + e.what());
    }
}

// ============================================
// Mount orientation (CASUAL mount type)
// ============================================

grpc::Status MountControllerServiceImpl::SetMountOrientation(
    grpc::ServerContext* context,
    const astro_mount::MountOrientation* request,
    google::protobuf::Empty* response) {
    try {
        config::MountOrientation orientation;
        orientation.quaternion[0] = request->qx();
        orientation.quaternion[1] = request->qy();
        orientation.quaternion[2] = request->qz();
        orientation.quaternion[3] = request->qw();
        
        if (controller_.setMountOrientation(orientation)) {
            API_LOG_INFO("Mount orientation set: Q=[{:.4f}, {:.4f}, {:.4f}, {:.4f}]",
                     orientation.quaternion[0], orientation.quaternion[1],
                     orientation.quaternion[2], orientation.quaternion[3]);
            return grpc::Status::OK;
        } else {
            return grpc::Status(grpc::StatusCode::INTERNAL,
                              "Failed to set mount orientation");
        }
    } catch (const std::exception& e) {
        API_LOG_ERROR("SetMountOrientation failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL,
                           std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::GetMountOrientation(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    astro_mount::MountOrientation* response) {
    try {
        auto orientation = controller_.getMountOrientation();
        response->set_qx(orientation.quaternion[0]);
        response->set_qy(orientation.quaternion[1]);
        response->set_qz(orientation.quaternion[2]);
        response->set_qw(orientation.quaternion[3]);
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        API_LOG_ERROR("GetMountOrientation failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL,
                           std::string("Error: ") + e.what());
    }
}

// ============================================
// LX200 Serial Interface Management
// ============================================

grpc::Status MountControllerServiceImpl::GetLx200Status(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    astro_mount::Lx200Status* response) {
    try {
        auto config = controller_.getConfiguration();
        response->set_enabled(config.lx200_enabled);
        response->set_running(controller_.isLx200Running());
        response->set_port(config.lx200_port);
        response->set_baud_rate(config.lx200_baud_rate);
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::StartLx200(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    astro_mount::Lx200Status* response) {
    try {
        controller_.startLx200();
        auto config = controller_.getConfiguration();
        response->set_enabled(config.lx200_enabled);
        response->set_running(controller_.isLx200Running());
        response->set_port(config.lx200_port);
        response->set_baud_rate(config.lx200_baud_rate);
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::StopLx200(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    astro_mount::Lx200Status* response) {
    try {
        controller_.stopLx200();
        auto config = controller_.getConfiguration();
        response->set_enabled(config.lx200_enabled);
        response->set_running(controller_.isLx200Running());
        response->set_port(config.lx200_port);
        response->set_baud_rate(config.lx200_baud_rate);
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

} // namespace api
} // namespace astro_mount
