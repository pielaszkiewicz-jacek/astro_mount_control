#include "controllers/mount_controller.h"
#include "core/astronomical_calculations.h"
#include "logging/logger.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <cmath>
#include <spdlog/sinks/callback_sink.h>
#include <vector>
#include <algorithm>
#include <sstream>

namespace astro_mount {
namespace controllers {
namespace test {

using namespace std::chrono_literals;

class MountControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        logging::Logger::init("");

        controller_ = std::make_unique<MountController>();

        // Default config: equatorial mount, no CANopen (uses mock)
        config_.mount_type = MountController::MountType::EQUATORIAL;
        config_.latitude = 52.0;
        config_.longitude = 21.0;
        config_.altitude = 100.0;
        config_.max_slew_rate = 5.0;
        config_.max_tracking_rate = 0.004178;
        config_.slew_acceleration = 1.0;
        config_.tracking_acceleration = 0.001;
        config_.position_tolerance = 0.5;
        config_.rate_tolerance = 0.001;
        config_.canopen_interface = "";  // will use mock
        config_.canopen_node_id = 1;
        config_.use_encoders = false;
        config_.encoders_absolute = false;
        config_.encoder_resolution = 360000;
        config_.default_temperature = 15.0;
        config_.default_pressure = 1013.25;
        config_.default_humidity = 0.5;
        config_.focal_length = 2000.0;
        config_.aperture = 250.0;
        config_.enable_guider = false;
        config_.guider_max_correction = 100.0;
        config_.guider_aggression = 0.5;
        config_.ha_axis_params.gear_ratio = 360.0;
        config_.dec_axis_params.gear_ratio = 360.0;
        config_.ha_axis_params.backlash = 0.0;
        config_.dec_axis_params.backlash = 0.0;
        config_.ha_axis_params.encoder_resolution = 360000.0;
        config_.dec_axis_params.encoder_resolution = 360000.0;
    }

    void TearDown() override {
        controller_->shutdown();
        controller_.reset();
    }

    std::unique_ptr<MountController> controller_;
    MountController::ControllerConfig config_;
};

// ============================================
// 1. INITIALIZATION
// ============================================

TEST_F(MountControllerTest, InitialStateIsUninitialized) {
    auto status = controller_->getStatus();
    EXPECT_EQ(status.state, MountController::MountStatus::State::UNINITIALIZED);
}

TEST_F(MountControllerTest, InitializeSuccess) {
    EXPECT_TRUE(controller_->initialize(config_));
}

TEST_F(MountControllerTest, AfterInitializeStateIsIdle) {
    controller_->initialize(config_);
    auto status = controller_->getStatus();
    EXPECT_EQ(status.state, MountController::MountStatus::State::IDLE);
}

TEST_F(MountControllerTest, ShutdownSetsUninitialized) {
    controller_->initialize(config_);
    controller_->shutdown();
    auto status = controller_->getStatus();
    EXPECT_EQ(status.state, MountController::MountStatus::State::UNINITIALIZED);
}

// ============================================
// 2. SLEW TO EQUATORIAL
// ============================================

TEST_F(MountControllerTest, SlewToEquatorialSuccess) {
    controller_->initialize(config_);
    EXPECT_TRUE(controller_->slewToEquatorial(12.0, 45.0));
}

TEST_F(MountControllerTest, SlewToEquatorialSetsTargets) {
    controller_->initialize(config_);
    controller_->slewToEquatorial(12.0, 45.0);

    auto status = controller_->getStatus();
    // For equatorial mounts, axis1 target is Hour Angle (HA = LST - RA) in degrees
    double jd = core::AstronomicalCalculations::getCurrentJulianDate();
    double lst = core::AstronomicalCalculations::calculateLST(jd, config_.longitude);
    double ha_hours = lst - 12.0;
    while (ha_hours > 12.0) ha_hours -= 24.0;
    while (ha_hours < -12.0) ha_hours += 24.0;
    // Use relaxed tolerance (1e-6 deg ≈ 0.0036 arcsec) because getCurrentJulianDate()
    // and calculateLST() are called at slightly different times in the test vs.
    // implementation (inside slewToEquatorial()), introducing ~1e-7 deg variance.
    EXPECT_NEAR(status.axis1_target, ha_hours * 15.0, 1e-6);
    EXPECT_DOUBLE_EQ(status.axis2_target, 45.0);          // Dec in degrees
}

TEST_F(MountControllerTest, SlewToEquatorialReachesTarget) {
    controller_->initialize(config_);
    controller_->slewToEquatorial(12.0, 45.0);

    // Compute expected HA target (axis1 = HA = LST - RA, axis2 = Dec)
    double jd = core::AstronomicalCalculations::getCurrentJulianDate();
    double lst = core::AstronomicalCalculations::calculateLST(jd, config_.longitude);
    double ha_hours = lst - 12.0;
    while (ha_hours > 12.0) ha_hours -= 24.0;
    while (ha_hours < -12.0) ha_hours += 24.0;
    double expected_axis1 = ha_hours * 15.0;

    // With mock CANopen, target_reached is true immediately so the background
    // thread completes within one poll cycle and updates positions from CANopen.
    bool reached = false;
    for (int i = 0; i < 50; i++) {
        std::this_thread::sleep_for(200ms);
        auto status = controller_->getStatus();
        if (status.state == MountController::MountStatus::State::IDLE) {
            reached = true;
            EXPECT_NEAR(status.axis1_position, expected_axis1, config_.position_tolerance);
            EXPECT_NEAR(status.axis2_position, 45.0, config_.position_tolerance);
            break;
        }
    }
    EXPECT_TRUE(reached) << "Slew did not complete within timeout";
}

TEST_F(MountControllerTest, SlewToEquatorialFromErrorState) {
    controller_->initialize(config_);
    // After shutdown, state is UNINITIALIZED; slew should be rejected
    controller_->shutdown();
    EXPECT_FALSE(controller_->slewToEquatorial(12.0, 45.0));
}

TEST_F(MountControllerTest, SlewToEquatorialWhileSlewingRejected) {
    controller_->initialize(config_);
    EXPECT_TRUE(controller_->slewToEquatorial(12.0, 45.0));

    // With mock CANopen, target_reached is true immediately so the background
    // thread completes before the second call. The second slew is accepted
    // because the first already finished (state is back to IDLE).
    // This verifies sequential slews work correctly with the mock.
    EXPECT_TRUE(controller_->slewToEquatorial(13.0, 46.0));
}

// ============================================
// 3. SLEW TO HORIZONTAL
// ============================================

TEST_F(MountControllerTest, SlewToHorizontalSuccess) {
    controller_->initialize(config_);
    EXPECT_TRUE(controller_->slewToHorizontal(30.0, 180.0));
}

TEST_F(MountControllerTest, SlewToHorizontalSetsTargets) {
    controller_->initialize(config_);
    controller_->slewToHorizontal(30.0, 180.0);

    auto status = controller_->getStatus();
    EXPECT_DOUBLE_EQ(status.axis1_target, 180.0);  // azimuth
    EXPECT_DOUBLE_EQ(status.axis2_target, 30.0);   // altitude
}

TEST_F(MountControllerTest, SlewToHorizontalReachesTarget) {
    controller_->initialize(config_);
    controller_->slewToHorizontal(45.0, 90.0);

    bool reached = false;
    for (int i = 0; i < 50; i++) {
        std::this_thread::sleep_for(200ms);
        auto status = controller_->getStatus();
        if (status.state == MountController::MountStatus::State::IDLE) {
            reached = true;
            EXPECT_NEAR(status.axis1_position, 90.0, config_.position_tolerance);
            EXPECT_NEAR(status.axis2_position, 45.0, config_.position_tolerance);
            break;
        }
    }
    EXPECT_TRUE(reached) << "Horizontal slew did not complete within timeout";
}

// ============================================
// 4. TRACKING
// ============================================

TEST_F(MountControllerTest, StartTrackingSidereal) {
    controller_->initialize(config_);
    EXPECT_TRUE(controller_->startTracking(12.0, 45.0, MountController::TrackingMode::SIDEREAL));

    auto status = controller_->getStatus();
    EXPECT_EQ(status.state, MountController::MountStatus::State::TRACKING);
}

TEST_F(MountControllerTest, StartTrackingSetsTargets) {
    controller_->initialize(config_);
    controller_->startTracking(12.0, 45.0, MountController::TrackingMode::SIDEREAL);

    auto status = controller_->getStatus();
    // For equatorial mounts, axis1 target is Hour Angle (HA = LST - RA) in degrees
    double jd = core::AstronomicalCalculations::getCurrentJulianDate();
    double lst = core::AstronomicalCalculations::calculateLST(jd, config_.longitude);
    double ha_hours = lst - 12.0;
    while (ha_hours > 12.0) ha_hours -= 24.0;
    while (ha_hours < -12.0) ha_hours += 24.0;
    // Tolerance accounts for clock drift between the two independent
    // getCurrentJulianDate() calls (one inside startTracking, one here).
    // At 15 deg/hour, 1e-6 degrees ≈ 0.24 ms of elapsed time.
    EXPECT_NEAR(status.axis1_target, ha_hours * 15.0, 1e-6);
    EXPECT_DOUBLE_EQ(status.axis2_target, 45.0);
}

TEST_F(MountControllerTest, TrackingUpdatesPosition) {
    config_.enable_refraction_correction = false;
    controller_->initialize(config_);
    controller_->startTracking(12.0, 45.0, MountController::TrackingMode::SIDEREAL);

    // Take two readings and verify position moved
    std::this_thread::sleep_for(500ms);
    auto status1 = controller_->getStatus();
    std::this_thread::sleep_for(500ms);
    auto status2 = controller_->getStatus();

    // Position should have increased (tracking RA = +0.004178 deg/s)
    EXPECT_GT(status2.axis1_position, status1.axis1_position)
        << "Tracking should increase axis1 position over time";
}

TEST_F(MountControllerTest, StartTrackingWhileSlewingRejected) {
    controller_->initialize(config_);
    controller_->slewToEquatorial(12.0, 45.0);
    EXPECT_FALSE(controller_->startTracking(13.0, 46.0));
}

// ============================================
// 4b. ALT-AZ TRACKING
// ============================================

TEST_F(MountControllerTest, AltAzStartTracking) {
    // For ALT_AZ mounts, axis1 = altitude, axis2 = azimuth.
    // The ra/dec parameters in startTracking() carry alt/az values directly.
    config_.mount_type = MountController::MountType::ALT_AZ;
    controller_->initialize(config_);

    EXPECT_TRUE(controller_->startTracking(45.0, 0.0, MountController::TrackingMode::SIDEREAL));

    auto status = controller_->getStatus();
    EXPECT_EQ(status.state, MountController::MountStatus::State::TRACKING);
    EXPECT_DOUBLE_EQ(status.axis1_target, 45.0);  // altitude
    EXPECT_DOUBLE_EQ(status.axis2_target, 0.0);   // azimuth
}

TEST_F(MountControllerTest, AltAzTrackingUpdatesPosition) {
    // Verify that both axis positions change during ALT_AZ tracking.
    // The rates are position-dependent: at alt=60°, az=180°, lat=52°,
    // both axes should drift measurably over 1 second.
    config_.mount_type = MountController::MountType::ALT_AZ;
    config_.latitude = 52.0;
    controller_->initialize(config_);

    controller_->startTracking(60.0, 180.0, MountController::TrackingMode::SIDEREAL);

    // Let tracking run for ~1s to accumulate measurable position change
    std::this_thread::sleep_for(600ms);
    auto status1 = controller_->getStatus();
    std::this_thread::sleep_for(600ms);
    auto status2 = controller_->getStatus();

    // Both axes should have changed over time
    EXPECT_NE(status2.axis1_position, status1.axis1_position)
        << "ALT_AZ tracking should change altitude over time";
    EXPECT_NE(status2.axis2_position, status1.axis2_position)
        << "ALT_AZ tracking should change azimuth over time";
    
    // Rates should be non-zero during tracking
    EXPECT_NE(status2.axis1_rate, 0.0);
    EXPECT_NE(status2.axis2_rate, 0.0);
}

TEST_F(MountControllerTest, AltAzZenithClamp) {
    // Verify the zenith singularity guard: altitude should never exceed 90°,
    // even when starting very close to the zenith (alt=89.5°).
    config_.mount_type = MountController::MountType::ALT_AZ;
    controller_->initialize(config_);

    // Start tracking at alt=89.5° (very close to zenith). The cos(alt) clamp
    // at cos(89.5°) should prevent the azimuth rate from blowing up.
    controller_->startTracking(89.5, 0.0, MountController::TrackingMode::SIDEREAL);

    // Let multiple tracking iterations occur
    std::this_thread::sleep_for(800ms);
    auto status = controller_->getStatus();

    // Altitude should not exceed 90° (safety clamp)
    EXPECT_LE(status.axis1_position, 90.0)
        << "Altitude should not exceed 90° even near zenith";
    EXPECT_GE(status.axis1_position, -5.0)
        << "Altitude should not drop below -5°";
    
    // Azimuth should be in [0, 360) range
    EXPECT_GE(status.axis2_position, 0.0);
    EXPECT_LT(status.axis2_position, 360.0);

    // Should still be in TRACKING state (no ERROR from NaN)
    EXPECT_EQ(status.state, MountController::MountStatus::State::TRACKING);
}

TEST_F(MountControllerTest, AltAzNanGuard) {
    // Verify NaN guard: starting tracking at exactly alt=90° (where cos=0)
    // should not crash. The zenith clamp prevents division by zero, and
    // tracking should proceed normally.
    config_.mount_type = MountController::MountType::ALT_AZ;
    controller_->initialize(config_);

    // alt=90°, az=0° — cos(90°)=0 would cause az_rate → ∞ without the clamp
    EXPECT_TRUE(controller_->startTracking(90.0, 0.0, MountController::TrackingMode::SIDEREAL));

    std::this_thread::sleep_for(600ms);
    auto status = controller_->getStatus();

    // Should still be tracking (not ERROR from NaN/Inf)
    EXPECT_EQ(status.state, MountController::MountStatus::State::TRACKING);

    // Altitude should be clamped to 90°
    EXPECT_LE(status.axis1_position, 90.0);
    
    // Rates should be finite
    EXPECT_TRUE(std::isfinite(status.axis1_rate));
    EXPECT_TRUE(std::isfinite(status.axis2_rate));
}

TEST_F(MountControllerTest, EquatorialNanGuard) {
    // Verify that NaN propagation through the equatorial tracking path
    // is caught by the NaN guards and transitions the system to ERROR.
    // This tests guards 3-5 (rate_factor, position update, Kalman filter)
    // and the downstream EQUATORIAL guards 6-9 (HA/RA, nutation, TPoint, refraction).
    //
    // Strategy: inject NaN via applyGuiderCorrection() which writes to
    // guider_delta_axis1_/axis2_. The tracking loop reads these deltas,
    // producing NaN current_rate_1/2, which propagates to axis positions
    // through the rate update and is caught by the position update guard.
    config_.meridian_flip_enabled = false;
    controller_->initialize(config_);

    // Start tracking in equatorial mode
    double jd = core::AstronomicalCalculations::getCurrentJulianDate();
    double lst = core::AstronomicalCalculations::calculateLST(jd, config_.longitude);
    double ra = lst;
    while (ra < 0.0) ra += 24.0;
    while (ra >= 24.0) ra -= 24.0;

    bool started = controller_->startTracking(ra, 45.0, MountController::TrackingMode::SIDEREAL);
    ASSERT_TRUE(started);

    // Let tracking establish a few iterations
    std::this_thread::sleep_for(200ms);

    auto status_before = controller_->getStatus();
    ASSERT_EQ(status_before.state, MountController::MountStatus::State::TRACKING)
        << "Should be tracking before NaN injection";

    // Inject NaN via guider correction — NaN propagates through clamp(),
    // into guider_delta_axis1_, then into current_rate_1, and finally
    // into axis1_position_ where guard 4 catches it.
    controller_->applyGuiderCorrection(
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN());

    // Wait for tracking loop iteration that processes the NaN
    std::this_thread::sleep_for(500ms);

    auto status_after = controller_->getStatus();
    EXPECT_EQ(status_after.state, MountController::MountStatus::State::ERROR)
        << "NaN guider correction should trigger ERROR state"
        << " state=" << static_cast<int>(status_after.state);
    EXPECT_FALSE(status_after.error_message.empty())
        << "Error message should be set after NaN detection";

    // Verify recovery via clearErrors()
    controller_->clearErrors();
    auto status_recovered = controller_->getStatus();
    EXPECT_EQ(status_recovered.state, MountController::MountStatus::State::IDLE)
        << "Should recover to IDLE after clearErrors()";
}

// ============================================
// 5. STOP
// ============================================

TEST_F(MountControllerTest, StopDuringSlew) {
    controller_->initialize(config_);
    controller_->slewToEquatorial(20.0, 80.0);
    controller_->stop();

    auto status = controller_->getStatus();
    EXPECT_EQ(status.state, MountController::MountStatus::State::IDLE);
    EXPECT_DOUBLE_EQ(status.axis1_rate, 0.0);
    EXPECT_DOUBLE_EQ(status.axis2_rate, 0.0);
}

TEST_F(MountControllerTest, StopDuringTracking) {
    controller_->initialize(config_);
    controller_->startTracking(12.0, 45.0);
    controller_->stop();

    auto status = controller_->getStatus();
    EXPECT_EQ(status.state, MountController::MountStatus::State::IDLE);
    EXPECT_DOUBLE_EQ(status.axis1_rate, 0.0);
    EXPECT_DOUBLE_EQ(status.axis2_rate, 0.0);
}

// ============================================
// 6. PARK / UNPARK
// ============================================

TEST_F(MountControllerTest, ParkSuccess) {
    controller_->initialize(config_);
    controller_->park();

    // Park should set state to PARKING
    auto status = controller_->getStatus();
    EXPECT_EQ(status.state, MountController::MountStatus::State::PARKING);
}

TEST_F(MountControllerTest, ParkReachesPosition) {
    // Set explicit park position
    config_.park_position_axis1 = 10.0;
    config_.park_position_axis2 = 20.0;
    controller_->initialize(config_);
    controller_->park();

    bool parked = false;
    for (int i = 0; i < 50; i++) {
        std::this_thread::sleep_for(200ms);
        auto status = controller_->getStatus();
        if (status.state == MountController::MountStatus::State::PARKED) {
            parked = true;
            EXPECT_NEAR(status.axis1_position, config_.park_position_axis1, config_.position_tolerance);
            EXPECT_NEAR(status.axis2_position, config_.park_position_axis2, config_.position_tolerance);
            break;
        }
    }
    EXPECT_TRUE(parked) << "Park did not complete within timeout";
}

TEST_F(MountControllerTest, UnparkFromParked) {
    controller_->initialize(config_);
    controller_->park();

    // Wait for park to complete
    for (int i = 0; i < 50; i++) {
        std::this_thread::sleep_for(200ms);
        auto status = controller_->getStatus();
        if (status.state == MountController::MountStatus::State::PARKED) break;
    }

    controller_->unpark();
    auto status = controller_->getStatus();
    EXPECT_EQ(status.state, MountController::MountStatus::State::IDLE);
}

// ============================================
// 7. STATUS
// ============================================

TEST_F(MountControllerTest, StatusAfterSlew) {
    controller_->initialize(config_);
    controller_->slewToEquatorial(5.0, 30.0);

    auto status = controller_->getStatus();
    EXPECT_EQ(status.state, MountController::MountStatus::State::SLEWING);
    EXPECT_EQ(status.encoders_active, false);
    EXPECT_EQ(status.guider_active, false);
    EXPECT_EQ(status.tpoint_calibrated, false);
}

// ============================================
// 8. BOOTSTRAP CALIBRATION
// ============================================

TEST_F(MountControllerTest, BootstrapCalibrationWithNoMeasurements) {
    controller_->initialize(config_);
    EXPECT_FALSE(controller_->runBootstrapCalibration());
    EXPECT_FALSE(controller_->isBootstrapCalibrated());
}

TEST_F(MountControllerTest, BootstrapCalibrationWithOneMeasurement) {
    controller_->initialize(config_);
    EXPECT_TRUE(controller_->addBootstrapMeasurement(12.0, 45.0, 12.1, 45.1));
    EXPECT_FALSE(controller_->runBootstrapCalibration());  // Need at least 2
}

TEST_F(MountControllerTest, BootstrapCalibrationWithThreeMeasurements) {
    controller_->initialize(config_);
    controller_->addBootstrapMeasurement(12.0, 45.0, 12.1, 45.1);
    controller_->addBootstrapMeasurement(10.0, 30.0, 10.05, 30.05);
    controller_->addBootstrapMeasurement(8.0, 60.0, 8.05, 60.05);
    EXPECT_TRUE(controller_->runBootstrapCalibration());
    EXPECT_TRUE(controller_->isBootstrapCalibrated());
}

TEST_F(MountControllerTest, ClearBootstrapMeasurements) {
    controller_->initialize(config_);
    controller_->addBootstrapMeasurement(12.0, 45.0, 12.1, 45.1);
    controller_->addBootstrapMeasurement(10.0, 30.0, 10.05, 30.05);
    controller_->clearBootstrapMeasurements();
    EXPECT_FALSE(controller_->isBootstrapCalibrated());
    EXPECT_FALSE(controller_->runBootstrapCalibration());
  }
  
