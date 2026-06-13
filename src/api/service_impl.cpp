#include "api/service_impl.h"
#include "controllers/mount_controller.h"
#include "controllers/icanopen_interface.h"
#include "logging/logger.h"
#include "core/astronomical_calculations.h"
#include "proto/mount_controller.pb.h"
#include "proto/mount_controller.grpc.pb.h"
#include <google/protobuf/util/time_util.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>
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
        if (controller_.startTracking(request->ra(), request->dec())) {
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
        response->set_tracking_rate_ra(status.tracking_error_ra);
        response->set_tracking_rate_dec(status.tracking_error_dec);
        
        // Set timestamp
        *response->mutable_state_time() = TimeUtil::GetCurrentTime();
        
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
grpc::Status MountControllerServiceImpl::AddMeasurement(grpc::ServerContext* context,
                                                       const astro_mount::Measurement* request,
                                                       google::protobuf::Empty* response) {
    try {
        // Extract mount position (convert from mount_position proto if available)
        double mount_ha = 0.0;
        double mount_dec = 0.0;
        if (request->has_mount_position()) {
            // Convert mount position to HA/Dec based on timestamp and location
            auto config = controller_.getConfiguration();
            double axis1 = request->mount_position().axis1(); // degrees
            double axis2 = request->mount_position().axis2(); // degrees
            
            // Get timestamp from request (use current time if not provided)
            double jd = 0.0;
            if (request->has_timestamp()) {
                // Convert timestamp to Julian Date
                auto ts = request->timestamp();
                auto tp = std::chrono::system_clock::from_time_t(ts.seconds()) + 
                         std::chrono::nanoseconds(ts.nanos());
                auto duration = tp.time_since_epoch();
                auto days = std::chrono::duration_cast<std::chrono::duration<double, std::ratio<86400>>>(duration);
                jd = 2440587.5 + days.count(); // Unix epoch to JD
            } else {
                jd = core::AstronomicalCalculations::getCurrentJulianDate();
            }
            
            // Create astronomical calculations instance
            core::AstronomicalCalculations astro_calc;
            astro_calc.setObserverLocation(config.latitude, config.longitude, config.altitude);
            astro_calc.setEnvironmentalParams(15.0, 1013.25, 0.5); // default values
            
            if (config.mount_type == controllers::MountController::MountType::ALT_AZ) {
                // Alt-Az mount: convert altitude/azimuth to HA/Dec
                double altitude = axis2; // degrees
                double azimuth = axis1;  // degrees
                auto [ha_hours, dec] = astro_calc.horizontalToEquatorial(altitude, azimuth, jd);
                
                // HA is already in hours, convert to degrees for mount_ha
                mount_ha = ha_hours * 15.0; // hours to degrees
                mount_dec = dec;
                
                API_LOG_DEBUG("Alt-Az conversion: alt={:.3f}°, az={:.3f}° -> HA={:.3f}°, Dec={:.3f}°",
                             altitude, azimuth, mount_ha, mount_dec);
            } else if (config.mount_type == controllers::MountController::MountType::CASUAL) {
                // CASUAL mount: axis1 and axis2 directly are the mount position
                mount_ha = axis1; // axis1 in degrees (altitude-like)
                mount_dec = axis2; // axis2 in degrees (azimuth-like)
                
                API_LOG_DEBUG("CASUAL mount: axis1={:.3f}°, axis2={:.3f}°", mount_ha, mount_dec);
            } else {
                // Equatorial mount: axis1 is HA in degrees, axis2 is Dec in degrees
                mount_ha = axis1; // already in degrees
                mount_dec = axis2;
                
                // Optionally, we could convert HA to hours if needed, but keep in degrees for consistency
                API_LOG_DEBUG("Equatorial mount: HA={:.3f}°, Dec={:.3f}°", mount_ha, mount_dec);
            }
        }
        
        // Extract environmental parameters with defaults
        // Note: In proto3, scalar fields don't have has_* methods
        // We use the field value directly and apply defaults if value is 0.0 (default proto3 value)
        double temperature = request->temperature();
        double pressure = request->pressure();
        double humidity = request->humidity();
        
        // Apply defaults if values are zero (proto3 default)
        if (temperature == 0.0) temperature = 15.0;
        if (pressure == 0.0) pressure = 1013.25;
        if (humidity == 0.0) humidity = 0.5;
        
        // Extract astrometric parameters from expected coordinates
        // Note: Same proto3 limitation - no has_* methods for scalar fields
        double proper_motion_ra = request->expected().pm_ra();
        double proper_motion_dec = request->expected().pm_dec();
        double parallax = request->expected().parallax();
        double epoch = request->expected().epoch();
        
        // Apply defaults for astrometric parameters
        if (epoch == 0.0) epoch = 2000.0;
        
        if (controller_.addCalibrationMeasurement(
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
            return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to add measurement");
        }
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

// Bootstrap calibration API
grpc::Status MountControllerServiceImpl::AddBootstrapMeasurement(grpc::ServerContext* context,
                                                                const astro_mount::BootstrapMeasurement* request,
                                                                google::protobuf::Empty* response) {
    try {
        // Extract mount position (optional for bootstrap)
        double mount_ha = 0.0;
        double mount_dec = 0.0;
        if (request->has_mount_position()) {
            auto config = controller_.getConfiguration();
            if (config.mount_type == controllers::MountController::MountType::CASUAL) {
                // CASUAL mount: axis1 and axis2 are directly the mount position in degrees
                mount_ha = request->mount_position().axis1();  // axis1 in degrees
                mount_dec = request->mount_position().axis2(); // axis2 in degrees
            } else {
                // EQUATORIAL or ALT_AZ: axis1 is HA/azimuth in degrees, convert to hours
                mount_ha = request->mount_position().axis1() / 15.0; // degrees to hours
                mount_dec = request->mount_position().axis2();       // degrees
            }
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
            double rms_ra = controller_.getBootstrapRmsRaArcsec();
            double rms_dec = controller_.getBootstrapRmsDecArcsec();
            double ra_corr = controller_.getBootstrapRaCorrectionArcsec();
            double dec_corr = controller_.getBootstrapDecCorrectionArcsec();
            
            // Combined alignment error from both axes
            double alignment_error = std::sqrt(ra_corr * ra_corr + dec_corr * dec_corr);
            // Combined RMS residual
            double residual_rms = std::sqrt(rms_ra * rms_ra + rms_dec * rms_dec);
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
        auto cpp_mode = static_cast<controllers::MountController::BootstrapMode>(
            static_cast<int>(proto_mode));
        
        if (cpp_mode < controllers::MountController::BootstrapMode::BOOTSTRAP_MANUAL ||
            cpp_mode > controllers::MountController::BootstrapMode::BOOTSTRAP_AUTOMATIC) {
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
                controllers::MountController::BootstrapMode::BOOTSTRAP_AUTOMATIC) &&
            status.bootstrap_mode != static_cast<int>(
                controllers::MountController::BootstrapMode::BOOTSTRAP_HYBRID)) {
            return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                              "Bootstrap mode must be HYBRID or AUTOMATIC for auto-bootstrap");
        }
        
        // Verify absolute encoders or reference position for automatic mode
        bool has_reference = status.encoders_absolute;
        if (!has_reference && status.bootstrap_mode == static_cast<int>(
                controllers::MountController::BootstrapMode::BOOTSTRAP_AUTOMATIC)) {
            return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                              "AUTOMATIC bootstrap requires absolute encoders or prior calibration");
        }
        
        // Set the bootstrap mode on the controller
        auto mode = static_cast<controllers::MountController::BootstrapMode>(status.bootstrap_mode);
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
        response->set_latitude(config.latitude);
        response->set_longitude(config.longitude);
        response->set_altitude(config.altitude);
        response->set_mount_height(config.mount_height);
        response->set_pier_west(config.pier_west);
        response->set_pier_east(config.pier_east);
        response->set_focal_length(config.focal_length);
        response->set_aperture(config.aperture);
        response->set_default_temperature(config.default_temperature);
        response->set_default_pressure(config.default_pressure);
        response->set_default_humidity(config.default_humidity);
        response->set_process_noise(config.process_noise);
        response->set_measurement_noise(config.measurement_noise);
        response->set_log_level(config.log_level);
        response->set_log_directory(config.log_directory);
        response->set_log_rotation_days(config.log_rotation_days);
        response->set_grpc_address(config.grpc_address);
        response->set_grpc_port(config.grpc_port);
        response->set_canopen_interface(config.canopen_interface);
        response->set_canopen_node_id(config.canopen_node_id);
        
        // Mount control parameters
        response->set_max_slew_rate(config.max_slew_rate);
        response->set_max_tracking_rate(config.max_tracking_rate);
        response->set_slew_acceleration(config.slew_acceleration);
        response->set_tracking_acceleration(config.tracking_acceleration);
        
        // Park position
        response->set_park_position_axis1(config.park_position_axis1);
        response->set_park_position_axis2(config.park_position_axis2);
        
        // Encoder configuration
        response->set_use_encoders(config.use_encoders);
        response->set_encoders_absolute(config.encoders_absolute);
        response->set_encoder_resolution_config(config.encoder_resolution);
        
        // TPOINT configuration
        response->set_tpoint_enabled_terms(config.tpoint_enabled_terms);
        
        // Guider configuration
        response->set_enable_guider(config.enable_guider);
        response->set_guider_max_correction(config.guider_max_correction);
        response->set_guider_aggression(config.guider_aggression);
        
        // Atmospheric refraction correction
        response->set_enable_refraction_correction(config.enable_refraction_correction);
        
        // Mount type configuration
        response->set_mount_type(static_cast<astro_mount::MountType>(config.mount_type));
        
        // Mount orientation quaternion (for CASUAL mount type)
        {
            auto* orientation = response->mutable_mount_orientation();
            orientation->set_qx(config.mount_orientation.quaternion[0]);
            orientation->set_qy(config.mount_orientation.quaternion[1]);
            orientation->set_qz(config.mount_orientation.quaternion[2]);
            orientation->set_qw(config.mount_orientation.quaternion[3]);
        }
        
        // Position/rate tolerances for slew operations
        response->set_position_tolerance(config.position_tolerance);
        response->set_rate_tolerance(config.rate_tolerance);
        
        // Meridian flip configuration
        response->set_meridian_flip_enabled(config.meridian_flip_enabled);
        response->set_meridian_flip_delay_minutes(config.meridian_flip_delay_minutes);
        response->set_meridian_flip_hysteresis_degrees(config.meridian_flip_hysteresis_degrees);
        
        // Soft limits configuration
        response->set_soft_limits_enabled(config.soft_limits_enabled);
        response->set_soft_limit_axis1_min(config.soft_limit_axis1_min);
        response->set_soft_limit_axis1_max(config.soft_limit_axis1_max);
        response->set_soft_limit_axis2_min(config.soft_limit_axis2_min);
        response->set_soft_limit_axis2_max(config.soft_limit_axis2_max);
        response->set_soft_limit_warning_degrees(config.soft_limit_warning_degrees);
        response->set_soft_limit_deceleration_degrees(config.soft_limit_deceleration_degrees);
        response->set_soft_limit_tracking_rate_factor(config.soft_limit_tracking_rate_factor);
        
        // HA axis physical parameters
        auto* ha_params = response->mutable_ha_axis_params();
        ha_params->set_motor_steps_per_rev(config.ha_axis_params.motor_steps_per_rev);
        ha_params->set_motor_microstepping(config.ha_axis_params.motor_microstepping);
        ha_params->set_motor_step_angle(config.ha_axis_params.motor_step_angle);
        ha_params->set_encoder_resolution(config.ha_axis_params.encoder_resolution);
        ha_params->set_encoder_counts_per_arcsec(config.ha_axis_params.encoder_counts_per_arcsec);
        ha_params->set_encoder_quantization_error(config.ha_axis_params.encoder_quantization_error);
        ha_params->set_gear_ratio(config.ha_axis_params.gear_ratio);
        ha_params->set_worm_ratio(config.ha_axis_params.worm_ratio);
        ha_params->set_worm_teeth(config.ha_axis_params.worm_teeth);
        ha_params->set_worm_wheel_teeth(config.ha_axis_params.worm_wheel_teeth);
        ha_params->set_cyclic_error_amplitude(config.ha_axis_params.cyclic_error_amplitude);
        ha_params->set_cyclic_error_period(config.ha_axis_params.cyclic_error_period);
        
        // Copy cyclic harmonics
        for (const auto& harmonic : config.ha_axis_params.cyclic_harmonics) {
            ha_params->add_cyclic_harmonics(harmonic);
        }
        
        ha_params->set_backlash(config.ha_axis_params.backlash);
        ha_params->set_backlash_temp_coeff(config.ha_axis_params.backlash_temp_coeff);
        ha_params->set_axis_stiffness(config.ha_axis_params.axis_stiffness);
        ha_params->set_torsional_compliance(config.ha_axis_params.torsional_compliance);
        ha_params->set_expansion_coeff(config.ha_axis_params.expansion_coeff);
        ha_params->set_temp_gear_error_coeff(config.ha_axis_params.temp_gear_error_coeff);
        ha_params->set_calibration_temp(config.ha_axis_params.calibration_temp);
        
        // Copy calibration table
        for (const auto& value : config.ha_axis_params.calibration_table) {
            ha_params->add_calibration_table(value);
        }
        
        // Dec axis physical parameters
        auto* dec_params = response->mutable_dec_axis_params();
        dec_params->set_motor_steps_per_rev(config.dec_axis_params.motor_steps_per_rev);
        dec_params->set_motor_microstepping(config.dec_axis_params.motor_microstepping);
        dec_params->set_motor_step_angle(config.dec_axis_params.motor_step_angle);
        dec_params->set_encoder_resolution(config.dec_axis_params.encoder_resolution);
        dec_params->set_encoder_counts_per_arcsec(config.dec_axis_params.encoder_counts_per_arcsec);
        dec_params->set_encoder_quantization_error(config.dec_axis_params.encoder_quantization_error);
        dec_params->set_gear_ratio(config.dec_axis_params.gear_ratio);
        dec_params->set_worm_ratio(config.dec_axis_params.worm_ratio);
        dec_params->set_worm_teeth(config.dec_axis_params.worm_teeth);
        dec_params->set_worm_wheel_teeth(config.dec_axis_params.worm_wheel_teeth);
        dec_params->set_cyclic_error_amplitude(config.dec_axis_params.cyclic_error_amplitude);
        dec_params->set_cyclic_error_period(config.dec_axis_params.cyclic_error_period);
        
        // Copy cyclic harmonics for dec axis
        for (const auto& harmonic : config.dec_axis_params.cyclic_harmonics) {
            dec_params->add_cyclic_harmonics(harmonic);
        }
        
        dec_params->set_backlash(config.dec_axis_params.backlash);
        dec_params->set_backlash_temp_coeff(config.dec_axis_params.backlash_temp_coeff);
        dec_params->set_axis_stiffness(config.dec_axis_params.axis_stiffness);
        dec_params->set_torsional_compliance(config.dec_axis_params.torsional_compliance);
        dec_params->set_expansion_coeff(config.dec_axis_params.expansion_coeff);
        dec_params->set_temp_gear_error_coeff(config.dec_axis_params.temp_gear_error_coeff);
        dec_params->set_calibration_temp(config.dec_axis_params.calibration_temp);
        
        // Copy calibration table for dec axis
        for (const auto& value : config.dec_axis_params.calibration_table) {
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
        controllers::MountController::ControllerConfig config;
        
        // Basic configuration
        config.latitude = request->latitude();
        config.longitude = request->longitude();
        config.altitude = request->altitude();
        config.mount_height = request->mount_height();
        config.pier_west = request->pier_west();
        config.pier_east = request->pier_east();
        config.default_temperature = request->default_temperature();
        config.default_pressure = request->default_pressure();
        config.default_humidity = request->default_humidity();
        config.process_noise = request->process_noise();
        config.measurement_noise = request->measurement_noise();
        config.canopen_interface = request->canopen_interface();
        config.canopen_node_id = request->canopen_node_id();
        config.grpc_address = request->grpc_address();
        config.grpc_port = request->grpc_port();
        config.log_level = request->log_level();
        config.log_directory = request->log_directory();
        config.log_rotation_days = request->log_rotation_days();
        config.focal_length = request->focal_length();
        config.aperture = request->aperture();
        
        // Mount control parameters (gear ratios come from ha_axis_params/dec_axis_params)
        // If axis physical parameters are provided, they contain their own gear_ratio
        // For backward compatibility, also copy from top-level fields if available
        config.max_slew_rate = request->max_slew_rate();
        config.max_tracking_rate = request->max_tracking_rate();
        config.slew_acceleration = request->slew_acceleration();
        config.tracking_acceleration = request->tracking_acceleration();
        
        // Park position
        config.park_position_axis1 = request->park_position_axis1();
        config.park_position_axis2 = request->park_position_axis2();
        
        // Position/rate tolerances
        config.position_tolerance = request->position_tolerance();
        config.rate_tolerance = request->rate_tolerance();
        
        // Encoder configuration
        config.use_encoders = request->use_encoders();
        config.encoders_absolute = request->encoders_absolute();
        config.encoder_resolution = request->encoder_resolution_config();
        
        // TPOINT configuration
        config.tpoint_enabled_terms = request->tpoint_enabled_terms();
        
        // Guider configuration
        config.enable_guider = request->enable_guider();
        config.guider_max_correction = request->guider_max_correction();
        config.guider_aggression = request->guider_aggression();
        
        // Atmospheric refraction correction
        config.enable_refraction_correction = request->enable_refraction_correction();
        
        // Mount type
        config.mount_type = static_cast<controllers::MountController::MountType>(
            request->mount_type());
        
        // Mount orientation quaternion (for CASUAL mount type)
        if (request->has_mount_orientation()) {
            config.mount_orientation.quaternion[0] = request->mount_orientation().qx();
            config.mount_orientation.quaternion[1] = request->mount_orientation().qy();
            config.mount_orientation.quaternion[2] = request->mount_orientation().qz();
            config.mount_orientation.quaternion[3] = request->mount_orientation().qw();
        }
        
        // Meridian flip configuration
        config.meridian_flip_enabled = request->meridian_flip_enabled();
        config.meridian_flip_delay_minutes = request->meridian_flip_delay_minutes();
        config.meridian_flip_hysteresis_degrees = request->meridian_flip_hysteresis_degrees();
        
        // Soft limits configuration
        config.soft_limits_enabled = request->soft_limits_enabled();
        config.soft_limit_axis1_min = request->soft_limit_axis1_min();
        config.soft_limit_axis1_max = request->soft_limit_axis1_max();
        config.soft_limit_axis2_min = request->soft_limit_axis2_min();
        config.soft_limit_axis2_max = request->soft_limit_axis2_max();
        config.soft_limit_warning_degrees = request->soft_limit_warning_degrees();
        config.soft_limit_deceleration_degrees = request->soft_limit_deceleration_degrees();
        config.soft_limit_tracking_rate_factor = request->soft_limit_tracking_rate_factor();
        
        // HA axis physical parameters
        if (request->has_ha_axis_params()) {
            const auto& ha_params = request->ha_axis_params();
            config.ha_axis_params.motor_steps_per_rev = ha_params.motor_steps_per_rev();
            config.ha_axis_params.motor_microstepping = ha_params.motor_microstepping();
            config.ha_axis_params.motor_step_angle = ha_params.motor_step_angle();
            config.ha_axis_params.encoder_resolution = ha_params.encoder_resolution();
            config.ha_axis_params.encoder_counts_per_arcsec = ha_params.encoder_counts_per_arcsec();
            config.ha_axis_params.encoder_quantization_error = ha_params.encoder_quantization_error();
            config.ha_axis_params.gear_ratio = ha_params.gear_ratio();
            config.ha_axis_params.worm_ratio = ha_params.worm_ratio();
            config.ha_axis_params.worm_teeth = ha_params.worm_teeth();
            config.ha_axis_params.worm_wheel_teeth = ha_params.worm_wheel_teeth();
            config.ha_axis_params.cyclic_error_amplitude = ha_params.cyclic_error_amplitude();
            config.ha_axis_params.cyclic_error_period = ha_params.cyclic_error_period();
            
            // Copy cyclic harmonics
            for (int i = 0; i < ha_params.cyclic_harmonics_size() && i < 8; ++i) {
                config.ha_axis_params.cyclic_harmonics[i] = ha_params.cyclic_harmonics(i);
            }
            
            config.ha_axis_params.backlash = ha_params.backlash();
            config.ha_axis_params.backlash_temp_coeff = ha_params.backlash_temp_coeff();
            config.ha_axis_params.axis_stiffness = ha_params.axis_stiffness();
            config.ha_axis_params.torsional_compliance = ha_params.torsional_compliance();
            config.ha_axis_params.expansion_coeff = ha_params.expansion_coeff();
            config.ha_axis_params.temp_gear_error_coeff = ha_params.temp_gear_error_coeff();
            config.ha_axis_params.calibration_temp = ha_params.calibration_temp();
            
            // Copy calibration table
            config.ha_axis_params.calibration_table.clear();
            for (int i = 0; i < ha_params.calibration_table_size(); ++i) {
                config.ha_axis_params.calibration_table.push_back(ha_params.calibration_table(i));
            }
        }
        
        // Dec axis physical parameters
        if (request->has_dec_axis_params()) {
            const auto& dec_params = request->dec_axis_params();
            config.dec_axis_params.motor_steps_per_rev = dec_params.motor_steps_per_rev();
            config.dec_axis_params.motor_microstepping = dec_params.motor_microstepping();
            config.dec_axis_params.motor_step_angle = dec_params.motor_step_angle();
            config.dec_axis_params.encoder_resolution = dec_params.encoder_resolution();
            config.dec_axis_params.encoder_counts_per_arcsec = dec_params.encoder_counts_per_arcsec();
            config.dec_axis_params.encoder_quantization_error = dec_params.encoder_quantization_error();
            config.dec_axis_params.gear_ratio = dec_params.gear_ratio();
            config.dec_axis_params.worm_ratio = dec_params.worm_ratio();
            config.dec_axis_params.worm_teeth = dec_params.worm_teeth();
            config.dec_axis_params.worm_wheel_teeth = dec_params.worm_wheel_teeth();
            config.dec_axis_params.cyclic_error_amplitude = dec_params.cyclic_error_amplitude();
            config.dec_axis_params.cyclic_error_period = dec_params.cyclic_error_period();
            
            // Copy cyclic harmonics for dec axis
            for (int i = 0; i < dec_params.cyclic_harmonics_size() && i < 8; ++i) {
                config.dec_axis_params.cyclic_harmonics[i] = dec_params.cyclic_harmonics(i);
            }
            
            config.dec_axis_params.backlash = dec_params.backlash();
            config.dec_axis_params.backlash_temp_coeff = dec_params.backlash_temp_coeff();
            config.dec_axis_params.axis_stiffness = dec_params.axis_stiffness();
            config.dec_axis_params.torsional_compliance = dec_params.torsional_compliance();
            config.dec_axis_params.expansion_coeff = dec_params.expansion_coeff();
            config.dec_axis_params.temp_gear_error_coeff = dec_params.temp_gear_error_coeff();
            config.dec_axis_params.calibration_temp = dec_params.calibration_temp();
            
            // Copy calibration table for dec axis
            config.dec_axis_params.calibration_table.clear();
            for (int i = 0; i < dec_params.calibration_table_size(); ++i) {
                config.dec_axis_params.calibration_table.push_back(dec_params.calibration_table(i));
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

// Trajectory generation and execution
grpc::Status MountControllerServiceImpl::GenerateTrajectory(grpc::ServerContext* context,
                                                          const astro_mount::TrajectoryParams* request,
                                                          astro_mount::Trajectory* response) {
    try {
        // Convert proto params to ICanOpenInterface params
        controllers::ICanOpenInterface::TrajectoryParams params;
        
        // Map trajectory type
        switch (request->type()) {
            case astro_mount::TrajectoryType::TRAPEZOIDAL:
                params.type = controllers::ICanOpenInterface::TRAPEZOIDAL;
                break;
            case astro_mount::TrajectoryType::S_SHAPE:
                params.type = controllers::ICanOpenInterface::S_SHAPE;
                break;
            case astro_mount::TrajectoryType::SINE:
                params.type = controllers::ICanOpenInterface::SINE;
                break;
            case astro_mount::TrajectoryType::POLYNOMIAL:
                params.type = controllers::ICanOpenInterface::POLYNOMIAL;
                break;
            default:
                params.type = controllers::ICanOpenInterface::S_SHAPE;
        }
        
        params.max_velocity = request->max_velocity();
        params.max_acceleration = request->max_acceleration();
        params.max_jerk = request->max_jerk();
        params.start_position = request->start_position();
        params.target_position = request->target_position();
        params.update_rate = request->update_rate();
        
        // Generate trajectory using CanOpenInterface
        auto trajectory_points = controller_.getCanOpenInterface().generateTrajectory(params);
        
        // Fill response
        *response->mutable_params() = *request;
        *response->mutable_generated_at() = TimeUtil::GetCurrentTime();
        
        for (const auto& point : trajectory_points) {
            auto* proto_point = response->add_points();
            proto_point->set_position(point.position);
            proto_point->set_velocity(point.velocity);
            proto_point->set_acceleration(point.acceleration);
            proto_point->set_jerk(point.jerk);
            proto_point->set_time(point.time);
        }
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::ExecuteTrajectory(grpc::ServerContext* context,
                                                         const astro_mount::Trajectory* request,
                                                         google::protobuf::Empty* response) {
    try {
        // Convert proto trajectory to ICanOpenInterface trajectory
        std::vector<controllers::ICanOpenInterface::TrajectoryPoint> trajectory;
        
        for (const auto& proto_point : request->points()) {
            controllers::ICanOpenInterface::TrajectoryPoint point;
            point.position = proto_point.position();
            point.velocity = proto_point.velocity();
            point.acceleration = proto_point.acceleration();
            point.jerk = proto_point.jerk();
            point.time = proto_point.time();
            trajectory.push_back(point);
        }
        
        // Execute trajectory on axis 0 (RA/Azimuth) - in real implementation, 
        // we would determine which axis based on trajectory parameters
        bool success = controller_.getCanOpenInterface().executeTrajectory(0, trajectory);
        
        if (!success) {
            return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to execute trajectory");
        }
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::StopTrajectory(grpc::ServerContext* context,
                                                       const google::protobuf::Empty* request,
                                                       google::protobuf::Empty* response) {
    try {
        // Stop trajectory execution on all axes
        controller_.getCanOpenInterface().stopAxis(0); // RA/Azimuth
        controller_.getCanOpenInterface().stopAxis(1); // Dec/Altitude
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::INTERNAL, std::string("Error: ") + e.what());
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
    metrics.set_process_noise(config.process_noise);
    metrics.set_measurement_noise(config.measurement_noise);
    
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
        
        // Get CANopen interface
        auto& canopen = controller_.getCanOpenInterface();
        
        // Validate axis ID
        int axis_id = request->axis_id();
        if (axis_id < 0 || axis_id > 1) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                              "Invalid axis_id. Must be 0 (HA/RA/Azimuth) or 1 (Dec/Altitude)");
        }
        
        // Get configuration for default values
        auto config = controller_.getConfiguration();
        double default_velocity = config.max_slew_rate;
        double default_acceleration = config.slew_acceleration;
        
        if (request->mode() == astro_mount::AxisControlMode::POSITION_CONTROL) {
            // Position control mode
            double target_position = request->target_position();
            double max_velocity = request->max_velocity() > 0 ? request->max_velocity() : default_velocity;
            double acceleration = request->acceleration() > 0 ? request->acceleration() : default_acceleration;
            
            // If relative mode, get current position and add offset.
            // The REST proxy always sets relative=true explicitly for relative moves;
            // proto3 binary serialization preserves this correctly.
            double final_position = target_position;
            bool use_relative = request->relative();
            if (use_relative) {
                auto current_pos = canopen.getPositionData(axis_id);
                final_position = current_pos.actual_position + target_position;
                API_LOG_INFO("Relative mode: current={:.4f}° + offset={:.4f}° = target={:.4f}°",
                            current_pos.actual_position, target_position, final_position);
            }
            
            API_LOG_INFO("Setting position target: axis={}, position={}°, velocity={}°/s, acceleration={}°/s²",
                        axis_id, final_position, max_velocity, acceleration);
            
            if (!canopen.setPositionTarget(axis_id, final_position, max_velocity, acceleration)) {
                return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to set position target");
            }
            
        } else if (request->mode() == astro_mount::AxisControlMode::VELOCITY_CONTROL) {
            // Velocity control mode
            double target_velocity = request->target_velocity();
            double acceleration = request->acceleration() > 0 ? request->acceleration() : default_acceleration;
            
            // Clamp velocity to configured maximum
            if (std::abs(target_velocity) > default_velocity) {
                API_LOG_WARN("Velocity {:.2f}°/s exceeds max {:.2f}°/s, clamping",
                            target_velocity, default_velocity);
                target_velocity = std::copysign(default_velocity, target_velocity);
            }
            
            // If relative mode, get current velocity and add offset
            double final_velocity = target_velocity;
            if (request->relative()) {
                auto current_pos = canopen.getPositionData(axis_id);
                final_velocity = current_pos.actual_velocity + target_velocity;
            }
            
            API_LOG_INFO("Setting velocity target: axis={}, velocity={}°/s, acceleration={}°/s²",
                        axis_id, final_velocity, acceleration);
            
            if (!canopen.setVelocityTarget(axis_id, final_velocity, acceleration)) {
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
        
        // Get CANopen interface
        auto& canopen = controller_.getCanOpenInterface();
        
        // Validate axis ID
        int axis_id = request->axis_id();
        if (axis_id < 0 || axis_id > 1) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                              "Invalid axis_id. Must be 0 (HA/RA/Azimuth) or 1 (Dec/Altitude)");
        }
        
        if (request->decelerate()) {
            // Smooth stop with deceleration
            double deceleration = request->deceleration() > 0 ? request->deceleration() : 2.0; // default 2°/s²
            
            // Get current velocity
            auto current_pos = canopen.getPositionData(axis_id);
            double current_velocity = current_pos.actual_velocity;
            
            if (std::abs(current_velocity) > 0.001) {
                // Set velocity to 0 with deceleration
                if (!canopen.setVelocityTarget(axis_id, 0.0, deceleration)) {
                    return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to decelerate axis");
                }
                API_LOG_INFO("Axis {} decelerating from {}°/s with {}°/s²", 
                            axis_id, current_velocity, deceleration);
            } else {
                // Already stopped, just call stop
                canopen.stopAxis(axis_id);
            }
        } else {
            // Immediate stop
            canopen.stopAxis(axis_id);
            API_LOG_INFO("Axis {} stopped immediately", axis_id);
        }
        
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
        
        // Get CANopen interface
        auto& canopen = controller_.getCanOpenInterface();
        
        // Validate axis ID
        int axis_id = request->axis_id();
        if (axis_id < -1 || axis_id > 1) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                              "Invalid axis_id. Must be -1 (all axes), 0 (HA/RA/Azimuth), or 1 (Dec/Altitude)");
        }
        
        if (axis_id == -1) {
            // Emergency stop all axes
            canopen.emergencyStop(0);
            canopen.emergencyStop(1);
            API_LOG_WARN("Emergency stop on all axes");
        } else {
            // Emergency stop specific axis
            canopen.emergencyStop(axis_id);
            API_LOG_WARN("Emergency stop on axis {}", axis_id);
        }
        
        // Reset controller after emergency stop if requested
        if (request->reset_after()) {
            // Re-enable drives after emergency stop
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (axis_id == -1) {
                canopen.clearErrors(0);
                canopen.clearErrors(1);
            } else {
                canopen.clearErrors(axis_id);
            }
            API_LOG_INFO("Controller reset after emergency stop");
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
        
        // Get CANopen interface
        auto& canopen = controller_.getCanOpenInterface();
        
        // The AxisStatus proto is a single-axis message (no repeated field), so we
        // return axis 0 (HA/RA) status. For axis 1 (Dec) status, the proto could
        // be extended with an axis_id request field in the future.
        const int axis_id = 0;
        auto pos_data = canopen.getPositionData(axis_id);
        auto drive_status = canopen.getDriveStatus(axis_id);
        
        response->set_axis_id(axis_id);
        response->set_current_position(pos_data.actual_position);
        response->set_current_velocity(pos_data.actual_velocity);
        response->set_target_position(pos_data.target_position);
        response->set_target_velocity(0.0); // Not available in current API
        response->set_moving(drive_status.moving);
        response->set_target_reached(drive_status.target_reached);
        response->set_error(drive_status.error);
        response->set_error_message(drive_status.error_code > 0 ?
                                  std::to_string(drive_status.error_code) : "");
        
        // Set timestamp
        auto timestamp = google::protobuf::util::TimeUtil::MillisecondsToTimestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                pos_data.timestamp.time_since_epoch()).count());
        response->mutable_timestamp()->CopyFrom(timestamp);
        
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        API_LOG_ERROR("GetAxisStatus failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, 
                           std::string("Error: ") + e.what());
    }
}

// ============================================
// FIELD ROTATION / DEROTATOR CONTROL
// ============================================

grpc::Status MountControllerServiceImpl::ConfigureDerotator(
    grpc::ServerContext* context,
    const astro_mount::DerotatorConfig* request,
    google::protobuf::Empty* response) {
    
    try {
        API_LOG_INFO("ConfigureDerotator called");
        
        if (!controller_.configureDerotator(*request)) {
            return grpc::Status(grpc::StatusCode::INTERNAL, 
                               "Failed to configure derotator");
        }
        
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        API_LOG_ERROR("ConfigureDerotator failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, 
                           std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::EnableFieldRotation(
    grpc::ServerContext* context,
    const astro_mount::FieldRotationParams* request,
    google::protobuf::Empty* response) {
    
    try {
        API_LOG_INFO("EnableFieldRotation called");
        
        if (!controller_.enableFieldRotation(*request)) {
            return grpc::Status(grpc::StatusCode::INTERNAL, 
                               "Failed to enable field rotation");
        }
        
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        API_LOG_ERROR("EnableFieldRotation failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, 
                           std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::ControlFieldRotation(
    grpc::ServerContext* context,
    const astro_mount::FieldRotationControlRequest* request,
    google::protobuf::Empty* response) {
    
    try {
        API_LOG_INFO("ControlFieldRotation called");
        
        if (!controller_.controlFieldRotation(*request)) {
            return grpc::Status(grpc::StatusCode::INTERNAL, 
                               "Failed to control field rotation");
        }
        
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        API_LOG_ERROR("ControlFieldRotation failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, 
                           std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::GetDerotatorStatus(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    astro_mount::DerotatorStatus* response) {
    
    try {
        API_LOG_INFO("GetDerotatorStatus called");
        
        auto status = controller_.getDerotatorStatus();
        response->CopyFrom(status);
        
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        API_LOG_ERROR("GetDerotatorStatus failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, 
                           std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::HomeDerotator(
    grpc::ServerContext* context,
    const astro_mount::DerotatorHomingRequest* request,
    google::protobuf::Empty* response) {
    
    try {
        API_LOG_INFO("HomeDerotator called");
        
        if (!controller_.homeDerotator(*request)) {
            return grpc::Status(grpc::StatusCode::INTERNAL, 
                               "Failed to home derotator");
        }
        
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        API_LOG_ERROR("HomeDerotator failed: {}", e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, 
                           std::string("Error: ") + e.what());
    }
}

grpc::Status MountControllerServiceImpl::GetFieldRotationParams(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    astro_mount::FieldRotationParams* response) {
    
    try {
        API_LOG_INFO("GetFieldRotationParams called");
        
        auto params = controller_.getFieldRotationParams();
        response->CopyFrom(params);
        
        return grpc::Status::OK;
        
    } catch (const std::exception& e) {
        API_LOG_ERROR("GetFieldRotationParams failed: {}", e.what());
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

// ============================================
// Mount orientation (CASUAL mount type)
// ============================================

grpc::Status MountControllerServiceImpl::SetMountOrientation(
    grpc::ServerContext* context,
    const astro_mount::MountOrientation* request,
    google::protobuf::Empty* response) {
    try {
        controllers::MountController::MountOrientation orientation;
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

} // namespace api
} // namespace astro_mount
