/**
 * @file grpc_example.cpp
 * @brief Complete gRPC API usage example for Astronomical Mount Controller
 * 
 * This example demonstrates all gRPC API methods with comprehensive error handling
 * and realistic usage scenarios. It covers:
 * - Basic mount control (slew, track, stop, park)
 * - State management and configuration
 * - Bootstrap and TPOINT calibration
 * - Encoder and guider control
 * - Trajectory generation and execution
 * - Health checks and ephemeris tracking
 * - Low-level axis control for uncalibrated mounts
 * 
 * Compile with: g++ -std=c++17 -I. -I./proto -I./include grpc_example.cpp \
 *   -L./build/lib -lastro_mount_controller -lgrpc++ -lprotobuf -lpthread -o grpc_example
 */

#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <string>
#include <vector>
#include <cassert>

#include <grpcpp/grpcpp.h>
#include <google/protobuf/util/time_util.h>
#include "proto/mount_controller.grpc.pb.h"
#include "proto/mount_controller.pb.h"

using namespace astro_mount;
using google::protobuf::util::TimeUtil;
using google::protobuf::Empty;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

// Helper function to print status
void printStatus(const grpc::Status& status) {
    if (status.ok()) {
        std::cout << "✓ Success" << std::endl;
    } else {
        std::cout << "✗ Error " << status.error_code() << ": " 
                  << status.error_message() << std::endl;
    }
}

// Helper to print coordinates
void printCoordinates(const std::string& label, const Coordinates& coords) {
    std::cout << label << ": RA=" << coords.ra() << "h, Dec=" << coords.dec() << "°" << std::endl;
}


class MountControllerClient {
public:
    MountControllerClient(std::shared_ptr<Channel> channel)
        : stub_(MountControllerService::NewStub(channel)) {}
    
    // ============================================
    // 1. BASIC MOUNT CONTROL
    // ============================================
    