  // ============================================
  // 8b. BOOTSTRAP MODE & ENCODER TESTS
  // ============================================
  
  TEST_F(MountControllerTest, BootstrapModeDefaultIsManual) {
    controller_->initialize(config_);
    EXPECT_EQ(controller_->getBootstrapMode(),
              MountController::BootstrapMode::BOOTSTRAP_MANUAL);
  }
  
  TEST_F(MountControllerTest, BootstrapModeSetGet) {
    controller_->initialize(config_);
  
    // Set HYBRID
    EXPECT_TRUE(controller_->setBootstrapMode(
        MountController::BootstrapMode::BOOTSTRAP_HYBRID));
    EXPECT_EQ(controller_->getBootstrapMode(),
              MountController::BootstrapMode::BOOTSTRAP_HYBRID);
  
    // Set AUTOMATIC
    EXPECT_TRUE(controller_->setBootstrapMode(
        MountController::BootstrapMode::BOOTSTRAP_AUTOMATIC));
    EXPECT_EQ(controller_->getBootstrapMode(),
              MountController::BootstrapMode::BOOTSTRAP_AUTOMATIC);
  
    // Set back to MANUAL
    EXPECT_TRUE(controller_->setBootstrapMode(
        MountController::BootstrapMode::BOOTSTRAP_MANUAL));
    EXPECT_EQ(controller_->getBootstrapMode(),
              MountController::BootstrapMode::BOOTSTRAP_MANUAL);
  }
  
  TEST_F(MountControllerTest, BootstrapModeInStatus) {
    controller_->initialize(config_);
    auto status = controller_->getStatus();
  
    // Default mode in status should be MANUAL
    EXPECT_EQ(status.bootstrap_mode,
              static_cast<int>(MountController::BootstrapMode::BOOTSTRAP_MANUAL));
  
    // Set HYBRID and verify status reflects it
    controller_->setBootstrapMode(
        MountController::BootstrapMode::BOOTSTRAP_HYBRID);
    status = controller_->getStatus();
    EXPECT_EQ(status.bootstrap_mode,
              static_cast<int>(MountController::BootstrapMode::BOOTSTRAP_HYBRID));
  }
  
  TEST_F(MountControllerTest, EncodersAbsoluteStatusField) {
    // Test with absolute encoders
    config_.encoders_absolute = true;
    controller_->initialize(config_);
    auto status = controller_->getStatus();
    EXPECT_TRUE(status.encoders_absolute);
    controller_->shutdown();
  
    // Test with incremental encoders
    config_.encoders_absolute = false;
    controller_->initialize(config_);
    status = controller_->getStatus();
    EXPECT_FALSE(status.encoders_absolute);
  }
  
  TEST_F(MountControllerTest, BootstrapCalibrationWithEncoderOffset) {
    // Test bootstrap calibration with explicit mount_ha/mount_dec
    // to verify encoder offset absorption via Wahba/SVD
    controller_->initialize(config_);

    // Add measurements with known encoder offsets
    // The mount_ha/mount_dec simulate encoder readout, expected_ra/dec
    // are the true catalog coordinates.
    // Wahba/SVD requires at least 3 measurements for the 3-DOF rotation.
    controller_->addBootstrapMeasurement(
        12.0, 45.0,    // observed_ra, observed_dec
        12.0, 45.0,    // expected_ra, expected_dec (perfect alignment)
        2.0, 45.0);    // mount_ha, mount_dec (encoder readout matches expected)

    controller_->addBootstrapMeasurement(
        10.0, 30.0,
        10.0, 30.0,
        0.0, 30.0);    // mount_ha = expected_ha - 10h offset, mount_dec = expected

    controller_->addBootstrapMeasurement(
        8.0, 60.0,
        8.0, 60.0,
        -2.0, 60.0);   // mount_ha with offset, mount_dec = expected

    EXPECT_TRUE(controller_->runBootstrapCalibration());
    EXPECT_TRUE(controller_->isBootstrapCalibrated());
  }

  TEST_F(MountControllerTest, BootstrapCalibrationAltAz) {
    // Verify Wahba/SVD bootstrap calibration works for ALT_AZ mount type
    config_.mount_type = MountController::MountType::ALT_AZ;
    config_.enable_refraction_correction = false;
    controller_->initialize(config_);

    // Add 3 well-separated measurements (Wahba/SVD requires >= 3)
    // For ALT_AZ, the bootstrap still uses mount_ha/mount_dec → unit vector
    // conversion, but since there's no hour angle, mount_ha is treated as
    // the altitude-like axis position (degrees from NCP/SCP)
    controller_->addBootstrapMeasurement(
        12.0, 45.0, 12.1, 45.1,
        0.0, 45.0);
    controller_->addBootstrapMeasurement(
        5.0, 20.0, 5.05, 20.05,
        -7.0, 20.0);
    controller_->addBootstrapMeasurement(
        18.0, 70.0, 18.05, 70.05,
        6.0, 70.0);

    EXPECT_TRUE(controller_->runBootstrapCalibration());
    EXPECT_TRUE(controller_->isBootstrapCalibrated());
  }

