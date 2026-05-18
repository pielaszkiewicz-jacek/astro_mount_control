#include <iostream>
#include <memory>
#include <chrono>
#include <thread>

#include "controllers/mount_controller.h"

using namespace astro_mount::controllers;

int main() {
    std::cout << "Astronomical Mount Controller - C++ Example" << std::endl;
    std::cout << "==========================================" << std::endl;
    
    try {
        // Create mount controller
        auto controller = std::make_unique<MountController>();
        
        // Configure the controller
        MountController::ControllerConfig config;
        
        // Set location (Warsaw, Poland)
        config.mount_type = MountController::MountType::EQUATORIAL;
        config.latitude = 52.2297;
        config.longitude = 21.0122;
        config.altitude = 100.0;
        
        // Set mount parameters
        config.axis1_gear_ratio = 360.0;  // 360:1 gear ratio
        config.axis2_gear_ratio = 360.0;
        config.max_slew_rate = 5.0;       // 5 degrees/sec
        config.max_tracking_rate = 0.25;  // Sidereal rate
        
        // Set control parameters
        config.slew_acceleration = 1.0;
        config.tracking_acceleration = 0.1;
        config.position_tolerance = 0.01;  // 0.01 degrees
        config.rate_tolerance = 0.001;     // 0.001 degrees/sec
        
        // Set encoder configuration
        config.use_encoders = true;
        config.encoders_absolute = true;
        config.encoder_resolution = 36000;  // 0.01 degree resolution
        
        // Set filter parameters
        config.process_noise = 0.001;
        config.measurement_noise = 0.01;
        
        // Set TPOINT parameters
        config.tpoint_enabled_terms = 511;  // Enable all basic terms
        
        // Set CanOpen configuration
        config.canopen_interface = "can0";
        config.canopen_node_id = 1;
        
        // Set guider configuration
        config.enable_guider = true;
        config.guider_max_correction = 10.0;
        config.guider_aggression = 0.5;
        
        // Initialize controller
        std::cout << "Initializing mount controller..." << std::endl;
        if (!controller->initialize(config)) {
            std::cerr << "Failed to initialize mount controller" << std::endl;
            return 1;
        }
        std::cout << "Mount controller initialized successfully" << std::endl;
        
        // Get initial status
        auto status = controller->getStatus();
        std::cout << "Initial status: " << static_cast<int>(status.state) << std::endl;
        std::cout << "Axis positions: " << status.axis1_position << "°, " 
                  << status.axis2_position << "°" << std::endl;
        
        // Set environmental parameters
        controller->setEnvironmentalParams(15.0, 1013.25, 0.5);
        std::cout << "Environmental parameters set" << std::endl;
        
        // Example 1: Slew to a target
        std::cout << "\nExample 1: Slew to target (RA=10h, Dec=45°)" << std::endl;
        if (controller->slewToEquatorial(10.0, 45.0)) {
            std::cout << "Slew command accepted" << std::endl;
            
            // Wait for slew to complete
            for (int i = 0; i < 30; ++i) {
                status = controller->getStatus();
                if (status.state == MountController::MountStatus::State::IDLE ||
                    status.state == MountController::MountStatus::State::TRACKING) {
                    std::cout << "Slew completed" << std::endl;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        
        // Example 2: Start tracking
        std::cout << "\nExample 2: Start tracking (RA=12h, Dec=30°)" << std::endl;
        if (controller->startTracking(12.0, 30.0)) {
            std::cout << "Tracking started" << std::endl;
            
            // Track for 5 seconds
            for (int i = 0; i < 5; ++i) {
                status = controller->getStatus();
                std::cout << "Tracking... Position: " << status.axis1_position 
                          << "°, " << status.axis2_position << "°" << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        
        // Example 3: Add calibration measurements
        std::cout << "\nExample 3: Add calibration measurements" << std::endl;
        for (int i = 0; i < 5; ++i) {
            double ra = 10.0 + i * 0.5;
            double dec = 40.0 + i * 1.0;
            
            // Simulate observed position with small error
            double observed_ra = ra + (rand() % 100 - 50) / 36000.0;  // ±0.005 hours
            double observed_dec = dec + (rand() % 100 - 50) / 3600.0; // ±0.05 degrees
            
            if (controller->addCalibrationMeasurement(observed_ra, observed_dec, ra, dec)) {
                std::cout << "Added calibration measurement " << (i + 1) << std::endl;
            }
        }
        
        // Example 4: Run TPOINT calibration
        std::cout << "\nExample 4: Run TPOINT calibration" << std::endl;
        if (controller->runTPointCalibration()) {
            std::cout << "TPOINT calibration successful" << std::endl;
            
            // Get TPOINT parameters
            auto tpoint_params = controller->getTPointParameters();
            std::cout << "TPOINT parameters: " << tpoint_params << std::endl;
        }
        
        // Example 5: Get rotation matrix
        std::cout << "\nExample 5: Get rotation matrix" << std::endl;
        auto rotation_matrix = controller->getRotationMatrix();
        std::cout << "Rotation quaternion: ";
        for (double q : rotation_matrix) {
            std::cout << q << " ";
        }
        std::cout << std::endl;
        
        // Example 6: Determine pole position
        std::cout << "\nExample 6: Determine pole position (simulated)" << std::endl;
        auto pole_position = controller->determinePolePosition(0.1);  // 0.1 hours for demo
        std::cout << "Pole position: Lat=" << std::get<0>(pole_position) 
                  << "°, Lon=" << std::get<1>(pole_position) 
                  << "°, Accuracy=" << std::get<2>(pole_position) << " arcsec" << std::endl;
        
        // Example 7: Save and load state
        std::cout << "\nExample 7: Save and load controller state" << std::endl;
        if (controller->saveState("mount_state.json")) {
            std::cout << "State saved to mount_state.json" << std::endl;
        }
        
        // Example 8: Apply guider correction
        std::cout << "\nExample 8: Apply guider correction" << std::endl;
        controller->applyGuiderCorrection(2.5, -1.5);  // 2.5" RA, -1.5" Dec
        std::cout << "Guider correction applied" << std::endl;
        
        // Example 9: Stop tracking
        std::cout << "\nExample 9: Stop tracking" << std::endl;
        controller->stop();
        std::cout << "Tracking stopped" << std::endl;
        
        // Example 10: Park mount
        std::cout << "\nExample 10: Park mount" << std::endl;
        controller->park();
        
        // Wait for park to complete
        for (int i = 0; i < 10; ++i) {
            status = controller->getStatus();
            if (status.state == MountController::MountStatus::State::PARKED) {
                std::cout << "Mount parked" << std::endl;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Shutdown
        std::cout << "\nShutting down..." << std::endl;
        controller->shutdown();
        std::cout << "Shutdown complete" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\nExample completed successfully!" << std::endl;
    return 0;
}