    bool slewToCoordinates(double ra_hours, double dec_degrees) {
        std::cout << "\n1.1 SlewToCoordinates:" << std::endl;
        std::cout << "   Target: RA=" << ra_hours << "h, Dec=" << dec_degrees << "°" << std::endl;
        
        Coordinates coords;
        coords.set_ra(ra_hours);
        coords.set_dec(dec_degrees);
        
        ClientContext context;
        Empty response;
        Status status = stub_->SlewToCoordinates(&context, coords, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    bool slewToHorizontal(double altitude_degrees, double azimuth_degrees) {
        std::cout << "\n1.2 SlewToHorizontal:" << std::endl;
        std::cout << "   Target: Alt=" << altitude_degrees << "°, Az=" << azimuth_degrees << "°" << std::endl;
        
        HorizontalCoordinates coords;
        coords.set_altitude(altitude_degrees);
        coords.set_azimuth(azimuth_degrees);
        *coords.mutable_timestamp() = TimeUtil::GetCurrentTime();
        
        ClientContext context;
        Empty response;
        Status status = stub_->SlewToHorizontal(&context, coords, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    bool trackObject(double ra_hours, double dec_degrees) {
        std::cout << "\n1.3 TrackObject:" << std::endl;
        printCoordinates("   Target", createCoordinates(ra_hours, dec_degrees));
        
        Coordinates coords = createCoordinates(ra_hours, dec_degrees);
        
        ClientContext context;
        Empty response;
        Status status = stub_->TrackObject(&context, coords, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    bool stop() {
        std::cout << "\n1.4 Stop:" << std::endl;
        
        ClientContext context;
        Empty request, response;
        Status status = stub_->Stop(&context, request, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    bool park() {
        std::cout << "\n1.5 Park:" << std::endl;
        
        ClientContext context;
        Empty request, response;
        Status status = stub_->Park(&context, request, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    bool unpark() {
        std::cout << "\n1.6 Unpark:" << std::endl;
        
        ClientContext context;
        Empty request, response;
        Status status = stub_->Unpark(&context, request, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    // ============================================
    // 2. STATE MANAGEMENT
    // ============================================
    
    ControllerState getState() {
        std::cout << "\n2.1 GetState:" << std::endl;
        
        ClientContext context;
        Empty request;
        ControllerState response;
        Status status = stub_->GetState(&context, request, &response);
        
        if (status.ok()) {
            std::cout << "   Status: " << ControllerState::MountStatus_Name(response.status()) << std::endl;
            std::cout << "   Encoders enabled: " << (response.encoders_enabled() ? "Yes" : "No") << std::endl;
            std::cout << "   Guider active: " << (response.guider_active() ? "Yes" : "No") << std::endl;
        } else {
            printStatus(status);
        }
        
        return response;
    }
    
    bool saveState(const std::string& file_path = "", bool include_measurements = true) {
        std::cout << "\n2.2 SaveState:" << std::endl;
        std::cout << "   File: " << (file_path.empty() ? "default" : file_path) << std::endl;
        
        StateSaveRequest request;
        if (!file_path.empty()) {
            request.set_file_path(file_path);
        }
        request.set_include_measurements(include_measurements);
        
        ClientContext context;
        StateSaveResponse response;
        Status status = stub_->SaveState(&context, request, &response);
        
        if (status.ok()) {
            std::cout << "   Saved to: " << response.file_path() << std::endl;
            std::cout << "   File size: " << response.file_size() << " bytes" << std::endl;
        } else {
            printStatus(status);
        }
        
        return status.ok();
    }
    
    bool loadState(const std::string& file_path = "") {
        std::cout << "\n2.3 LoadState:" << std::endl;
        std::cout << "   File: " << (file_path.empty() ? "default" : file_path) << std::endl;
        
        StateLoadRequest request;
        if (!file_path.empty()) {
            request.set_file_path(file_path);
        }
        
        ClientContext context;
        Empty response;
        Status status = stub_->LoadState(&context, request, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    // ============================================
    // 3. MEASUREMENT AND CALIBRATION
    // ============================================
    
    bool addMeasurement(const Measurement& measurement) {
        std::cout << "\n3.1 AddMeasurement:" << std::endl;
        printCoordinates("   Observed", measurement.observed());
        printCoordinates("   Expected", measurement.expected());
        
        ClientContext context;
        Empty response;
        Status status = stub_->AddMeasurement(&context, measurement, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    // Bootstrap calibration
    bool addBootstrapMeasurement(const BootstrapMeasurement& measurement) {
        std::cout << "\n3.2 AddBootstrapMeasurement:" << std::endl;
        printCoordinates("   Observed", measurement.observed());
        printCoordinates("   Expected", measurement.expected());
        
        ClientContext context;
        Empty response;
        Status status = stub_->AddBootstrapMeasurement(&context, measurement, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    BootstrapCalibrationResult runBootstrapCalibration() {
        std::cout << "\n3.3 RunBootstrapCalibration:" << std::endl;
        
        ClientContext context;
        Empty request;
        BootstrapCalibrationResult response;
        Status status = stub_->RunBootstrapCalibration(&context, request, &response);
        
        if (status.ok()) {
            std::cout << "   Success: " << (response.success() ? "Yes" : "No") << std::endl;
            if (!response.success()) {
                std::cout << "   Error: " << response.error_message() << std::endl;
            } else {
                std::cout << "   Measurements used: " << response.measurement_count() << std::endl;
                std::cout << "   Alignment error: " << response.alignment_error_arcsec() << "\"" << std::endl;
                std::cout << "   Ready for TPOINT: " << (response.ready_for_tpoint() ? "Yes" : "No") << std::endl;
            }
        } else {
            printStatus(status);
        }
        
        return response;
    }
    
    BootstrapStatus getBootstrapStatus() {
        std::cout << "\n3.4 GetBootstrapStatus:" << std::endl;
        
        ClientContext context;
        Empty request;
        BootstrapStatus response;
        Status status = stub_->GetBootstrapStatus(&context, request, &response);
        
        if (status.ok()) {
            std::cout << "   Calibrated: " << (response.calibrated() ? "Yes" : "No") << std::endl;
            std::cout << "   Measurement count: " << response.measurement_count() << std::endl;
            std::cout << "   Ready for TPOINT: " << (response.ready_for_tpoint() ? "Yes" : "No") << std::endl;
            std::cout << "   State: " << BootstrapStatus_CalibrationState_Name(response.state()) << std::endl;
        } else {
            printStatus(status);
        }
        
        return response;
    }
    
    bool clearBootstrapMeasurements() {
        std::cout << "\n3.5 ClearBootstrapMeasurements:" << std::endl;
        
        ClientContext context;
        Empty request, response;
        Status status = stub_->ClearBootstrapMeasurements(&context, request, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    // TPOINT calibration
    bool addTPointMeasurement(const Measurement& measurement) {
        std::cout << "\n3.6 AddTPointMeasurement:" << std::endl;
        printCoordinates("   Observed", measurement.observed());
        printCoordinates("   Expected", measurement.expected());
        
        ClientContext context;
        Empty response;
        Status status = stub_->AddTPointMeasurement(&context, measurement, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    bool clearTPointMeasurements() {
        std::cout << "\n3.7 ClearTPointMeasurements:" << std::endl;
        
        ClientContext context;
        Empty request, response;
        Status status = stub_->ClearTPointMeasurements(&context, request, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    TPointParameters getTPointParameters() {
        std::cout << "\n3.8 GetTPointParameters:" << std::endl;
        
        ClientContext context;
        Empty request;
        TPointParameters response;
        Status status = stub_->GetTPointParameters(&context, request, &response);
        
        if (status.ok()) {
            std::cout << "   Chi-squared: " << response.chi_squared() << std::endl;
            std::cout << "   Last update: " << TimeUtil::ToString(response.last_update()) << std::endl;
        } else {
            printStatus(status);
        }
        
        return response;
    }
    
    bool runTPointCalibration() {
        std::cout << "\n3.9 RunTPointCalibration:" << std::endl;
        
        ClientContext context;
        Empty request, response;
        Status status = stub_->RunTPointCalibration(&context, request, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    RotationMatrix getRotationMatrix() {
        std::cout << "\n3.10 GetRotationMatrix:" << std::endl;
        
        ClientContext context;
        Empty request;
        RotationMatrix response;
        Status status = stub_->GetRotationMatrix(&context, request, &response);
        
        if (status.ok()) {
            std::cout << "   Quaternion: q0=" << response.q0() 
                      << ", q1=" << response.q1()
                      << ", q2=" << response.q2()
                      << ", q3=" << response.q3() << std::endl;
        } else {
            printStatus(status);
        }
        
        return response;
    }
    
    // ============================================
    // 4. POLE POSITION AND ENCODERS
    // ============================================
    
    PolePosition determinePolePosition(int measurement_count = 10, double duration_hours = 1.0) {
        std::cout << "\n4.1 DeterminePolePosition:" << std::endl;
        std::cout << "   Measurements: " << measurement_count << std::endl;
        std::cout << "   Duration: " << duration_hours << " hours" << std::endl;
        
        PoleDeterminationRequest request;
        request.set_measurement_count(measurement_count);
        request.set_duration_hours(duration_hours);
        
        ClientContext context;
        PolePosition response;
        Status status = stub_->DeterminePolePosition(&context, request, &response);
        
        if (status.ok()) {
            std::cout << "   Latitude: " << response.latitude() << "°" << std::endl;
            std::cout << "   Longitude: " << response.longitude() << "°" << std::endl;
            std::cout << "   Accuracy: " << response.accuracy() << "\"" << std::endl;
        } else {
            printStatus(status);
        }
        
        return response;
    }
    
    bool enableEncoders(EncoderConfig::EncoderType type = EncoderConfig::ABSOLUTE, 
                        double resolution = 36000.0, bool use_feedback = true) {
        std::cout << "\n4.2 EnableEncoders:" << std::endl;
        std::cout << "   Type: " << EncoderConfig::EncoderType_Name(type) << std::endl;
        std::cout << "   Resolution: " << resolution << " counts/rev" << std::endl;
        
        EncoderConfig config;
        config.set_type(type);
        config.set_resolution(resolution);
        config.set_use_feedback(use_feedback);
        
        ClientContext context;
        Empty response;
        Status status = stub_->EnableEncoders(&context, config, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    bool disableEncoders() {
        std::cout << "\n4.3 DisableEncoders:" << std::endl;
        
        ClientContext context;
        Empty request, response;
        Status status = stub_->DisableEncoders(&context, request, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    // ============================================
    // 5. GUIDER CONTROL
    // ============================================
    
    bool connectGuider(const std::string& connection_string = "tcp://localhost:7624",
                       double max_correction = 10.0, double aggression = 0.5) {
        std::cout << "\n5.1 ConnectGuider:" << std::endl;
        std::cout << "   Connection: " << connection_string << std::endl;
        std::cout << "   Max correction: " << max_correction << "\"" << std::endl;
        
        GuiderConfig config;
        config.set_connection_string(connection_string);
        config.set_max_correction(max_correction);
        config.set_aggression(aggression);
        
        ClientContext context;
        Empty response;
        Status status = stub_->ConnectGuider(&context, config, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    bool disconnectGuider() {
        std::cout << "\n5.2 DisconnectGuider:" << std::endl;
        
        ClientContext context;
        Empty request, response;
        Status status = stub_->DisconnectGuider(&context, request, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    bool sendGuiderCorrection(double ra_correction_arcsec, double dec_correction_arcsec) {
        std::cout << "\n5.3 SendGuiderCorrection:" << std::endl;
        std::cout << "   RA correction: " << ra_correction_arcsec << "\"" << std::endl;
        std::cout << "   Dec correction: " << dec_correction_arcsec << "\"" << std::endl;
        
        GuiderCorrection correction;
        correction.set_ra_correction(ra_correction_arcsec);
        correction.set_dec_correction(dec_correction_arcsec);
        *correction.mutable_timestamp() = TimeUtil::GetCurrentTime();
        
        ClientContext context;
        Empty response;
        Status status = stub_->SendGuiderCorrection(&context, correction, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    // ============================================
    // 6. CONFIGURATION
    // ============================================
    
    Configuration getConfiguration() {
        std::cout << "\n6.1 GetConfiguration:" << std::endl;
        
        ClientContext context;
        Empty request;
        Configuration response;
        Status status = stub_->GetConfiguration(&context, request, &response);
        
        if (status.ok()) {
            std::cout << "   Latitude: " << response.latitude() << "°" << std::endl;
            std::cout << "   Longitude: " << response.longitude() << "°" << std::endl;
            std::cout << "   Max slew rate: " << response.max_slew_rate() << "°/s" << std::endl;
            std::cout << "   TPOINT enabled terms: " << response.tpoint_enabled_terms() << std::endl;
        } else {
            printStatus(status);
        }
        
        return response;
    }
    
    bool updateConfiguration(const Configuration& config) {
        std::cout << "\n6.2 UpdateConfiguration:" << std::endl;
        std::cout << "   Latitude: " << config.latitude() << "°" << std::endl;
        std::cout << "   Longitude: " << config.longitude() << "°" << std::endl;
        
        ClientContext context;
        Empty response;
        Status status = stub_->UpdateConfiguration(&context, config, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    // ============================================
    // 7. TRAJECTORY GENERATION AND EXECUTION
    // ============================================
    
    Trajectory generateTrajectory(TrajectoryType type = TrajectoryType::S_SHAPE,
                                  double max_velocity = 5.0, double max_acceleration = 2.0,
                                  double start_position = 0.0, double target_position = 90.0) {
        std::cout << "\n7.1 GenerateTrajectory:" << std::endl;
        std::cout << "   Type: " << TrajectoryType_Name(type) << std::endl;
        std::cout << "   Start: " << start_position << "°, Target: " << target_position << "°" << std::endl;
        std::cout << "   Max velocity: " << max_velocity << "°/s" << std::endl;
        
        TrajectoryParams params;
        params.set_type(type);
        params.set_max_velocity(max_velocity);
        params.set_max_acceleration(max_acceleration);
        params.set_start_position(start_position);
        params.set_target_position(target_position);
        params.set_update_rate(100.0);  // 100 Hz
        
        ClientContext context;
        Trajectory response;
        Status status = stub_->GenerateTrajectory(&context, params, &response);
        
        if (status.ok()) {
            std::cout << "   Generated " << response.points_size() << " trajectory points" << std::endl;
            if (response.points_size() > 0) {
                std::cout << "   First point: pos=" << response.points(0).position() 
                          << "°, vel=" << response.points(0).velocity() << "°/s" << std::endl;
                std::cout << "   Last point: pos=" << response.points(response.points_size()-1).position() 
                          << "°, vel=" << response.points(response.points_size()-1).velocity() << "°/s" << std::endl;
            }
        } else {
            printStatus(status);
        }
        
        return response;
    }
    
    bool executeTrajectory(const Trajectory& trajectory) {
        std::cout << "\n7.2 ExecuteTrajectory:" << std::endl;
        std::cout << "   Points: " << trajectory.points_size() << std::endl;
        
        ClientContext context;
        Empty response;
        Status status = stub_->ExecuteTrajectory(&context, trajectory, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    bool stopTrajectory() {
        std::cout << "\n7.3 StopTrajectory:" << std::endl;
        
        ClientContext context;
        Empty request, response;
        Status status = stub_->StopTrajectory(&context, request, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    // ============================================
    // 8. HEALTH CHECK AND METRICS
    // ============================================
    
    HealthCheckResponse checkHealth(const std::string& service = "mount_controller") {
        std::cout << "\n8.1 CheckHealth:" << std::endl;
        std::cout << "   Service: " << service << std::endl;
        
        HealthCheckRequest request;
        request.set_service(service);
        
        ClientContext context;
        HealthCheckResponse response;
        Status status = stub_->CheckHealth(&context, request, &response);
        
        if (status.ok()) {
            std::cout << "   Status: " << HealthCheckResponse::ServingStatus_Name(response.status()) << std::endl;
            if (response.has_metrics()) {
                std::cout << "   CPU usage: " << response.metrics().cpu_usage_percent() << "%" << std::endl;
                std::cout << "   Memory: " << response.metrics().memory_usage_mb() << " MB" << std::endl;
                std::cout << "   Active connections: " << response.metrics().active_connections() << std::endl;
            }
        } else {
            printStatus(status);
        }
        
        return response;
    }
    
    // ============================================
    // 9. EPHEMERIS-BASED TRACKING
    // ============================================
    
    bool uploadEphemeris(const EphemerisData& ephemeris) {
        std::cout << "\n9.1 UploadEphemeris:" << std::endl;
        std::cout << "   Object: " << ephemeris.object_name() << " (" << ephemeris.object_id() << ")" << std::endl;
        std::cout << "   Points: " << ephemeris.points_size() << std::endl;
        
        ClientContext context;
        Empty response;
        Status status = stub_->UploadEphemeris(&context, ephemeris, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    EphemerisTrackStatus startEphemerisTracking(const std::string& object_id, 
                                                double slew_margin_seconds = 30.0) {
        std::cout << "\n9.2 StartEphemerisTracking:" << std::endl;
        std::cout << "   Object ID: " << object_id << std::endl;
        std::cout << "   Slew margin: " << slew_margin_seconds << " seconds" << std::endl;
        
        StartEphemerisTrackingRequest request;
        request.set_object_id(object_id);
        request.set_slew_margin_seconds(slew_margin_seconds);
        *request.mutable_start_time() = TimeUtil::GetCurrentTime();
        
        ClientContext context;
        EphemerisTrackStatus response;
        Status status = stub_->StartEphemerisTracking(&context, request, &response);
        
        if (status.ok()) {
            std::cout << "   State: " << EphemerisTrackStatus::TrackingState_Name(response.state()) << std::endl;
            std::cout << "   Object: " << response.object_name() << std::endl;
            std::cout << "   Position error: " << response.position_error_arcsec() << "\"" << std::endl;
        } else {
            printStatus(status);
        }
        
        return response;
    }
    
    EphemerisTrackStatus startEphemerisTrackingWithData(const EphemerisTrackRequest& request) {
        std::cout << "\n9.3 StartEphemerisTrackingWithData:" << std::endl;
        std::cout << "   Object: " << request.ephemeris().object_name() << std::endl;
        
        ClientContext context;
        EphemerisTrackStatus response;
        Status status = stub_->StartEphemerisTrackingWithData(&context, request, &response);
        
        if (status.ok()) {
            std::cout << "   State: " << EphemerisTrackStatus::TrackingState_Name(response.state()) << std::endl;
        } else {
            printStatus(status);
        }
        
        return response;
    }
    
    EphemerisTrackStatus getEphemerisTrackStatus() {
        std::cout << "\n9.4 GetEphemerisTrackStatus:" << std::endl;
        
        ClientContext context;
        Empty request;
        EphemerisTrackStatus response;
        Status status = stub_->GetEphemerisTrackStatus(&context, request, &response);
        
        if (status.ok()) {
            std::cout << "   State: " << EphemerisTrackStatus::TrackingState_Name(response.state()) << std::endl;
            if (response.state() != EphemerisTrackStatus::IDLE) {
                std::cout << "   Object: " << response.object_name() << std::endl;
                std::cout << "   Time remaining: " << response.time_remaining_seconds() << " seconds" << std::endl;
            }
        } else {
            printStatus(status);
        }
        
        return response;
    }
    
    bool stopEphemerisTracking() {
        std::cout << "\n9.5 StopEphemerisTracking:" << std::endl;
        
        ClientContext context;
        Empty request, response;
        Status status = stub_->StopEphemerisTracking(&context, request, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    EphemerisMetrics getEphemerisMetrics() {
        std::cout << "\n9.6 GetEphemerisMetrics:" << std::endl;
        
        ClientContext context;
        Empty request;
        EphemerisMetrics response;
        Status status = stub_->GetEphemerisMetrics(&context, request, &response);
        
        if (status.ok()) {
            std::cout << "   Object: " << response.object_id() << " (" << response.object_type() << ")" << std::endl;
            std::cout << "   Total track time: " << response.total_track_time_seconds() << " seconds" << std::endl;
            std::cout << "   Avg position error: " << response.avg_position_error_arcsec() << "\"" << std::endl;
        } else {
            printStatus(status);
        }
        
        return response;
    }
    
    bool clearEphemerisCache() {
        std::cout << "\n9.7 ClearEphemerisCache:" << std::endl;
        
        ClientContext context;
        Empty request, response;
        Status status = stub_->ClearEphemerisCache(&context, request, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    // ============================================
    // 10. LOW-LEVEL AXIS CONTROL API
    // ============================================
    
    bool controlAxis(int axis_id, int mode,  // mode: 0=POSITION_CONTROL, 1=VELOCITY_CONTROL
                     double target_value, bool relative = false) {
        std::cout << "\n10.1 ControlAxis:" << std::endl;
        std::cout << "   Axis: " << axis_id << " (0=HA/RA, 1=Dec/Alt)" << std::endl;
        std::cout << "   Mode: " << (mode == 0 ? "POSITION_CONTROL" : "VELOCITY_CONTROL") << std::endl;
        std::cout << "   Target: " << target_value << (mode == 0 ? "°" : "°/s") << std::endl;
        std::cout << "   Relative: " << (relative ? "Yes" : "No") << std::endl;
        
        AxisControlRequest request;
        request.set_axis_id(axis_id);
        request.set_mode(static_cast<int32_t>(mode));  // Use int32_t for enum value
        request.set_relative(relative);
        
        if (mode == 0) {  // POSITION_CONTROL
            request.set_target_position(target_value);
            request.set_max_velocity(2.0);  // Default values
            request.set_acceleration(1.0);
        } else {  // VELOCITY_CONTROL
            request.set_target_velocity(target_value);
            request.set_acceleration(0.5);
        }
        
        ClientContext context;
        Empty response;
        Status status = stub_->ControlAxis(&context, request, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    bool stopAxis(int axis_id, bool decelerate = true, double deceleration = 2.0) {
        std::cout << "\n10.2 StopAxis:" << std::endl;
        std::cout << "   Axis: " << axis_id << std::endl;
        std::cout << "   Decelerate: " << (decelerate ? "Yes" : "No") << std::endl;
        if (decelerate) {
            std::cout << "   Deceleration: " << deceleration << "°/s²" << std::endl;
        }
        
        AxisStopRequest request;
        request.set_axis_id(axis_id);
        request.set_decelerate(decelerate);
        if (decelerate) {
            request.set_deceleration(deceleration);
        }
        
        ClientContext context;
        Empty response;
        Status status = stub_->StopAxis(&context, request, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    bool emergencyStop(int axis_id = -1, bool reset_after = true) {
        std::cout << "\n10.3 EmergencyStop:" << std::endl;
        if (axis_id == -1) {
            std::cout << "   All axes" << std::endl;
        } else {
            std::cout << "   Axis: " << axis_id << std::endl;
        }
        std::cout << "   Reset after: " << (reset_after ? "Yes" : "No") << std::endl;
        
        EmergencyStopRequest request;
        request.set_axis_id(axis_id);
        request.set_reset_after(reset_after);
        
        ClientContext context;
        Empty response;
        Status status = stub_->EmergencyStop(&context, request, &response);
        
        printStatus(status);
        return status.ok();
    }
    
    AxisStatus getAxisStatus() {
        std::cout << "\n10.4 GetAxisStatus:" << std::endl;
        
        ClientContext context;
        Empty request;
        AxisStatus response;
        Status status = stub_->GetAxisStatus(&context, request, &response);
        
        if (status.ok()) {
            std::cout << "   Axis " << response.axis_id() << ":" << std::endl;
            std::cout << "     Position: " << response.current_position() << "°" << std::endl;
            std::cout << "     Velocity: " << response.current_velocity() << "°/s" << std::endl;
            std::cout << "     Moving: " << (response.moving() ? "Yes" : "No") << std::endl;
            std::cout << "     Target reached: " << (response.target_reached() ? "Yes" : "No") << std::endl;
            if (response.error()) {
                std::cout << "     Error: " << response.error_message() << std::endl;
            }
        } else {
            printStatus(status);
        }
        
        return response;
    }
    
private:
    std::unique_ptr<MountControllerService::Stub> stub_;
};

// Helper to create coordinates - public version
Coordinates createCoordinates(double ra_hours, double dec_degrees) {
    Coordinates coords;
    coords.set_ra(ra_hours);
    coords.set_dec(dec_degrees);
    return coords;
}

// Helper to create coordinates - public static version
Coordinates CreateCoordinates(double ra_hours, double dec_degrees) {
    Coordinates coords;
    coords.set_ra(ra_hours);
    coords.set_dec(dec_degrees);
    return coords;
}

// Helper functions to create sample data
Measurement createSampleMeasurement(double obs_ra, double obs_dec, 
                                   double exp_ra, double exp_dec,
                                   double mount_ha, double mount_dec) {
    Measurement measurement;
    
    Coordinates* observed = measurement.mutable_observed();
    observed->set_ra(obs_ra);
    observed->set_dec(obs_dec);
    
    Coordinates* expected = measurement.mutable_expected();
    expected->set_ra(exp_ra);
    expected->set_dec(exp_dec);
    
    MountPosition* mount_pos = measurement.mutable_mount_position();
    mount_pos->set_axis1(mount_ha * 15.0);  // Convert HA to degrees
    mount_pos->set_axis2(mount_dec);
    *mount_pos->mutable_timestamp() = TimeUtil::GetCurrentTime();
    
    measurement.set_temperature(15.0);
    measurement.set_pressure(1013.25);
    measurement.set_humidity(0.5);
    *measurement.mutable_timestamp() = TimeUtil::GetCurrentTime();
    
    return measurement;
}

EphemerisData createSampleEphemeris() {
    EphemerisData ephemeris;
    ephemeris.set_object_id("test_comet");
    ephemeris.set_object_name("Test Comet C/2024 X1");
    ephemeris.set_object_type("comet");
    ephemeris.set_interpolation_order(2.0);
    ephemeris.set_reference_frame("J2000");
    ephemeris.set_source("test_data");
    
    // Add a few sample points
    auto now = TimeUtil::GetCurrentTime();
    
    for (int i = 0; i < 5; ++i) {
        EphemerisPoint* point = ephemeris.add_points();
        point->set_ra(10.0 + i * 0.1);  // RA changes with time
        point->set_dec(45.0 + i * 0.05); // Dec changes with time
        point->set_ra_rate(0.1);  // 0.1 hours/hour
        point->set_dec_rate(0.05); // 0.05 degrees/hour
        point->set_distance(1.0 + i * 0.1);  // AU
        point->set_magnitude(8.0 + i * 0.1);
        
        auto timestamp = TimeUtil::GetCurrentTime();
        timestamp.set_seconds(timestamp.seconds() + i * 3600);  // 1 hour intervals
        *point->mutable_timestamp() = timestamp;
    }
    
    *ephemeris.mutable_valid_from() = now;
    auto valid_to = now;
    valid_to.set_seconds(valid_to.seconds() + 24 * 3600);  // Valid for 24 hours
    *ephemeris.mutable_valid_to() = valid_to;
    
    return ephemeris;
}

int main(int argc, char** argv) {
    std::cout << "==========================================" << std::endl;
    std::cout << "Astronomical Mount Controller - gRPC Example" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "This example demonstrates ALL gRPC API methods." << std::endl;
    std::cout << "Server must be running on localhost:50051" << std::endl;
    std::cout << "==========================================" << std::endl;
    
    try {
        // Create gRPC channel
        std::string server_address = "localhost:50051";
        if (argc > 1) {
            server_address = argv[1];
        }
        
        std::cout << "\nConnecting to server at: " << server_address << std::endl;
        
        auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
        MountControllerClient client(channel);
        
        // Test connection with health check
        std::cout << "\n=== Testing connection ===" << std::endl;
        auto health_response = client.checkHealth("mount_controller");
        
        if (health_response.status() != HealthCheckResponse::SERVING) {
            std::cerr << "Server is not serving. Make sure astro_mount_controller is running." << std::endl;
            return 1;
        }
        
        std::cout << "\n=== Starting comprehensive API demonstration ===" << std::endl;
        
        // 1. BASIC MOUNT CONTROL
        std::cout << "\n=== 1. BASIC MOUNT CONTROL ===" << std::endl;
        client.unpark();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        client.slewToCoordinates(10.0, 45.0);  // RA=10h, Dec=45°
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        client.trackObject(12.0, 30.0);  // Start tracking
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        client.slewToHorizontal(45.0, 180.0);  // Alt=45°, Az=180° (South)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 2. STATE MANAGEMENT
        std::cout << "\n=== 2. STATE MANAGEMENT ===" << std::endl;
        auto state = client.getState();
        client.saveState("test_state.json", true);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 3. CALIBRATION
        std::cout << "\n=== 3. CALIBRATION ===" << std::endl;
        
        // Bootstrap calibration
        BootstrapMeasurement bootstrap_meas;
        *bootstrap_meas.mutable_observed() = createCoordinates(18.6156, 38.7836);  // Vega
        *bootstrap_meas.mutable_expected() = createCoordinates(18.6156, 38.7836);
        bootstrap_meas.set_estimated_error_arcsec(45.0);
        bootstrap_meas.set_use_for_initial_alignment(true);
        *bootstrap_meas.mutable_timestamp() = TimeUtil::GetCurrentTime();
        
        client.addBootstrapMeasurement(bootstrap_meas);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        auto calibration_result = client.runBootstrapCalibration();
        auto bootstrap_status = client.getBootstrapStatus();
        
        // TPOINT measurement - create with 6 arguments
        Measurement tpoint_meas = createSampleMeasurement(
            10.5, 45.0,    // observed RA, Dec
            10.5, 45.0,    // expected RA, Dec  
            -1.5, 45.0     // mount HA, Dec
        );
        
        client.addTPointMeasurement(tpoint_meas);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        auto tpoint_params = client.getTPointParameters();
        auto rotation_matrix = client.getRotationMatrix();
        
        // 4. POLE POSITION AND ENCODERS
        std::cout << "\n=== 4. POLE POSITION AND ENCODERS ===" << std::endl;
        auto pole_position = client.determinePolePosition(5, 0.5);
        client.enableEncoders(EncoderConfig::ABSOLUTE, 36000.0, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 5. GUIDER CONTROL
        std::cout << "\n=== 5. GUIDER CONTROL ===" << std::endl;
        client.connectGuider("tcp://localhost:7624", 10.0, 0.5);
        client.sendGuiderCorrection(2.5, -1.5);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 6. CONFIGURATION
        std::cout << "\n=== 6. CONFIGURATION ===" << std::endl;
        auto config = client.getConfiguration();
        
        // Modify configuration
        config.set_latitude(52.2297);  // Warsaw
        config.set_longitude(21.0122);
        config.set_altitude(100.0);
        config.set_max_slew_rate(5.0);
        config.set_tpoint_enabled_terms(511);  // Enable all basic terms
        
        client.updateConfiguration(config);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 7. TRAJECTORY
        std::cout << "\n=== 7. TRAJECTORY ===" << std::endl;
        auto trajectory = client.generateTrajectory(
            TrajectoryType::S_SHAPE, 5.0, 2.0, 0.0, 90.0
        );
        
        if (trajectory.points_size() > 0) {
            // In real usage, you would execute the trajectory
            // client.executeTrajectory(trajectory);
            std::cout << "   (Trajectory execution commented out for safety)" << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 8. HEALTH CHECK
        std::cout << "\n=== 8. HEALTH CHECK ===" << std::endl;
        client.checkHealth();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 9. EPHEMERIS TRACKING
        std::cout << "\n=== 9. EPHEMERIS TRACKING ===" << std::endl;
        auto ephemeris = createSampleEphemeris();
        client.uploadEphemeris(ephemeris);
        
        auto track_status = client.getEphemerisTrackStatus();
        auto ephemeris_metrics = client.getEphemerisMetrics();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 10. LOW-LEVEL AXIS CONTROL
        std::cout << "\n=== 10. LOW-LEVEL AXIS CONTROL ===" << std::endl;
        client.controlAxis(0, 0, 45.0, false);  // 0 = POSITION_CONTROL
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        client.controlAxis(1, 1, 0.5, false);   // 1 = VELOCITY_CONTROL
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        auto axis_status = client.getAxisStatus();
        client.stopAxis(0, true, 2.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Emergency stop demonstration (commented out for safety)
        std::cout << "   (Emergency stop demonstration commented out for safety)" << std::endl;
        // client.emergencyStop(0, true);
        
        // Cleanup
        std::cout << "\n=== CLEANUP ===" << std::endl;
        client.stop();
        client.disconnectGuider();
        client.disableEncoders();
        client.park();
        client.clearEphemerisCache();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        std::cout << "\n==========================================" << std::endl;
        std::cout << "gRPC API demonstration completed successfully!" << std::endl;
        std::cout << "All API methods were demonstrated." << std::endl;
        std::cout << "==========================================" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}