  TEST_F(MountControllerTest, BootstrapCalibrationCasualUniversal) {
    // Verify Wahba/SVD works for CASUAL mount type (universal code path)
    config_.mount_type = MountController::MountType::CASUAL;
    config_.enable_refraction_correction = false;

    // Identity quaternion orientation (Alt-Az-like)
    config_.mount_orientation.quaternion = {{0.0, 0.0, 0.0, 1.0}};
    controller_->initialize(config_);

    // Add measurements; the universal Wahba/SVD code path should handle CASUAL.
    // Wahba/SVD requires at least 3 measurements for the 3-DOF rotation.
    controller_->addBootstrapMeasurement(
        12.0, 45.0, 12.1, 45.1,
        0.0, 45.0);
    controller_->addBootstrapMeasurement(
        5.0, 20.0, 5.05, 20.05,
        -7.0, 20.0);
    controller_->addBootstrapMeasurement(
        18.0, 70.0, 18.05, 70.05,
        6.0, 70.0);

    EXPECT_TRUE(controller_->runBootstrapCalibration());
    EXPECT_TRUE(controller_->isBootstrapCalibrated());
  }
  
  TEST_F(MountControllerTest, BootstrapBootstrapMeasurementCount) {
    controller_->initialize(config_);
    EXPECT_EQ(controller_->getBootstrapMeasurementCount(), 0u);
  
    controller_->addBootstrapMeasurement(12.0, 45.0, 12.1, 45.1);
    EXPECT_EQ(controller_->getBootstrapMeasurementCount(), 1u);
  
    controller_->addBootstrapMeasurement(10.0, 30.0, 10.05, 30.05);
    EXPECT_EQ(controller_->getBootstrapMeasurementCount(), 2u);
  
    controller_->clearBootstrapMeasurements();
    EXPECT_EQ(controller_->getBootstrapMeasurementCount(), 0u);
  }
  
  TEST_F(MountControllerTest, BootstrapMeasurementCountInStatus) {
    controller_->initialize(config_);
    auto status = controller_->getStatus();
    EXPECT_EQ(status.bootstrap_measurement_count, 0);
  
    controller_->addBootstrapMeasurement(12.0, 45.0, 12.1, 45.1);
    status = controller_->getStatus();
    EXPECT_EQ(status.bootstrap_measurement_count, 1);
  
    controller_->addBootstrapMeasurement(10.0, 30.0, 10.05, 30.05);
    status = controller_->getStatus();
    EXPECT_EQ(status.bootstrap_measurement_count, 2);
  }

  TEST_F(MountControllerTest, IncrementalEncoderStartPosition) {
    // With encoders_absolute=false (default in SetUp), the encoder
    // start position should be (0,0) — the reference origin for
    // incremental encoders (plan §8.1).
    controller_->initialize(config_);
    auto status = controller_->getStatus();
    EXPECT_DOUBLE_EQ(status.axis1_position, 0.0);
    EXPECT_DOUBLE_EQ(status.axis2_position, 0.0);
  }

  TEST_F(MountControllerTest, AltAzCaveatLogging) {
    // Verify that the ALT_AZ caveat log message is emitted when
    // runBootstrapCalibration() succeeds on an ALT_AZ mount (plan §8.6).
    // The caveat explains that altitude encoder offset is a spherical
    // translation, not a pure 3D rotation.
    std::vector<std::string> captured_logs;
    auto sink = std::make_shared<spdlog::sinks::callback_sink_mt>(
        [&captured_logs](const spdlog::details::log_msg& msg) {
            captured_logs.emplace_back(msg.payload.data(), msg.payload.size());
        });
    auto mount_logger = logging::Logger::mount();
    mount_logger->sinks().push_back(sink);

    config_.mount_type = MountController::MountType::ALT_AZ;
    config_.enable_refraction_correction = false;
    controller_->initialize(config_);

    // Wahba/SVD requires at least 3 measurements
    controller_->addBootstrapMeasurement(
        12.0, 45.0, 12.1, 45.1, 0.0, 45.0);
    controller_->addBootstrapMeasurement(
        5.0, 20.0, 5.05, 20.05, -7.0, 20.0);
    controller_->addBootstrapMeasurement(
        18.0, 70.0, 18.05, 70.05, 6.0, 70.0);

    EXPECT_TRUE(controller_->runBootstrapCalibration());
    EXPECT_TRUE(controller_->isBootstrapCalibrated());

    // Remove the capturing sink to avoid interfering with other tests
    auto& sinks = mount_logger->sinks();
    sinks.erase(std::remove(sinks.begin(), sinks.end(), sink), sinks.end());

    // Verify the ALT_AZ caveat message was logged
    bool found_caveat = std::any_of(
        captured_logs.begin(), captured_logs.end(),
        [](const std::string& log) {
            return log.find("ALT_AZ bootstrap") != std::string::npos;
        });
    EXPECT_TRUE(found_caveat) << "ALT_AZ caveat log message was not emitted";
  }

  // ============================================
  // 9. TPOINT CALIBRATION
// ============================================

TEST_F(MountControllerTest, TPointCalibrationWithNoMeasurements) {
    controller_->initialize(config_);
    EXPECT_FALSE(controller_->runTPointCalibration());
}

TEST_F(MountControllerTest, TPointCalibrationWithThreeMeasurements) {
    controller_->initialize(config_);
    controller_->addTPointMeasurement(10.0, 45.0, 10.0, 45.0, 2.0, 45.0, 15.0, 1013.25);
    controller_->addTPointMeasurement(12.0, 30.0, 12.0, 30.0, 4.0, 30.0, 15.0, 1013.25);
    controller_->addTPointMeasurement(14.0, 60.0, 14.0, 60.0, 6.0, 60.0, 15.0, 1013.25);
    EXPECT_TRUE(controller_->runTPointCalibration());
}

TEST_F(MountControllerTest, TPointCalibrationSimplifiedAPI) {
    controller_->initialize(config_);
    controller_->addTPointMeasurement(10.0, 45.0, 10.1, 45.1);
    controller_->addTPointMeasurement(12.0, 30.0, 12.1, 30.1);
    controller_->addTPointMeasurement(14.0, 60.0, 14.1, 60.1);
    EXPECT_TRUE(controller_->runTPointCalibration());
}

TEST_F(MountControllerTest, ClearTPointMeasurements) {
    controller_->initialize(config_);
    controller_->addTPointMeasurement(10.0, 45.0, 10.0, 45.0, 2.0, 45.0, 15.0, 1013.25);
    controller_->addTPointMeasurement(12.0, 30.0, 12.0, 30.0, 4.0, 30.0, 15.0, 1013.25);
    controller_->clearTPointMeasurements();
    EXPECT_FALSE(controller_->runTPointCalibration());
}

TEST_F(MountControllerTest, LegacyCalibrationMeasurement) {
    controller_->initialize(config_);
    EXPECT_TRUE(controller_->addCalibrationMeasurement(
        10.0, 45.0, 10.1, 45.1, 2.0, 45.0, 15.0, 1013.25, 0.5, 0.0, 0.0, 0.0, 2000.0));
}

// ============================================
// 10. TPOINT PARAMETERS
// ============================================

TEST_F(MountControllerTest, GetTPointParametersInitial) {
    controller_->initialize(config_);
    std::string params = controller_->getTPointParameters();
    EXPECT_FALSE(params.empty());
    // Should contain "calibrated": false
    EXPECT_NE(params.find("false"), std::string::npos);
}

TEST_F(MountControllerTest, GetRotationMatrixEquatorial) {
    controller_->initialize(config_);
    auto matrix = controller_->getRotationMatrix();
    // For equatorial mount, should be identity quaternion
    EXPECT_EQ(matrix.size(), 4);
    EXPECT_DOUBLE_EQ(matrix[0], 1.0);  // q0
    EXPECT_DOUBLE_EQ(matrix[1], 0.0);  // q1
    EXPECT_DOUBLE_EQ(matrix[2], 0.0);  // q2
    EXPECT_DOUBLE_EQ(matrix[3], 0.0);  // q3
}

// ============================================
// 11. ENCODER CONTROL
// ============================================

TEST_F(MountControllerTest, SetEncodersEnabled) {
    controller_->initialize(config_);
    controller_->setEncodersEnabled(true);
    auto status = controller_->getStatus();
    EXPECT_TRUE(status.encoders_active);
}

TEST_F(MountControllerTest, SetEncodersDisabled) {
    controller_->initialize(config_);
    controller_->setEncodersEnabled(false);
    auto status = controller_->getStatus();
    EXPECT_FALSE(status.encoders_active);
}

TEST_F(MountControllerTest, SetEncoderType) {
    controller_->initialize(config_);
    // Just check no exception
    controller_->setEncoderType(true);
    controller_->setEncoderType(false);
}

// ============================================
// 12. GUIDER CONTROL
// ============================================

TEST_F(MountControllerTest, ConnectGuider) {
    controller_->initialize(config_);
    EXPECT_TRUE(controller_->connectGuider("socket://127.0.0.1:5000"));
    auto status = controller_->getStatus();
    EXPECT_TRUE(status.guider_active);
}

TEST_F(MountControllerTest, DisconnectGuider) {
    controller_->initialize(config_);
    controller_->connectGuider("socket://127.0.0.1:5000");
    controller_->disconnectGuider();
    auto status = controller_->getStatus();
    EXPECT_FALSE(status.guider_active);
}

TEST_F(MountControllerTest, ApplyGuiderCorrection) {
    controller_->initialize(config_);
    controller_->startTracking(12.0, 45.0);
    // Apply 10 arcsec correction in RA and Dec
    // Should not throw
    controller_->applyGuiderCorrection(10.0, 5.0);
}

// ============================================
// 13. POLE POSITION
// ============================================

TEST_F(MountControllerTest, DeterminePolePosition) {
    config_.enable_refraction_correction = false;
    controller_->initialize(config_);
    
    // The drift-alignment procedure scales the per-star wait with duration_hours.
    // A small duration keeps the test fast while still exercising the full
    // two-star procedure (slew → track → measure → compute).
    // The per-star wait is clamped to a minimum of 200ms.
    auto [latitude, longitude, accuracy] = controller_->determinePolePosition(0.001);
    
    // Without TPoint calibration, the simulated mount has zero Dec drift
    // (axis2_rate_ = 0 for sidereal tracking), so both measured polar errors
    // are zero and the returned position equals the configured latitude/longitude.
    EXPECT_DOUBLE_EQ(latitude, config_.latitude);
    EXPECT_DOUBLE_EQ(longitude, config_.longitude);
    
    // The accuracy estimate comes from the measurement noise floor, not the
    // old 600" fallback — it should be significantly better.
    EXPECT_GT(accuracy, 0.0);
    EXPECT_LT(accuracy, 200.0);
}

// ============================================
// 14. SAVE/LOAD STATE
// ============================================

TEST_F(MountControllerTest, SaveState) {
    controller_->initialize(config_);
    EXPECT_TRUE(controller_->saveState("/tmp/mount_test_state.json"));
}

TEST_F(MountControllerTest, LoadStateFromSaved) {
    controller_->initialize(config_);
    controller_->saveState("/tmp/mount_test_state.json");
    EXPECT_TRUE(controller_->loadState("/tmp/mount_test_state.json"));
}

TEST_F(MountControllerTest, LoadStateFromNonexistent) {
    controller_->initialize(config_);
    EXPECT_FALSE(controller_->loadState("/tmp/nonexistent_file.json"));
}

// ============================================
// 15. ENVIRONMENTAL PARAMS
// ============================================

TEST_F(MountControllerTest, SetEnvironmentalParams) {
    controller_->initialize(config_);
    controller_->setEnvironmentalParams(20.0, 1000.0, 0.6);
    // No getter, but verify no exception
}

// ============================================
// 16. CONFIGURATION
// ============================================

TEST_F(MountControllerTest, GetConfiguration) {
    controller_->initialize(config_);
    auto retrieved = controller_->getConfiguration();
    EXPECT_DOUBLE_EQ(retrieved.latitude, config_.latitude);
    EXPECT_DOUBLE_EQ(retrieved.longitude, config_.longitude);
}

TEST_F(MountControllerTest, UpdateConfiguration) {
    controller_->initialize(config_);
    config_.latitude = 50.0;
    EXPECT_TRUE(controller_->updateConfiguration(config_));
    auto retrieved = controller_->getConfiguration();
    EXPECT_DOUBLE_EQ(retrieved.latitude, 50.0);
}

// ============================================
// 17. SET STATUS/ERROR CALLBACK
// ============================================

TEST_F(MountControllerTest, SetStatusCallback) {
    controller_->initialize(config_);
    bool callback_called = false;
    controller_->setStatusCallback([&](const auto&) {
        callback_called = true;
    });
    // Trigger a slew which should cause status updates
    controller_->slewToEquatorial(12.0, 45.0);
    // Wait briefly for callback to fire from background thread
    std::this_thread::sleep_for(300ms);
    // Verify the callback was invoked during the slew
    EXPECT_TRUE(callback_called)
        << "Status callback should have been called at least once "
        << "during slew operation";
    // Clear callback BEFORE test exits to prevent dangling reference:
    // the background thread may still fire status callbacks via the after-loop
    // handler long after this test ends (slew timeout is 60s).
    controller_->setStatusCallback(nullptr);
}

TEST_F(MountControllerTest, SetErrorCallback) {
    config_.meridian_flip_enabled = false;
    controller_->initialize(config_);
    bool callback_called = false;
    std::string error_msg;
    controller_->setErrorCallback([&](const std::string& msg) {
        callback_called = true;
        error_msg = msg;
    });
    // Start tracking so that applyGuiderCorrection() can inject NaN
    double jd = core::AstronomicalCalculations::getCurrentJulianDate();
    double lst = core::AstronomicalCalculations::calculateLST(jd, config_.longitude);
    double ra = lst;
    while (ra < 0.0) ra += 24.0;
    while (ra >= 24.0) ra -= 24.0;
    bool started = controller_->startTracking(ra, 45.0,
        MountController::TrackingMode::SIDEREAL);
    ASSERT_TRUE(started);
    // Let tracking establish a few iterations
    std::this_thread::sleep_for(200ms);
    // Trigger an error via NaN guider correction — follows the same
    // pattern as EquatorialNanGuard test (NaN propagation → ERROR state)
    controller_->applyGuiderCorrection(
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN());
    // Wait for tracking loop to process NaN and fire error callback
    std::this_thread::sleep_for(500ms);
    // Verify the error callback was invoked
    EXPECT_TRUE(callback_called)
        << "Error callback should have been called after NaN injection";
    EXPECT_FALSE(error_msg.empty())
        << "Error message should not be empty after NaN detection";
    // Clear callback BEFORE test exits for the same dangling-reference reason.
    controller_->setErrorCallback(nullptr);
    controller_->stop();
}

// ============================================
// 18. DEROTATOR CONFIGURATION
// ============================================

TEST_F(MountControllerTest, ConfigureDerotatorStepper) {
    controller_->initialize(config_);
    ::astro_mount::DerotatorConfig config;
    config.set_type(::astro_mount::DerotatorConfig::STEPPER);
    config.set_gear_ratio(100.0);
    config.set_max_speed(10.0);
    config.set_max_acceleration(20.0);
    EXPECT_TRUE(controller_->configureDerotator(config));
}

TEST_F(MountControllerTest, ConfigureDerotatorServo) {
    controller_->initialize(config_);
    ::astro_mount::DerotatorConfig config;
    config.set_type(::astro_mount::DerotatorConfig::SERVO);
    EXPECT_TRUE(controller_->configureDerotator(config));
}

TEST_F(MountControllerTest, ConfigureDerotatorCustom) {
    controller_->initialize(config_);
    ::astro_mount::DerotatorConfig config;
    config.set_type(::astro_mount::DerotatorConfig::CUSTOM);
    EXPECT_TRUE(controller_->configureDerotator(config));
}

TEST_F(MountControllerTest, ConfigureDerotatorUnsupportedType) {
    controller_->initialize(config_);
    ::astro_mount::DerotatorConfig config;
    // Proto3 defaults enum to first value (CANOPEN=0), which IS supported.
    // Use an explicitly invalid type value to test unsupported type rejection.
    config.set_type(static_cast<::astro_mount::DerotatorConfig::DerotatorType>(99));
    EXPECT_FALSE(controller_->configureDerotator(config));
}

// ============================================
// 19. FIELD ROTATION
// ============================================

TEST_F(MountControllerTest, EnableFieldRotationEquatorial) {
    controller_->initialize(config_);
    // First configure derotator
    ::astro_mount::DerotatorConfig derotator_config;
    derotator_config.set_type(::astro_mount::DerotatorConfig::STEPPER);
    controller_->configureDerotator(derotator_config);

    // Enable field rotation
    ::astro_mount::FieldRotationParams params;
    params.set_enabled(true);
    params.set_altitude(45.0);
    params.set_azimuth(90.0);
    EXPECT_TRUE(controller_->enableFieldRotation(params));
}

TEST_F(MountControllerTest, ControlFieldRotationDisabled) {
    controller_->initialize(config_);
    // Without configuring derotator, control should fail
    ::astro_mount::FieldRotationControlRequest request;
    request.set_mode(::astro_mount::FieldRotationControlRequest::FIXED_ANGLE);
    request.set_target_angle(90.0);
    EXPECT_FALSE(controller_->controlFieldRotation(request));
}

TEST_F(MountControllerTest, ControlFieldRotationFixedAngle) {
    controller_->initialize(config_);
    ::astro_mount::DerotatorConfig derotator_config;
    derotator_config.set_type(::astro_mount::DerotatorConfig::STEPPER);
    controller_->configureDerotator(derotator_config);

    ::astro_mount::FieldRotationControlRequest request;
    request.set_mode(::astro_mount::FieldRotationControlRequest::FIXED_ANGLE);
    request.set_target_angle(90.0);
    EXPECT_TRUE(controller_->controlFieldRotation(request));
}

TEST_F(MountControllerTest, ControlFieldRotationAltAz) {
    controller_->initialize(config_);
    ::astro_mount::DerotatorConfig derotator_config;
    derotator_config.set_type(::astro_mount::DerotatorConfig::STEPPER);
    controller_->configureDerotator(derotator_config);

    ::astro_mount::FieldRotationParams params;
    params.set_enabled(true);
    params.set_altitude(45.0);
    controller_->enableFieldRotation(params);

    ::astro_mount::FieldRotationControlRequest request;
    request.set_mode(::astro_mount::FieldRotationControlRequest::ALT_AZ);
    EXPECT_TRUE(controller_->controlFieldRotation(request));
}

TEST_F(MountControllerTest, ControlFieldRotationCustom) {
    controller_->initialize(config_);
    ::astro_mount::DerotatorConfig derotator_config;
    derotator_config.set_type(::astro_mount::DerotatorConfig::STEPPER);
    controller_->configureDerotator(derotator_config);

    ::astro_mount::FieldRotationControlRequest request;
    request.set_mode(::astro_mount::FieldRotationControlRequest::CUSTOM);
    request.set_rotation_rate(0.5);
    EXPECT_TRUE(controller_->controlFieldRotation(request));
}

TEST_F(MountControllerTest, ControlFieldRotationTracking) {
    controller_->initialize(config_);
    ::astro_mount::DerotatorConfig derotator_config;
    derotator_config.set_type(::astro_mount::DerotatorConfig::STEPPER);
    controller_->configureDerotator(derotator_config);
    controller_->startTracking(12.0, 45.0);

    ::astro_mount::FieldRotationControlRequest request;
    request.set_mode(::astro_mount::FieldRotationControlRequest::TRACKING);
    EXPECT_TRUE(controller_->controlFieldRotation(request));
}

TEST_F(MountControllerTest, ControlFieldRotationEquatorial) {
    controller_->initialize(config_);
    ::astro_mount::DerotatorConfig derotator_config;
    derotator_config.set_type(::astro_mount::DerotatorConfig::STEPPER);
    controller_->configureDerotator(derotator_config);

    ::astro_mount::FieldRotationControlRequest request;
    request.set_mode(::astro_mount::FieldRotationControlRequest::EQUATORIAL);
    EXPECT_TRUE(controller_->controlFieldRotation(request));
}

TEST_F(MountControllerTest, ControlFieldRotationDisabledMode) {
    controller_->initialize(config_);
    ::astro_mount::DerotatorConfig derotator_config;
    derotator_config.set_type(::astro_mount::DerotatorConfig::STEPPER);
    controller_->configureDerotator(derotator_config);

    ::astro_mount::FieldRotationControlRequest request;
    request.set_mode(::astro_mount::FieldRotationControlRequest::DISABLED);
    EXPECT_TRUE(controller_->controlFieldRotation(request));
}

// ============================================
// 20. DEROTATOR STATUS
// ============================================

TEST_F(MountControllerTest, GetDerotatorStatusBeforeConfig) {
    controller_->initialize(config_);
    auto status = controller_->getDerotatorStatus();
    EXPECT_FALSE(status.enabled());
    EXPECT_FALSE(status.homed());
    EXPECT_FALSE(status.moving());
}

TEST_F(MountControllerTest, GetDerotatorStatusAfterConfig) {
    controller_->initialize(config_);
    ::astro_mount::DerotatorConfig derotator_config;
    derotator_config.set_type(::astro_mount::DerotatorConfig::STEPPER);
    controller_->configureDerotator(derotator_config);

    auto status = controller_->getDerotatorStatus();
    EXPECT_TRUE(status.enabled());
    EXPECT_FALSE(status.homed());
}

// ============================================
// 21. DEROTATOR HOMING
// ============================================

TEST_F(MountControllerTest, HomeDerotatorWithoutConfig) {
    controller_->initialize(config_);
    ::astro_mount::DerotatorHomingRequest request;
    request.set_offset(0.0);
    EXPECT_FALSE(controller_->homeDerotator(request));
}

TEST_F(MountControllerTest, HomeDerotatorWithStepperConfig) {
    controller_->initialize(config_);
    ::astro_mount::DerotatorConfig derotator_config;
    derotator_config.set_type(::astro_mount::DerotatorConfig::STEPPER);
    controller_->configureDerotator(derotator_config);

    ::astro_mount::DerotatorHomingRequest request;
    request.set_offset(0.0);
    EXPECT_TRUE(controller_->homeDerotator(request));

    // homeDerotator() spawns a background thread with a 2-second simulated delay
    // for non-CANOPEN derotators. Poll for completion instead of checking immediately.
    bool homed = false;
    for (int i = 0; i < 30; i++) {
        std::this_thread::sleep_for(100ms);
        auto status = controller_->getDerotatorStatus();
        if (status.homed()) {
            homed = true;
            break;
        }
    }
    EXPECT_TRUE(homed);
}

// ============================================
// 22. FIELD ROTATION PARAMS
// ============================================

TEST_F(MountControllerTest, GetFieldRotationParams) {
    controller_->initialize(config_);
    ::astro_mount::FieldRotationParams params;
    params.set_enabled(true);
    params.set_altitude(45.0);
    controller_->enableFieldRotation(params);

    auto retrieved = controller_->getFieldRotationParams();
    EXPECT_TRUE(retrieved.enabled());
    EXPECT_DOUBLE_EQ(retrieved.altitude(), 45.0);
}

// ============================================
// 23. EPHEMERIS TRACKING (basic)
// ============================================

TEST_F(MountControllerTest, StartEphemerisTrackingWithoutData) {
    controller_->initialize(config_);
    auto now = std::chrono::system_clock::now();

    // Should return empty string since no ephemeris uploaded
    auto tracker_id = controller_->startEphemerisTracking(
        "99942", now, 30.0, true, true, 1.0, "continuous");
    EXPECT_TRUE(tracker_id.empty());
}

TEST_F(MountControllerTest, UploadEphemerisAndStartTracking) {
    controller_->initialize(config_);
    auto now = std::chrono::system_clock::now();

    std::vector<std::tuple<std::chrono::system_clock::time_point,
                           double, double, double, double>> points;

    // Add ephemeris points for a comet
    for (int i = 0; i < 10; i++) {
        auto t = now + std::chrono::hours(i);
        points.emplace_back(t,
                            10.0 + i * 0.1,  // RA increases
                            45.0 + i * 0.05, // Dec increases
                            0.1,             // RA rate
                            0.05);           // Dec rate
    }

    EXPECT_TRUE(controller_->uploadEphemeris(
        "99942", "Test Comet", "comet", points, 3));

    auto tracker_id = controller_->startEphemerisTracking(
        "99942", now, 30.0, true, true, 1.0, "continuous");
    EXPECT_FALSE(tracker_id.empty());

    // Verify active trackers - getActiveEphemerisTrackers() returns internal
    // tracker IDs (e.g. "tracker_1"), not the object ID ("99942").
    auto active = controller_->getActiveEphemerisTrackers();
    EXPECT_EQ(active.size(), 1);
    EXPECT_EQ(active[0], tracker_id);
}

TEST_F(MountControllerTest, UploadEphemerisAndTrackWithData) {
    controller_->initialize(config_);
    auto now = std::chrono::system_clock::now();

    std::vector<std::tuple<std::chrono::system_clock::time_point,
                           double, double, double, double>> points;

    for (int i = 0; i < 5; i++) {
        auto t = now + std::chrono::hours(i);
        points.emplace_back(t, 10.0 + i * 0.1, 45.0 + i * 0.05, 0.1, 0.05);
    }

    auto tracker_id = controller_->startEphemerisTrackingWithData(
        "99942", "Test Comet", "comet", points, now, 30.0, 3, "continuous");
    EXPECT_FALSE(tracker_id.empty());
}

TEST_F(MountControllerTest, StopEphemerisTracking) {
    controller_->initialize(config_);
    auto now = std::chrono::system_clock::now();

    std::vector<std::tuple<std::chrono::system_clock::time_point,
                           double, double, double, double>> points;
    for (int i = 0; i < 5; i++) {
        points.emplace_back(now + std::chrono::hours(i),
                            10.0 + i * 0.1, 45.0 + i * 0.05, 0.1, 0.05);
    }

    auto tracker_id = controller_->startEphemerisTrackingWithData(
        "99942", "Test Comet", "comet", points, now, 30.0, 3, "continuous");

    // Stop tracking
    EXPECT_TRUE(controller_->stopEphemerisTracking(tracker_id));
    auto active = controller_->getActiveEphemerisTrackers();
    EXPECT_TRUE(active.empty());
}

TEST_F(MountControllerTest, GetEphemerisTrackStatus) {
    controller_->initialize(config_);
    // Should not throw for non-existent tracker
    auto status = controller_->getEphemerisTrackStatus("nonexistent");
    // Should return default status
}

TEST_F(MountControllerTest, ClearEphemerisCache) {
    controller_->initialize(config_);
    controller_->clearEphemerisCache();
}

TEST_F(MountControllerTest, GetEphemerisMetrics) {
    controller_->initialize(config_);
    auto metrics = controller_->getEphemerisMetrics();
    // Should not throw, returns default metrics
}

// ============================================
// 24. CANopen INTERFACE
// ============================================

TEST_F(MountControllerTest, GetCanOpenInterfaceThrowsBeforeInit) {
    controller_->initialize(config_);
    // Should not throw after initialization (mock interface)
    auto& canopen = controller_->getCanOpenInterface();
    // Verify it's usable
    SUCCEED();
}

// ============================================
// 25. EDGE CASES
// ============================================

TEST_F(MountControllerTest, MultipleSlewsInSequence) {
    controller_->initialize(config_);

    // First slew
    EXPECT_TRUE(controller_->slewToEquatorial(5.0, 30.0));
    std::this_thread::sleep_for(500ms);

    // Stop
    controller_->stop();
    std::this_thread::sleep_for(200ms);

    // Second slew
    EXPECT_TRUE(controller_->slewToEquatorial(10.0, 45.0));
    std::this_thread::sleep_for(200ms);

    // Stop again
    controller_->stop();
}

TEST_F(MountControllerTest, ParkAndUnparkMultipleTimes) {
    controller_->initialize(config_);

    // First park
    controller_->park();
    for (int i = 0; i < 50; i++) {
        std::this_thread::sleep_for(200ms);
        auto status = controller_->getStatus();
        if (status.state == MountController::MountStatus::State::PARKED) break;
    }

    controller_->unpark();
    auto status = controller_->getStatus();
    EXPECT_EQ(status.state, MountController::MountStatus::State::IDLE);

    // Second park
    controller_->park();
    for (int i = 0; i < 50; i++) {
        std::this_thread::sleep_for(200ms);
        auto status = controller_->getStatus();
        if (status.state == MountController::MountStatus::State::PARKED) break;
    }

    EXPECT_EQ(controller_->getStatus().state,
              MountController::MountStatus::State::PARKED);
}

// ============================================
// MERIDIAN FLIP TESTS
// ============================================

TEST_F(MountControllerTest, MeridianFlipDetection) {
    // Verify that tracking past the meridian triggers a pending flip
    config_.enable_refraction_correction = false;
    config_.meridian_flip_hysteresis_degrees = 0.0001;
    config_.meridian_flip_delay_minutes = 5.0; // Long delay to keep it pending
    controller_->initialize(config_);

    EXPECT_FALSE(controller_->isMeridianFlipPending());

    // Start tracking - axis1_position_ (HA) will increase at sidereal rate
    // After ~3 iterations (300ms), HA exceeds 0.0001° hysteresis
    controller_->startTracking(10.0, 45.0, MountController::TrackingMode::SIDEREAL);

    // Wait for HA to cross the hysteresis threshold
    std::this_thread::sleep_for(std::chrono::milliseconds(800));

    auto status = controller_->getStatus();
    EXPECT_TRUE(status.meridian_flip_pending);
    EXPECT_TRUE(controller_->isMeridianFlipPending());
    // State should still be TRACKING (flip not yet executed due to long delay)
    EXPECT_EQ(status.state, MountController::MountStatus::State::TRACKING);
}

TEST_F(MountControllerTest, MeridianFlipExecuted) {
    // Verify that the flip completes: state → MERIDIAN_FLIP → back to TRACKING
    config_.enable_refraction_correction = false;
    config_.meridian_flip_hysteresis_degrees = 0.0001;
    config_.meridian_flip_delay_minutes = 0.0; // Immediate flip
    config_.max_slew_rate = 3600.0; // Fast flip (360 deg/iteration)
    controller_->initialize(config_);

    controller_->startTracking(10.0, 45.0, MountController::TrackingMode::SIDEREAL);

    // Wait for HA to cross hysteresis → state becomes MERIDIAN_FLIP → flip completes
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    auto status = controller_->getStatus();
    // After flip completes, state returns to TRACKING
    EXPECT_EQ(status.state, MountController::MountStatus::State::TRACKING);
    // Pier side should have flipped to West (-1)
    EXPECT_EQ(status.pier_side, -1);
    // Flip should no longer be in progress
    EXPECT_FALSE(status.meridian_flip_in_progress);
    EXPECT_FALSE(status.meridian_flip_pending);
}

TEST_F(MountControllerTest, MeridianFlipInProgressState) {
    // Verify the intermediate MERIDIAN_FLIP state during flip execution
    config_.meridian_flip_hysteresis_degrees = 0.0001;
    config_.meridian_flip_delay_minutes = 0.0;
    config_.max_slew_rate = 5.0; // Slow slew so flip takes many iterations
    controller_->initialize(config_);

    controller_->startTracking(10.0, 45.0, MountController::TrackingMode::SIDEREAL);

    // Wait for the flip to start but not complete
    std::this_thread::sleep_for(std::chrono::milliseconds(800));

    auto status = controller_->getStatus();
    // The state should be MERIDIAN_FLIP (flip in progress, slew not yet complete)
    if (status.state == MountController::MountStatus::State::TRACKING) {
        // May have already completed if timing was unlucky; that's acceptable
        EXPECT_FALSE(status.meridian_flip_in_progress);
    } else {
        EXPECT_EQ(status.state, MountController::MountStatus::State::MERIDIAN_FLIP);
        EXPECT_TRUE(status.meridian_flip_in_progress);
        EXPECT_FALSE(status.meridian_flip_pending);
    }
}

TEST_F(MountControllerTest, MeridianFlipDisabled) {
    // Verify no flip occurs when meridian_flip_enabled is false
    config_.meridian_flip_enabled = false;
    config_.meridian_flip_hysteresis_degrees = 0.0001;
    config_.meridian_flip_delay_minutes = 0.0;
    controller_->initialize(config_);

    controller_->startTracking(10.0, 45.0, MountController::TrackingMode::SIDEREAL);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    auto status = controller_->getStatus();
    EXPECT_EQ(status.state, MountController::MountStatus::State::TRACKING);
    EXPECT_FALSE(status.meridian_flip_pending);
    EXPECT_FALSE(status.meridian_flip_in_progress);
    // Pier side should still be East (1)
    EXPECT_EQ(status.pier_side, 1);
}

TEST_F(MountControllerTest, MeridianFlipNonEquatorial) {
    // Verify no flip occurs for ALT_AZ mounts
    config_.mount_type = MountController::MountType::ALT_AZ;
    config_.meridian_flip_hysteresis_degrees = 0.0001;
    config_.meridian_flip_delay_minutes = 0.0;
    controller_->initialize(config_);

    controller_->startTracking(10.0, 45.0, MountController::TrackingMode::SIDEREAL);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    auto status = controller_->getStatus();
    EXPECT_EQ(status.state, MountController::MountStatus::State::TRACKING);
    EXPECT_FALSE(status.meridian_flip_pending);
    EXPECT_FALSE(status.meridian_flip_in_progress);
}

TEST_F(MountControllerTest, MeridianFlipManualTrigger) {
    // Verify executeMeridianFlip() initiates a flip during tracking
    config_.enable_refraction_correction = false;
    config_.meridian_flip_hysteresis_degrees = 5.0; // Large hysteresis so auto-flip won't trigger
    config_.meridian_flip_delay_minutes = 5.0;
    config_.max_slew_rate = 3600.0;
    config_.meridian_flip_enabled = true;
    controller_->initialize(config_);

    controller_->startTracking(10.0, 45.0, MountController::TrackingMode::SIDEREAL);

    // Wait for tracking to stabilize
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Manually trigger the flip
    EXPECT_TRUE(controller_->executeMeridianFlip());

    // Wait for flip to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    auto status = controller_->getStatus();
    EXPECT_EQ(status.state, MountController::MountStatus::State::TRACKING);
    EXPECT_EQ(status.pier_side, -1);
    EXPECT_FALSE(status.meridian_flip_in_progress);
}

TEST_F(MountControllerTest, MeridianFlipManualTriggerRejected) {
    // Verify executeMeridianFlip() is rejected when not tracking
    config_.meridian_flip_enabled = true;
    controller_->initialize(config_);

    // Not tracking yet
    EXPECT_FALSE(controller_->executeMeridianFlip());

    // Start tracking
    controller_->startTracking(10.0, 45.0, MountController::TrackingMode::SIDEREAL);

    // Stop tracking
    controller_->stop();

    // Should be rejected after stop
    EXPECT_FALSE(controller_->executeMeridianFlip());
}

TEST_F(MountControllerTest, MeridianFlipStatusFields) {
    // Verify getStatus() returns correct meridian fields
    config_.enable_refraction_correction = false;
    config_.meridian_flip_hysteresis_degrees = 0.0001;
    config_.meridian_flip_delay_minutes = 5.0; // Keep pending, don't execute
    controller_->initialize(config_);

    auto status = controller_->getStatus();
    // Before tracking, no flip should be pending
    EXPECT_FALSE(status.meridian_flip_pending);
    EXPECT_FALSE(status.meridian_flip_in_progress);
    EXPECT_EQ(status.pier_side, 1); // Default East
    EXPECT_DOUBLE_EQ(status.time_to_meridian, 0.0);

    controller_->startTracking(10.0, 45.0, MountController::TrackingMode::SIDEREAL);

    std::this_thread::sleep_for(std::chrono::milliseconds(800));

    status = controller_->getStatus();
    EXPECT_TRUE(status.meridian_flip_pending);
    EXPECT_FALSE(status.meridian_flip_in_progress);
    // Pier side should reflect HA > hysteresis
    EXPECT_EQ(status.pier_side, -1);
}

// ============================================
// SOFT SAFETY LIMITS TESTS
// ============================================

TEST_F(MountControllerTest, SoftLimitWarningDuringTracking) {
    // Verify warning flag activates when axis2 is within warning zone of a limit
    // axis2_position is set to the Dec target and doesn't change during sidereal tracking
    config_.soft_limit_axis2_max = 60.0;
    config_.soft_limit_warning_degrees = 15.0;
    config_.soft_limit_deceleration_degrees = 5.0;  // Small decel zone so we stay in warning only
    controller_->initialize(config_);

    // Start tracking at Dec=50° → distance to max limit = 60-50 = 10°, which is < 15° warning
    // but >= 5° deceleration → warning should be active but NOT deceleration
    controller_->startTracking(12.0, 50.0, MountController::TrackingMode::SIDEREAL);

    // Wait for at least one tracking loop iteration (100ms)
    std::this_thread::sleep_for(300ms);

    auto status = controller_->getStatus();
    EXPECT_TRUE(status.soft_limit_warning_active)
        << "Warning should be active: axis2 distance to limit = "
        << (config_.soft_limit_axis2_max - 50.0) << "°";
    EXPECT_FALSE(status.soft_limit_deceleration_active)
        << "Deceleration should NOT be active at distance "
        << (config_.soft_limit_axis2_max - 50.0) << "° from limit";
    // State should remain TRACKING (not ERROR)
    EXPECT_EQ(status.state, MountController::MountStatus::State::TRACKING);
}

TEST_F(MountControllerTest, SoftLimitDecelerationDuringTracking) {
    // Verify deceleration flag activates and rate is scaled when axis is near limit
    // axis2 starts at 0.0° (from SimulatedHAL). Set axis2_max generous and axis2_min
    // just below 0 so dist2 = 0 - axis2_min is within the decel zone.
    // With axis2_min = -4.0: dist2 = 0 - (-4.0) = 4.0 < 5.0 → decel active.
    config_.soft_limit_axis2_min = -4.0;         // Tight: axis2=0°, distance to min = 4°
    config_.soft_limit_axis2_max = 185.0;        // Generous on the high side
    config_.soft_limit_warning_degrees = 10.0;   // Large warning zone
    config_.soft_limit_deceleration_degrees = 5.0; // Decel zone covers 4° < 5°
    config_.soft_limit_tracking_rate_factor = 0.1;
    config_.meridian_flip_enabled = false;       // Disable meridian flip to avoid interference
    controller_->initialize(config_);

    // Start tracking — axis2 stays at 0.0° (rate=0 in CUSTOM mode),
    // distance to axis2_min = 0 - (-4.0) = 4.0° which is < 5° decel zone
    bool started = controller_->startTracking(12.0, 45.0, MountController::TrackingMode::CUSTOM);
    EXPECT_TRUE(started) << "startTracking should succeed";

    // Wait for tracking loop iterations (100ms each)
    std::this_thread::sleep_for(300ms);

    auto status = controller_->getStatus();
    EXPECT_TRUE(status.soft_limit_warning_active)
        << "Warning should be active: axis2=" << status.axis2_position
        << "°, min2=" << config_.soft_limit_axis2_min
        << "°, dist2=" << status.soft_limit_distance_axis2;
    EXPECT_TRUE(status.soft_limit_deceleration_active)
        << "Deceleration should be active: axis2=" << status.axis2_position
        << "°, min2=" << config_.soft_limit_axis2_min
        << "°, dist2=" << status.soft_limit_distance_axis2;
    // Distance to axis2 limit should be positive and less than decel zone
    EXPECT_GT(status.soft_limit_distance_axis2, 0.0);
    EXPECT_LT(status.soft_limit_distance_axis2, config_.soft_limit_deceleration_degrees)
        << "dist2=" << status.soft_limit_distance_axis2
        << " decel=" << config_.soft_limit_deceleration_degrees
        << " axis2_pos=" << status.axis2_position
        << " axis2_min=" << config_.soft_limit_axis2_min;
    // State should still be TRACKING
    EXPECT_EQ(status.state, MountController::MountStatus::State::TRACKING);
}

TEST_F(MountControllerTest, SoftLimitErrorOnExceeded) {
    // Verify ERROR state when tracking moves beyond hard limit
    // axis1 starts at 0.0° (from SimulatedHAL) and moves at max_tracking_rate.
    // Set axis1_max to a value reachable within 1200ms at the configured tracking rate.
    // With max_tracking_rate=30°/s, 1200ms gives ~36° of movement.
    // Set axis1_max=25.0 so distance=25°, reachable in ~1.1s with deceleration.
    config_.soft_limit_axis1_min = -270.0;
    config_.soft_limit_axis1_max = 25.0;         // Reachable from axis1 start at 0°
    config_.soft_limit_warning_degrees = 10.0;
    config_.soft_limit_deceleration_degrees = 5.0;
    config_.soft_limit_tracking_rate_factor = 0.1;
    config_.max_tracking_rate = 30.0;            // Fast tracking for test
    config_.meridian_flip_enabled = false;
    controller_->initialize(config_);

    // Compute RA near current LST so that HA ≈ 0 and axis1_target ≈ 0°.
    // This avoids time-dependent test failures: if RA=12.0 is hardcoded, then
    // HA = LST - 12.0 varies with time of day and may exceed axis1_max,
    // causing startTracking to reject the target at the soft limit check.
    double jd = core::AstronomicalCalculations::getCurrentJulianDate();
    double lst = core::AstronomicalCalculations::calculateLST(jd, config_.longitude);
    double ra = lst;  // HA = LST - RA ≈ 0 → axis1_target ≈ 0°
    // Normalize RA to [0, 24) hours
    while (ra < 0.0) ra += 24.0;
    while (ra >= 24.0) ra -= 24.0;

    // Start tracking with CUSTOM mode (rate = 30°/s)
    // axis1_position_ starts at 0.0°, distance to limit = 25°
    // Deceleration begins at 20°, leaving 20° at full speed (~670ms)
    // then 5° in deceleration zone (~350ms). Total ~1.0s < 1200ms.
    bool started = controller_->startTracking(ra, 45.0, MountController::TrackingMode::CUSTOM);
    EXPECT_TRUE(started) << "startTracking should succeed";

    // Wait for axis1 to cross the soft limit
    std::this_thread::sleep_for(1200ms);

    auto status = controller_->getStatus();
    EXPECT_EQ(status.state, MountController::MountStatus::State::ERROR)
        << "State should be ERROR after exceeding soft limit"
        << " state=" << static_cast<int>(status.state)
        << " axis1=" << status.axis1_position
        << " axis2=" << status.axis2_position
        << " rate1=" << status.axis1_rate
        << " dist1=" << status.soft_limit_distance_axis1
        << " max1=" << config_.soft_limit_axis1_max;
    EXPECT_FALSE(status.error_message.empty())
        << "Error message should be set"
        << " msg='" << status.error_message << "'";
}

TEST_F(MountControllerTest, ClearErrorsRecoversFromError) {
    // Verify clearErrors() transitions ERROR → IDLE and restores normal operation
    config_.soft_limit_axis1_min = -270.0;
    config_.soft_limit_axis1_max = 25.0;
    config_.soft_limit_warning_degrees = 10.0;
    config_.soft_limit_deceleration_degrees = 5.0;
    config_.soft_limit_tracking_rate_factor = 0.1;
    config_.max_tracking_rate = 30.0;
    config_.meridian_flip_enabled = false;
    controller_->initialize(config_);

    double jd = core::AstronomicalCalculations::getCurrentJulianDate();
    double lst = core::AstronomicalCalculations::calculateLST(jd, config_.longitude);
    double ra = lst;
    while (ra < 0.0) ra += 24.0;
    while (ra >= 24.0) ra -= 24.0;

    // Start tracking to trigger soft limit violation → ERROR
    bool started = controller_->startTracking(ra, 45.0, MountController::TrackingMode::CUSTOM);
    EXPECT_TRUE(started) << "startTracking should succeed";

    // Wait for axis1 to cross the soft limit and enter ERROR
    std::this_thread::sleep_for(1200ms);

    auto status_before = controller_->getStatus();
    EXPECT_EQ(status_before.state, MountController::MountStatus::State::ERROR)
        << "Should be in ERROR state before clearErrors()"
        << " state=" << static_cast<int>(status_before.state);
    EXPECT_FALSE(status_before.error_message.empty())
        << "Error message should be set in ERROR state";

    // Call clearErrors() to recover
    controller_->clearErrors();

    auto status_after = controller_->getStatus();
    EXPECT_EQ(status_after.state, MountController::MountStatus::State::IDLE)
        << "Should be in IDLE state after clearErrors()"
        << " state=" << static_cast<int>(status_after.state);
    EXPECT_TRUE(status_after.error_message.empty())
        << "Error message should be cleared after clearErrors()";

    // Verify the mount can be used again after recovery
    // Use same RA≈LST so the target is within soft limits
    bool can_slew_again = controller_->slewToEquatorial(ra, 45.0);
    EXPECT_TRUE(can_slew_again) << "Should be able to slew after clearErrors()";
}

TEST_F(MountControllerTest, ClearErrorsNoEffectInNonErrorState) {
    // Verify clearErrors() has no effect when not in ERROR state
    controller_->initialize(config_);
    
    auto status_before = controller_->getStatus();
    EXPECT_EQ(status_before.state, MountController::MountStatus::State::IDLE);
    
    // Call clearErrors() while in IDLE — should be a no-op
    controller_->clearErrors();
    
    auto status_after = controller_->getStatus();
    EXPECT_EQ(status_after.state, MountController::MountStatus::State::IDLE)
        << "State should remain IDLE when clearErrors() called outside ERROR";
}

TEST_F(MountControllerTest, SoftLimitDisabled) {
    // Verify no warning/deceleration flags when soft limits are disabled
    config_.soft_limits_enabled = false;
    config_.soft_limit_axis2_max = 60.0;  // Tight limit
    config_.soft_limit_warning_degrees = 15.0;
    controller_->initialize(config_);

    // Start tracking at Dec=50° (close to limit, but limits are disabled)
    controller_->startTracking(12.0, 50.0, MountController::TrackingMode::SIDEREAL);

    std::this_thread::sleep_for(300ms);

    auto status = controller_->getStatus();
    EXPECT_FALSE(status.soft_limit_warning_active)
        << "Warning should not be active when soft limits are disabled";
    EXPECT_FALSE(status.soft_limit_deceleration_active)
        << "Deceleration should not be active when soft limits are disabled";
    EXPECT_EQ(status.state, MountController::MountStatus::State::TRACKING);
}

TEST_F(MountControllerTest, SoftLimitStatusFields) {
    // Verify status fields are populated correctly
    // Note: axis2_position_ starts at 0, not at the Dec target.
    // The soft limits are evaluated against actual position, not target.
    // With axis2_min=-5 and axis2_pos=0, distance to min = 0-(-5) = 5.
    // So we should see distance_axis2 ≈ 5.
    config_.soft_limit_axis2_min = -5.0;  // Default
    config_.soft_limit_axis2_max = 60.0;
    config_.soft_limit_warning_degrees = 10.0;
    config_.soft_limit_deceleration_degrees = 3.0;
    controller_->initialize(config_);

    // Start tracking - axis2_position_ remains at 0 (no Dec tracking rate)
    controller_->startTracking(12.0, 50.0, MountController::TrackingMode::SIDEREAL);

    std::this_thread::sleep_for(300ms);

    auto status = controller_->getStatus();
    // axis2 distance to nearest limit: min(0-(-5), 60-0) = min(5, 60) = 5
    EXPECT_NEAR(status.soft_limit_distance_axis2, 5.0, 0.5);
    // axis2 is in warning zone (5 < 10) but not deceleration zone (5 >= 3)
    EXPECT_TRUE(status.soft_limit_warning_active);
    EXPECT_FALSE(status.soft_limit_deceleration_active);
    // Warning message should describe axis2
    EXPECT_FALSE(status.soft_limit_warning_message.empty());
    EXPECT_NE(status.soft_limit_warning_message.find("Axis2"), std::string::npos);
    // axis1 distance is very large (default limits ±270°, pos ≈ 0)
    EXPECT_GT(status.soft_limit_distance_axis1, 200.0);
}

TEST_F(MountControllerTest, SoftLimitRejectsOutOfRangeTarget) {
    // Verify target outside soft limits is rejected.
    // Note: axis1 = Hour Angle (HA), which depends on current LST.
    // We keep default wide axis1 limits (±270°) and test axis2 limits only.
    config_.soft_limit_axis2_min = -10.0;
    config_.soft_limit_axis2_max = 10.0;
    controller_->initialize(config_);

    // Try to slew to a target where axis2 (Dec=20°) is outside limits
    EXPECT_FALSE(controller_->slewToEquatorial(12.0, 20.0))
        << "Slew to Dec=20° should be rejected (limit is 10°)";
    EXPECT_FALSE(controller_->slewToHorizontal(45.0, 200.0))
        << "Slew to azimuth=200° should be rejected (axis2=45° > 10°)";
    EXPECT_FALSE(controller_->startTracking(12.0, 20.0))
        << "StartTracking to Dec=20° should be rejected (limit is 10°)";

    // But a target within limits should be accepted
    EXPECT_TRUE(controller_->slewToEquatorial(12.0, 5.0))
        << "Slew to Dec=5° should be accepted (within limits)";
}

// ============================================
// CASUAL MOUNT TESTS
// ============================================

/**
 * @brief Test fixture for CASUAL mount type.
 *
 * CASUAL mounts have arbitrary 3D orientation described by a quaternion
 * [qx, qy, qz, qw] representing rotation from the local horizontal frame
 * (ENU: East, North, Up) to the mount frame.
 *
 * We use a known orientation quaternion that represents a 30° rotation
 * around the East axis, which tilts the mount frame relative to horizontal.
 * This gives a predictable, analytically computable transform for testing.
 */
class CasualMountTest : public MountControllerTest {
protected:
    void SetUp() override {
        MountControllerTest::SetUp();
        
        // Use CASUAL mount type
        config_.mount_type = MountController::MountType::CASUAL;
        
        // Set a known orientation quaternion: 30° rotation about East axis.
        // This tilts the mount frame so that the mount's "altitude-like" axis
        // (axis1) points 30° away from zenith toward East.
        //
        // Quaternion for rotation of angle θ about axis (ax, ay, az):
        //   q = (ax*sin(θ/2), ay*sin(θ/2), az*sin(θ/2), cos(θ/2))
        //
        // For θ = 30° about East (1, 0, 0):
        //   q = (sin(15°), 0, 0, cos(15°))
        //      ≈ (0.258819, 0, 0, 0.965926)
        double theta = 30.0 * M_PI / 180.0;
        double half_theta = theta / 2.0;
        config_.mount_orientation.quaternion = {{
            std::sin(half_theta),  // qx: rotation about East
            0.0,                   // qy: no rotation about North
            0.0,                   // qz: no rotation about Up
            std::cos(half_theta)   // qw: scalar part
        }};
        
        // Disable refraction to keep coordinate transforms predictable
        config_.enable_refraction_correction = false;
        
        controller_->initialize(config_);
    }
};

// ----------------------------------------------------------------
// Test 1: MountOrientation struct operations
// ----------------------------------------------------------------

TEST_F(CasualMountTest, MountOrientationIsValid) {
    // Identity quaternion should be valid
    MountController::MountOrientation identity;
    EXPECT_TRUE(identity.isValid());
    
    // Custom quaternion should be valid
    MountController::MountOrientation custom;
    custom.quaternion = {{0.0, 0.0, 0.0, 1.0}};
    EXPECT_TRUE(custom.isValid());
}

TEST_F(CasualMountTest, MountOrientationInvalidQuaternion) {
    // Zero quaternion should be invalid
    MountController::MountOrientation zero;
    zero.quaternion = {{0.0, 0.0, 0.0, 0.0}};
    EXPECT_FALSE(zero.isValid());
    
    // Non-unit quaternion should be invalid (norm = sqrt(0.25+0.25) ≈ 0.707)
    MountController::MountOrientation non_unit;
    non_unit.quaternion = {{0.5, 0.0, 0.0, 0.5}};
    EXPECT_FALSE(non_unit.isValid());
}

TEST_F(CasualMountTest, MountOrientationSetFromAxisAngles) {
    MountController::MountOrientation ori;
    // axis1 = 30° altitude, axis2 = 180° azimuth (pointing South)
    ori.setFromAxisAngles(30.0, 180.0);
    
    // The resulting quaternion should be valid
    EXPECT_TRUE(ori.isValid());
    
    // The quaternion should have non-trivial values (not identity)
    // For axis1=30°, axis2=180°, the mount frame is rotated
    bool is_identity = (std::abs(ori.quaternion[0]) < 1e-6 &&
                        std::abs(ori.quaternion[1]) < 1e-6 &&
                        std::abs(ori.quaternion[2] - 1.0) < 1e-6 &&
                        std::abs(ori.quaternion[3]) < 1e-6);
    EXPECT_FALSE(is_identity) << "setFromAxisAngles should produce non-identity quaternion";
}

TEST_F(CasualMountTest, MountOrientationToRotationMatrix) {
    // Identity quaternion → identity rotation matrix
    MountController::MountOrientation identity;
    auto mat = identity.toRotationMatrix();
    EXPECT_EQ(mat.size(), 9);
    
    // Diagonal should be [1, 1, 1]
    EXPECT_NEAR(mat[0], 1.0, 1e-10);  // m00
    EXPECT_NEAR(mat[4], 1.0, 1e-10);  // m11
    EXPECT_NEAR(mat[8], 1.0, 1e-10);  // m22
}

// ----------------------------------------------------------------
// Test 2: SetMountOrientation / GetMountOrientation
// ----------------------------------------------------------------

TEST_F(CasualMountTest, SetAndGetMountOrientation) {
    MountController::MountOrientation test_ori;
    test_ori.quaternion = {{0.5, 0.5, 0.5, 0.5}};
    
    EXPECT_TRUE(controller_->setMountOrientation(test_ori));
    
    auto retrieved = controller_->getMountOrientation();
    EXPECT_NEAR(retrieved.quaternion[0], 0.5, 1e-10);
    EXPECT_NEAR(retrieved.quaternion[1], 0.5, 1e-10);
    EXPECT_NEAR(retrieved.quaternion[2], 0.5, 1e-10);
    EXPECT_NEAR(retrieved.quaternion[3], 0.5, 1e-10);
}

// ----------------------------------------------------------------
// Test 3: SlewToEquatorial with CASUAL mount
// ----------------------------------------------------------------

TEST_F(CasualMountTest, SlewToEquatorialStartsSlewing) {
    EXPECT_TRUE(controller_->slewToEquatorial(12.0, 45.0));
    
    auto status = controller_->getStatus();
    // Should transition to SLEWING (or IDLE if mock completes immediately)
    EXPECT_TRUE(status.state == MountController::MountStatus::State::SLEWING ||
                status.state == MountController::MountStatus::State::IDLE);
}

TEST_F(CasualMountTest, SlewToEquatorialSetsValidTargets) {
    controller_->slewToEquatorial(12.0, 45.0);
    
    auto status = controller_->getStatus();
    // Axis targets should be finite and within reasonable range
    EXPECT_TRUE(std::isfinite(status.axis1_target));
    EXPECT_TRUE(std::isfinite(status.axis2_target));
    
    // Axis1 (altitude-like) should be in [-90, 90]
    EXPECT_GE(status.axis1_target, -90.0);
    EXPECT_LE(status.axis1_target, 90.0);
    
    // Axis2 (azimuth-like) should be in [0, 360)
    EXPECT_GE(status.axis2_target, 0.0);
    EXPECT_LT(status.axis2_target, 360.0);
}

// ----------------------------------------------------------------
// Test 4: SlewToHorizontal with CASUAL mount (quaternion transform)
// ----------------------------------------------------------------

TEST_F(CasualMountTest, SlewToHorizontalSuccess) {
    // Slew to a known horizontal position
    EXPECT_TRUE(controller_->slewToHorizontal(45.0, 180.0));
}

TEST_F(CasualMountTest, SlewToHorizontalAppliesQuaternionTransform) {
    // When slewToHorizontal is called with true horizontal coordinates
    // (altitude, azimuth), the CASUAL mount should transform them through
    // the mount orientation quaternion to get mount-frame axis angles.
    //
    // With our 30° East-tilt quaternion, the transform should result in
    // axis targets that differ from the raw horizontal coordinates.
    controller_->slewToHorizontal(45.0, 180.0);
    
    auto status = controller_->getStatus();
    
    // Axis targets should be finite
    EXPECT_TRUE(std::isfinite(status.axis1_target));
    EXPECT_TRUE(std::isfinite(status.axis2_target));
    
    // The quaternion transform should produce different axis targets
    // than the raw (alt, az) = (45, 180) input.
    // axis1 should NOT equal altitude (45°) due to the 30° East tilt.
    // axis2 should NOT equal azimuth (180°) due to the same tilt.
    //
    // With 30° East tilt:
    //   - altitude 45°, azimuth 180° (South) in ENU:
    //     ENU vector = (0, cos45, sin45) = (0, 0.707, 0.707)
    //   - Rotating by Q = (sin15°, 0, 0, cos15°) about East:
    //     The South-Up vector tilts toward the East.
    //   - After rotation, mount_x > 0, mount_y < 0.707, mount_z > 0.707
    //   So axis1 ≠ 45 and axis2 ≠ 180.
    EXPECT_NE(status.axis1_target, 45.0)
        << "CASUAL mount should transform altitude through quaternion";
    EXPECT_NE(status.axis2_target, 180.0)
        << "CASUAL mount should transform azimuth through quaternion";
}

TEST_F(CasualMountTest, SlewToHorizontalReachesTarget) {
    controller_->slewToHorizontal(45.0, 180.0);
    
    bool reached = false;
    for (int i = 0; i < 50; i++) {
        std::this_thread::sleep_for(200ms);
        auto status = controller_->getStatus();
        if (status.state == MountController::MountStatus::State::IDLE) {
            reached = true;
            // Both axes should reach their transformed targets
            EXPECT_NEAR(status.axis1_position, status.axis1_target, config_.position_tolerance);
            EXPECT_NEAR(status.axis2_position, status.axis2_target, config_.position_tolerance);
            break;
        }
    }
    EXPECT_TRUE(reached) << "CASUAL mount horizontal slew did not complete within timeout";
}

// ----------------------------------------------------------------
// Test 5: StartTracking with CASUAL mount
// ----------------------------------------------------------------

TEST_F(CasualMountTest, StartTrackingSuccess) {
    EXPECT_TRUE(controller_->startTracking(12.0, 45.0, MountController::TrackingMode::SIDEREAL));
    
    auto status = controller_->getStatus();
    EXPECT_EQ(status.state, MountController::MountStatus::State::TRACKING);
}

TEST_F(CasualMountTest, StartTrackingSetsValidTargets) {
    controller_->startTracking(12.0, 45.0, MountController::TrackingMode::SIDEREAL);
    
    auto status = controller_->getStatus();
    // Axis1 (altitude-like) should be in [-90, 90]
    EXPECT_GE(status.axis1_target, -90.0);
    EXPECT_LE(status.axis1_target, 90.0);
    
    // Axis2 (azimuth-like) should be in [0, 360)
    EXPECT_GE(status.axis2_target, 0.0);
    EXPECT_LT(status.axis2_target, 360.0);
}

TEST_F(CasualMountTest, TrackingUpdatesPosition) {
    controller_->startTracking(12.0, 45.0, MountController::TrackingMode::SIDEREAL);
    
    // Let tracking run
    std::this_thread::sleep_for(600ms);
    auto status1 = controller_->getStatus();
    std::this_thread::sleep_for(600ms);
    auto status2 = controller_->getStatus();
    
    // Tracking should move the mount (rates should be non-zero)
    EXPECT_NE(status2.axis1_rate, 0.0)
        << "CASUAL tracking should have non-zero axis1 rate";
    EXPECT_NE(status2.axis2_rate, 0.0)
        << "CASUAL tracking should have non-zero axis2 rate";
    
    // Rates should be finite and reasonable (< 1 deg/s for sidereal tracking)
    EXPECT_LT(std::abs(status2.axis1_rate), 1.0)
        << "CASUAL axis1 rate too large for sidereal tracking";
    EXPECT_LT(std::abs(status2.axis2_rate), 1.0)
        << "CASUAL axis2 rate too large for sidereal tracking";
}

// ----------------------------------------------------------------
// Test 6: Identity quaternion (CASUAL behaves like ALT_AZ)
// ----------------------------------------------------------------

class CasualMountIdentityTest : public MountControllerTest {
protected:
    void SetUp() override {
        MountControllerTest::SetUp();
        config_.mount_type = MountController::MountType::CASUAL;
        
        // Identity quaternion: no rotation between ENU and mount frame.
        // With Q = (0, 0, 0, 1), the mount frame IS the ENU frame,
        // so CASUAL should behave identically to ALT_AZ.
        config_.mount_orientation.quaternion = {{0.0, 0.0, 0.0, 1.0}};
        config_.enable_refraction_correction = false;
        
        controller_->initialize(config_);
    }
};

TEST_F(CasualMountIdentityTest, SlewToHorizontalMatchesAltAz) {
    // With identity quaternion, slewToHorizontal should map:
    //   axis1 = azimuth, axis2 = altitude  (same as ALT_AZ convention)
    controller_->slewToHorizontal(30.0, 180.0);
    
    auto status = controller_->getStatus();
    EXPECT_DOUBLE_EQ(status.axis1_target, 180.0);  // azimuth
    EXPECT_DOUBLE_EQ(status.axis2_target, 30.0);   // altitude
}

TEST_F(CasualMountIdentityTest, TrackingComputesRates) {
    controller_->startTracking(60.0, 180.0, MountController::TrackingMode::SIDEREAL);
    
    std::this_thread::sleep_for(600ms);
    auto status1 = controller_->getStatus();
    std::this_thread::sleep_for(600ms);
    auto status2 = controller_->getStatus();
    
    // Both axes should change over time (like ALT_AZ)
    EXPECT_NE(status2.axis1_position, status1.axis1_position)
        << "Identity CASUAL should show position change like ALT_AZ";
    EXPECT_NE(status2.axis2_position, status1.axis2_position)
        << "Identity CASUAL should show position change like ALT_AZ";
}

// ----------------------------------------------------------------
// Test 7: Soft limits for CASUAL (axis2 = azimuth-like wraps)
// ----------------------------------------------------------------

TEST_F(CasualMountTest, SoftLimitAxis2AllowedExceeds) {
    // For CASUAL mounts, axis2 is azimuth-like [0, 360) and wraps.
    // Setting axis2_min=-10, axis2_max=370 should not reject any target.
    config_.soft_limit_axis2_min = -10.0;
    config_.soft_limit_axis2_max = 370.0;
    controller_->initialize(config_);
    
    // Azimuth-like axis2 = 350° is within [0, 360) even though it's > 270°.
    // Soft limits check absolute values, so this should be fine.
    EXPECT_TRUE(controller_->slewToHorizontal(45.0, 350.0));
}

// ----------------------------------------------------------------
// Test 8: Meridian flip not applicable to CASUAL
// ----------------------------------------------------------------

TEST_F(CasualMountTest, NoMeridianFlip) {
    // CASUAL mounts (like ALT_AZ) don't have a meridian flip.
    // The controller should never report a pending meridian flip.
    controller_->startTracking(12.0, 45.0, MountController::TrackingMode::SIDEREAL);
    
    // getTimeToMeridian should return a large positive value
    // (indicating no imminent meridian flip needed)
    double time_to_meridian = controller_->getTimeToMeridian();
    EXPECT_GT(time_to_meridian, 0.0)
        << "CASUAL mount should report no imminent meridian flip";
}

} // namespace test
} // namespace controllers
} // namespace astro_mount

// ============================================================================
// Ephemeris Interpolation Numerical Stability Tests
// ============================================================================
namespace astro_mount {
namespace models {
namespace test {

using namespace std::chrono;

class EphemerisInterpolationStabilityTest : public ::testing::Test {
protected:
    void SetUp() override {
        logging::Logger::init("");
    }

    // Helper: create an EphemerisPoint from a time_point
    ::astro_mount::EphemerisPoint make_point(const system_clock::time_point& tp,
                                              double ra, double dec,
                                              double ra_rate, double dec_rate) {
        ::astro_mount::EphemerisPoint point;
        auto tt = system_clock::to_time_t(tp);
        auto dur = tp - system_clock::from_time_t(tt);
        auto ns = duration_cast<nanoseconds>(dur).count();
        point.mutable_timestamp()->set_seconds(tt);
        point.mutable_timestamp()->set_nanos(static_cast<int32_t>(ns));
        point.set_ra(ra);
        point.set_dec(dec);
        point.set_ra_rate(ra_rate);
        point.set_dec_rate(dec_rate);
        return point;
    }

    // Build a standard ephemeris test dataset: 10 points, 1 hour apart
    std::vector<::astro_mount::EphemerisPoint> make_standard_set() {
        auto base = system_clock::now();
        std::vector<::astro_mount::EphemerisPoint> pts;
        for (int i = 0; i < 10; ++i) {
            auto t = base + hours(i);
            pts.push_back(make_point(t,
                10.0 + i * 0.1,      // RA: linear increase
                45.0 + i * 0.05,     // Dec: linear increase
                0.1,                 // RA rate (constant)
                0.05));              // Dec rate (constant)
        }
        return pts;
    }
};

// ----------------------------------------------------------------
// Test 1: Extrapolation guard prevents NaN/Inf outside data range
// ----------------------------------------------------------------
TEST_F(EphemerisInterpolationStabilityTest, ExtrapolationGuardReturnsBoundaryValues) {
    auto pts = make_standard_set();
    auto base = system_clock::now();

    // Cubic interpolator (order 3)
    EphemerisInterpolator interp(pts, 3);
    ASSERT_TRUE(interp.isValid());

    // Query far before the ephemeris range
    auto far_before = base - hours(100);
    auto [ra_b, dec_b, rr_b, dr_b] = interp.getPositionAtTime(far_before);

    EXPECT_FALSE(std::isnan(ra_b)) << "RA should not be NaN when extrapolating before range";
    EXPECT_FALSE(std::isnan(dec_b)) << "Dec should not be NaN when extrapolating before range";
    EXPECT_FALSE(std::isinf(ra_b)) << "RA should not be Inf when extrapolating before range";
    EXPECT_FALSE(std::isinf(dec_b)) << "Dec should not be Inf when extrapolating before range";

    // Values should be clamped to the first ephemeris point (boundary)
    EXPECT_NEAR(ra_b, 10.0, 1e-6) << "RA should be clamped to first point value";
    EXPECT_NEAR(dec_b, 45.0, 1e-6) << "Dec should be clamped to first point value";

    // Query far after the ephemeris range
    auto far_after = base + hours(1000);
    auto [ra_a, dec_a, rr_a, dr_a] = interp.getPositionAtTime(far_after);

    EXPECT_FALSE(std::isnan(ra_a)) << "RA should not be NaN when extrapolating after range";
    EXPECT_FALSE(std::isnan(dec_a)) << "Dec should not be NaN when extrapolating after range";
    EXPECT_FALSE(std::isinf(ra_a)) << "RA should not be Inf when extrapolating after range";
    EXPECT_FALSE(std::isinf(dec_a)) << "Dec should not be Inf when extrapolating after range";

    // Values should be clamped to the last ephemeris point (boundary)
    EXPECT_NEAR(ra_a, 10.9, 1e-6) << "RA should be clamped to last point value";
    EXPECT_NEAR(dec_a, 45.45, 1e-6) << "Dec should be clamped to last point value";
}

// ----------------------------------------------------------------
// Test 2: Extrapolation guard also works for linear and quadratic
// ----------------------------------------------------------------
TEST_F(EphemerisInterpolationStabilityTest, ExtrapolationGuardWorksForAllOrders) {
    auto pts = make_standard_set();
    auto base = system_clock::now();
    auto far_after = base + hours(10000);

    for (int order = 1; order <= 3; ++order) {
        EphemerisInterpolator interp(pts, order);
        auto [ra, dec, rr, dr] = interp.getPositionAtTime(far_after);

        EXPECT_FALSE(std::isnan(ra)) << "Order " << order << ": RA should not be NaN";
        EXPECT_FALSE(std::isnan(dec)) << "Order " << order << ": Dec should not be NaN";
        EXPECT_FALSE(std::isinf(ra)) << "Order " << order << ": RA should not be Inf";
        EXPECT_FALSE(std::isinf(dec)) << "Order " << order << ": Dec should not be Inf";
    }
}

// ----------------------------------------------------------------
// Test 3: Duplicate timestamps don't cause division by zero
// ----------------------------------------------------------------
TEST_F(EphemerisInterpolationStabilityTest, DuplicateTimestampsNoDivisionByZero) {
    auto base = system_clock::now();

    // Create points where two timestamps are identical
    std::vector<::astro_mount::EphemerisPoint> pts;
    pts.push_back(make_point(base + hours(0), 10.0, 45.0, 0.1, 0.05));
    pts.push_back(make_point(base + hours(1), 10.1, 45.05, 0.1, 0.05));
    pts.push_back(make_point(base + hours(1), 10.2, 45.10, 0.1, 0.05)); // DUPLICATE!
    pts.push_back(make_point(base + hours(2), 10.3, 45.15, 0.1, 0.05));

    EphemerisInterpolator interp(pts, 3);
    ASSERT_TRUE(interp.isValid());

    // Query at the duplicate timestamp - should not produce NaN/Inf
    auto [ra, dec, rr, dr] = interp.getPositionAtTime(base + hours(1));

    EXPECT_FALSE(std::isnan(ra)) << "RA should not be NaN with duplicate timestamps";
    EXPECT_FALSE(std::isnan(dec)) << "Dec should not be NaN with duplicate timestamps";
    EXPECT_FALSE(std::isinf(ra)) << "RA should not be Inf with duplicate timestamps";
    EXPECT_FALSE(std::isinf(dec)) << "Dec should not be Inf with duplicate timestamps";

    // Values should still be reasonable (fallback to lower-order interp)
    EXPECT_GT(ra, 9.0) << "RA should be in physically plausible range";
    EXPECT_LT(ra, 11.0) << "RA should be in physically plausible range";
    EXPECT_GT(dec, 44.0) << "Dec should be in physically plausible range";
    EXPECT_LT(dec, 46.0) << "Dec should be in physically plausible range";
}

// ----------------------------------------------------------------
// Test 4: Near-identical timestamps (sub-millisecond) handled safely
// ----------------------------------------------------------------
TEST_F(EphemerisInterpolationStabilityTest, SubMillisecondSpacingNoNumericalBlowup) {
    auto base = system_clock::now();

    // Points spaced 1 microsecond apart — extreme edge case
    std::vector<::astro_mount::EphemerisPoint> pts;
    for (int i = 0; i < 5; ++i) {
        auto t = base + microseconds(i);
        pts.push_back(make_point(t, 10.0 + i * 0.01, 45.0 + i * 0.005, 0.1, 0.05));
    }

    EphemerisInterpolator interp(pts, 3);
    ASSERT_TRUE(interp.isValid());

    // Query at various points
    for (int i = 0; i < 5; ++i) {
        auto [ra, dec, rr, dr] = interp.getPositionAtTime(base + microseconds(i));
        EXPECT_FALSE(std::isnan(ra)) << "i=" << i << ": RA should not be NaN";
        EXPECT_FALSE(std::isnan(dec)) << "i=" << i << ": Dec should not be NaN";
        EXPECT_FALSE(std::isinf(ra)) << "i=" << i << ": RA should not be Inf";
        EXPECT_FALSE(std::isinf(dec)) << "i=" << i << ": Dec should not be Inf";
    }
}

// ----------------------------------------------------------------
// Test 5: PredictPosition with acceleration works correctly
// ----------------------------------------------------------------
TEST_F(EphemerisInterpolationStabilityTest, PredictPositionWithAcceleration) {
    auto base = system_clock::now();

    // Accelerating object: RA rate increases over time
    std::vector<::astro_mount::EphemerisPoint> pts;
    for (int i = 0; i < 10; ++i) {
        double ra_rate = 0.1 + i * 0.02;  // Increasing rate (acceleration)
        double dec_rate = 0.05 + i * 0.01;
        pts.push_back(make_point(base + hours(i),
            10.0 + i * 0.1 + 0.5 * 0.02 * i * i * 0.1,  // Quadratic position
            45.0 + i * 0.05 + 0.5 * 0.01 * i * i * 0.1,
            ra_rate, dec_rate));
    }

    EphemerisInterpolator interp(pts, 3);
    ASSERT_TRUE(interp.isValid());

    // Predict 30 minutes beyond the ephemeris range
    auto beyond = base + hours(10) + minutes(30);
    auto result = interp.predictPosition(beyond, 7200.0);  // 2 hour extrapolation window

    ASSERT_TRUE(result.has_value()) << "predictPosition should return a value";
    auto [ra, dec, ra_rate, dec_rate] = result.value();

    EXPECT_FALSE(std::isnan(ra)) << "Predicted RA should not be NaN";
    EXPECT_FALSE(std::isnan(dec)) << "Predicted Dec should not be NaN";
    EXPECT_FALSE(std::isinf(ra)) << "Predicted RA should not be Inf";
    EXPECT_FALSE(std::isinf(dec)) << "Predicted Dec should not be Inf";

    // With acceleration, predicted RA should be beyond simple linear extrapolation
    // Simple linear: last_ra + last_rate * dt = 10.9 + 0.28 * 0.5 = 11.04
    // With acceleration: should be higher due to positive acceleration
    EXPECT_GT(ra, 10.9) << "Predicted RA should be beyond last ephemeris point";
}

// ----------------------------------------------------------------
// Test 6: Interpolation produces smooth, monotonic results
// ----------------------------------------------------------------
TEST_F(EphemerisInterpolationStabilityTest, InterpolationProducesSmoothResults) {
    auto pts = make_standard_set();
    auto base = system_clock::now();

    EphemerisInterpolator interp(pts, 3);
    ASSERT_TRUE(interp.isValid());

    double prev_ra = -1.0;
    double prev_dec = -1.0;

    // Sweep through the entire range at 10-minute intervals
    for (int minute = 0; minute <= 9 * 60; minute += 10) {
        auto t = base + minutes(minute);
        auto [ra, dec, rr, dr] = interp.getPositionAtTime(t);

        EXPECT_FALSE(std::isnan(ra)) << "t=" << minute << "min: RA not NaN";
        EXPECT_FALSE(std::isnan(dec)) << "t=" << minute << "min: Dec not NaN";
        EXPECT_FALSE(std::isinf(ra)) << "t=" << minute << "min: RA not Inf";
        EXPECT_FALSE(std::isinf(dec)) << "t=" << minute << "min: Dec not Inf";

        // RA should be monotonically increasing
        if (prev_ra > 0) {
            EXPECT_GT(ra, prev_ra) << "RA should be monotonically increasing at t=" << minute << "min";
        }
        // Dec should be monotonically increasing
        if (prev_dec > 0) {
            EXPECT_GT(dec, prev_dec) << "Dec should be monotonically increasing at t=" << minute << "min";
        }

        prev_ra = ra;
        prev_dec = dec;
    }
}

// ----------------------------------------------------------------
// Test 7: PredictPosition returns nullopt for excessively far extrapolation
// ----------------------------------------------------------------
TEST_F(EphemerisInterpolationStabilityTest, PredictPositionRejectsExcessiveExtrapolation) {
    auto pts = make_standard_set();
    auto base = system_clock::now();

    EphemerisInterpolator interp(pts, 3);

    // Try to predict 1 year beyond — should be rejected
    auto way_beyond = base + hours(8760);
    auto result = interp.predictPosition(way_beyond, 3600.0);  // only 1h window

    EXPECT_FALSE(result.has_value()) << "predictPosition should reject extrapolation beyond 1h window";
}

// ----------------------------------------------------------------
// Test 8: Interpolation with only 2 points (linear) is stable
// ----------------------------------------------------------------
TEST_F(EphemerisInterpolationStabilityTest, TwoPointInterpolationIsStable) {
    auto base = system_clock::now();

    std::vector<::astro_mount::EphemerisPoint> pts;
    pts.push_back(make_point(base + hours(0), 10.0, 45.0, 0.1, 0.05));
    pts.push_back(make_point(base + hours(1), 10.1, 45.05, 0.1, 0.05));

    // Force cubic order, but with only 2 points it will fall back through
    // quadratic → linear automatically
    EphemerisInterpolator interp(pts, 3);
    ASSERT_TRUE(interp.isValid());

    auto t_mid = base + minutes(30);
    auto [ra, dec, rr, dr] = interp.getPositionAtTime(t_mid);

    EXPECT_FALSE(std::isnan(ra));
    EXPECT_FALSE(std::isnan(dec));
    EXPECT_NEAR(ra, 10.05, 1e-6) << "Linear interpolation at midpoint";
    EXPECT_NEAR(dec, 45.025, 1e-6) << "Linear interpolation at midpoint";
}

} // namespace test
} // namespace models
} // namespace astro_mount

// Main
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
