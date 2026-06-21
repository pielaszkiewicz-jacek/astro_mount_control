#include "controllers/mount_controller.h"
#include "controllers/derotator_controller.h"
#include "controllers/icanopen_interface.h"
#include "controllers/canopen_factory.h"
#include "core/astronomical_calculations.h"
#include "hal/hal_interface.h"
#include "hal/hal_config.h"
#include "hal/safety_monitor.h"
#include "hal/sensor_interface.h"
#include "models/ephemeris_tracker.h"
#include "models/tpoint_model.h"
#include "proto/mount_controller.pb.h"
#include "logging/logger.h"
#include <google/protobuf/util/time_util.h>
#include <chrono>
#include <ctime>
#include <thread>
#include <fstream>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <cmath>
#include <atomic>
#include <Eigen/Dense>
#include <shared_mutex>
#include "hal/gamepad_hal/gamepad_input_evdev.h"

namespace astro_mount {
namespace controllers {

using json = nlohmann::json;
using google::protobuf::util::TimeUtil;

/// Lightweight 4-state Kalman filter for smoothing axis positions in the tracking loop.
/// State vector: [pos1, pos2, rate1, rate2]^T
///   - pos1: axis1 position (degrees)
///   - pos2: axis2 position (degrees)
///   - rate1: axis1 velocity (deg/s)
///   - rate2: axis2 velocity (deg/s)
///
/// Measurement: [pos1, pos2]^T  — the accumulated position from the tracking loop.
/// The filter blends the kinematic prediction (pos += rate * dt) with the measured
/// position, attenuating encoder noise while maintaining low-latency response.
struct PositionKalmanFilter {
    Eigen::Vector4d x;   // State: [pos1, pos2, rate1, rate2]
    Eigen::Matrix4d P;   // Estimate covariance
    Eigen::Matrix4d Q;   // Process noise covariance
    Eigen::Matrix2d R;   // Measurement noise covariance
    bool initialized{false};

    PositionKalmanFilter() {
        x.setZero();
        P.setIdentity();
        Q.setIdentity();
        R.setIdentity();
    }

    /// Initialize with first position measurement and zero rates.
    void init(double pos1, double pos2, double process_noise, double measurement_noise) {
        x(0) = pos1;
        x(1) = pos2;
        x(2) = 0.0;  // rate1 — will be set by tracking rate
        x(3) = 0.0;  // rate2

        P.setIdentity();
        P *= 10.0;  // High initial uncertainty

        // Guard against NaN or near-zero noise values that would cause
        // the innovation covariance S = H*P*H^T + R to become singular,
        // producing NaN in the Kalman gain computation.
        double pn = std::isfinite(process_noise) ? std::abs(process_noise) : 0.001;
        double mn = std::isfinite(measurement_noise) ? std::abs(measurement_noise) : 0.001;
        if (pn < 1e-12) pn = 0.001;
        if (mn < 1e-12) mn = 0.001;

        // Process noise: position noise scales with dt², rate noise is constant
        // These are the BASE per-second values; predict() scales by dt
        double pq = pn * pn;
        Q(0,0) = pq;  Q(1,1) = pq;
        Q(2,2) = pq;  Q(3,3) = pq;

        // Measurement noise: uncertainty in the accumulated position
        double mr = mn * mn;
        R(0,0) = mr;  R(1,1) = mr;

        initialized = true;
    }

    /// Predict state forward by dt seconds using the state's own velocity estimates.
    void predict(double dt) {
        if (!initialized || dt <= 0.0) return;

        // State transition: pos += rate * dt, rate unchanged
        Eigen::Matrix4d F = Eigen::Matrix4d::Identity();
        F(0,2) = dt;
        F(1,3) = dt;

        // Predict state
        x = F * x;

        // Predict covariance: P = F * P * F^T + Q_scaled
        Eigen::Matrix4d Q_scaled = Q;
        Q_scaled(0,0) *= dt * dt;  Q_scaled(1,1) *= dt * dt;
        Q_scaled(2,2) *= dt;       Q_scaled(3,3) *= dt;
        P = F * P * F.transpose() + Q_scaled;
    }

    /// Update state with a position measurement [pos1, pos2].
    void update(double pos1, double pos2) {
        if (!initialized) return;

        // Measurement vector
        Eigen::Vector2d z(pos1, pos2);

        // Measurement matrix H: maps state to measurement space
        Eigen::Matrix<double, 2, 4> H;
        H << 1, 0, 0, 0,
             0, 1, 0, 0;

        // Innovation (residual): y = z - H * x
        Eigen::Vector2d y = z - H * x;

        // Innovation covariance: S = H * P * H^T + R
        Eigen::Matrix2d S = H * P * H.transpose() + R;

        // Kalman gain: K = P * H^T * S^{-1}
        Eigen::Matrix<double, 4, 2> K = P * H.transpose() * S.inverse();

        // Update state: x = x + K * y
        x = x + K * y;

        // Update covariance using Joseph stabilized form:
        //   P = (I - K*H) * P * (I - K*H)^T + K * R * K^T
        //
        // This is analytically equivalent to the standard (I-KH)P update when K is the
        // optimal Kalman gain, but remains symmetric positive-semidefinite even when K is
        // suboptimal (e.g., numerical rounding, non-linear models). The standard form
        // (I-KH)P can produce asymmetric or negative eigenvalues from rounding errors
        // that destroy the positive-semidefinite constraint, leading to filter divergence.
        // The Joseph form adds the K*R*K^T term which guarantees P remains symmetric PSD.
        Eigen::Matrix4d I_KH = Eigen::Matrix4d::Identity() - K * H;
        P = I_KH * P * I_KH.transpose() + K * R * K.transpose();
    }

    /// Override the internal rate estimates with externally computed tracking rates.
    /// This is essential because the tracking loop computes accurate astronomical rates
    /// (sidereal/solar/lunar for EQUATORIAL, position-dependent for ALT-AZ/CASUAL),
    /// while the KF's own rate estimates (derived purely from position changes via
    /// Kalman gain) would lag behind, degrading prediction accuracy.
    /// Call this before predict() to inject the correct velocity into the state.
    void setRates(double rate1, double rate2) {
        if (!initialized) return;
        x(2) = rate1;
        x(3) = rate2;
    }

    double pos1() const { return x(0); }
    double pos2() const { return x(1); }
    double rate1() const { return x(2); }
    double rate2() const { return x(3); }
};

// ============================================================
// MountOrientation member function implementations
// ============================================================

bool MountController::MountOrientation::isValid() const {
    double sum_sq = quaternion[0] * quaternion[0] +
                    quaternion[1] * quaternion[1] +
                    quaternion[2] * quaternion[2] +
                    quaternion[3] * quaternion[3];
    return std::abs(sum_sq - 1.0) < 1e-6;
}

void MountController::MountOrientation::setFromAxisAngles(double axis1_altitude, double axis1_azimuth) {
    // Build orientation quaternion Q such that Q rotates from ENU {north, east, up}
    // to mount frame. Axis1 (altitude-like) points at (altitude, azimuth) in ENU.
    // Q * ENU_axis1 * Q_conj = (0, 0, 1) (mount zenith)
    //
    // ENU axis1 direction: (cos(alt)*cos(az), cos(alt)*sin(az), sin(alt))
    // Q_conj = quaternion_from_a_to_b((0,0,1), ENU_axis1)
    // Q = conjugate(Q_conj)
    
    double alt_rad = axis1_altitude * M_PI / 180.0;
    double az_rad = axis1_azimuth * M_PI / 180.0;
    
    double north = std::cos(alt_rad) * std::cos(az_rad);
    double east  = std::cos(alt_rad) * std::sin(az_rad);
    double up    = std::sin(alt_rad);
    
    // Q_conj: rotate (0,0,1) to ENU_axis1
    // v = (0,0,1) × (north, east, up) = (-east, north, 0)
    // s = 1 + (0,0,1)·(north, east, up) = 1 + up
    double vx = -east;
    double vy =  north;
    double vz = 0.0;
    double s  = 1.0 + up;
    
    double norm = std::sqrt(vx*vx + vy*vy + vz*vz + s*s);
    if (norm > 0.0) {
        // Q = conjugate(Q_conj): negate vector part, keep scalar
        quaternion[0] = -vx / norm;   // qx
        quaternion[1] = -vy / norm;   // qy
        quaternion[2] = -vz / norm;   // qz
        quaternion[3] =  s / norm;    // qw
    } else {
        // Zero rotation → identity
        quaternion = {{0.0, 0.0, 0.0, 1.0}};
    }
}

std::array<double, 9> MountController::MountOrientation::toRotationMatrix() const {
    // Convert unit quaternion [qx, qy, qz, qw] to 3×3 rotation matrix (row-major)
    double qx = quaternion[0];
    double qy = quaternion[1];
    double qz = quaternion[2];
    double qw = quaternion[3];
    
    double qx2 = qx * qx;
    double qy2 = qy * qy;
    double qz2 = qz * qz;
    
    return {{
        1.0 - 2.0 * (qy2 + qz2),  2.0 * (qx*qy - qz*qw),  2.0 * (qx*qz + qy*qw),
        2.0 * (qx*qy + qz*qw),    1.0 - 2.0 * (qx2 + qz2),  2.0 * (qy*qz - qx*qw),
        2.0 * (qx*qz - qy*qw),    2.0 * (qy*qz + qx*qw),  1.0 - 2.0 * (qx2 + qy2)
    }};
}

class MountController::Impl {
public:
    Impl() : state_{MountStatus::State::UNINITIALIZED},
             axis1_position_(0.0),
             axis2_position_(0.0),
             axis1_target_(0.0),
             axis2_target_(0.0),
             axis1_rate_(0.0),
             axis2_rate_(0.0),
             encoders_active_(false),
             guider_active_(false),
             tpoint_calibrated_(false),
             bootstrap_calibrated_(false),
             tracking_error_ra_(0.0),
             tracking_error_dec_(0.0),
             state_mutex_(new std::shared_mutex()),
             rate_mutex_(new std::shared_mutex()),
             env_mutex_(new std::mutex()),
             thread_mutex_(new std::mutex()),
             encoder_absolute_(false),
             bootstrap_mode_{BootstrapMode::BOOTSTRAP_MANUAL},
             env_temperature_(15.0),
             env_pressure_(1013.25),
             env_humidity_(0.5),
             last_field_rotation_time_(0.0),
             astro_calc_(std::make_unique<core::AstronomicalCalculations>()),
             tpoint_model_(std::make_unique<models::TPointModel>()),
             flip_start_time_{},
             error_message_(),
             config_file_path_() {}
    
    Impl(std::unique_ptr<hal::HALInterface> hal)
        : Impl() {
        hal_interface_ = std::move(hal);
    }
    
    ~Impl() {
        stopGamepad();  // must join gamepad thread before destroying members
    }
    
    bool initialize(const ControllerConfig& config) {
        config_ = config;
        hal_config_ = config.hal_config;
        state_ = MountStatus::State::IDLE;
        notifyStatusChanged();  // UNINITIALIZED → IDLE

        // --- Encoder type logic (plan §8.1) ---
        // For absolute encoders, read the actual physical position at startup.
        // For incremental encoders, start from (0,0) — the CANopen servo
        // also initialises to zero on power-up.
        if (config_.encoders_absolute) {
            if (hal_axis1_encoder_ && hal_axis2_encoder_) {
                auto enc1 = hal_axis1_encoder_->read();
                auto enc2 = hal_axis2_encoder_->read();
                axis1_position_ = enc1.position_deg;
                axis2_position_ = enc2.position_deg;
                encoder_absolute_ = true;
                MOUNT_LOG_INFO("Absolute encoders: init pos=({:.4f}°, {:.4f}°)",
                              axis1_position_, axis2_position_);
            } else {
                MOUNT_LOG_WARN("encoders_absolute=true but no encoder reader available; "
                              "falling back to (0,0)");
                axis1_position_ = 0.0;
                axis2_position_ = 0.0;
            }
        } else {
            // Incremental encoders — start from configured park position.
            // Using the park position as reference gives a sensible default
            // (e.g. NCP for equatorial: HA=0°, Dec=90°) that is immediately
            // visible in the status UI, rather than forcing the operator to
            // issue a move command just to see non-zero positions.
            // The bootstrap calibration (Wahba/SVD) will later determine the
            // rotation offset between the mount frame and the true horizontal frame,
            // which implicitly absorbs the unknown encoder zero offset.
            MOUNT_LOG_INFO("Incremental encoders: starting from park position ({:.4f}°, {:.4f}°)",
                          config_.park_position_axis1, config_.park_position_axis2);
            axis1_position_ = config_.park_position_axis1;
            axis2_position_ = config_.park_position_axis2;
        }

        // Initialize position Kalman filter with config noise parameters.
        // This filter smooths axis positions in the tracking loop by blending
        // kinematic prediction (pos += rate * dt) with measured position updates.
        position_kf_ = std::make_unique<PositionKalmanFilter>();
        position_kf_->init(axis1_position_, axis2_position_,
                           config_.process_noise, config_.measurement_noise);
        
        // Initialize HAL interface if available
        if (hal_interface_) {
            // HALType enum describes the hardware interface (CANopen, Serial, etc.),
            // not the mount geometry (equatorial vs alt-az). Both mount types
            // can use CANopen hardware, so both map to CANOPEN here.
            // IMPORTANT: Do NOT overwrite hal_config_ — it was loaded from
            // config.hal_config at line 271 and contains all fields from the
            // JSON config file (canopen.library, canopen.bitrate, canopen.pdo_config_enabled, etc.).
            // Creating a new HALConfig and assigning would lose all those values.
            hal_config_.type = astro_mount::hal::HALType::CANOPEN;
            hal_config_.name = "MountController_HAL";
            if (!hal_interface_->initialize(hal_config_)) {
                MOUNT_LOG_ERROR("Failed to initialize HAL interface");
                return false;
            }
            
            // Create HAL component instances for runtime use during slew, track, and park.
            // These provide MotorControl (position/velocity control), EncoderReader
            // (position feedback), SafetyMonitor (hardware limit checking), and
            // SensorInterface (environmental monitoring) — replacing or augmenting
            // direct CANopen interface calls during operations.
            hal_axis1_motor_ = hal_interface_->createMotorControl(0);
            hal_axis2_motor_ = hal_interface_->createMotorControl(1);
            hal_axis1_encoder_ = hal_interface_->createEncoderReader(0);
            hal_axis2_encoder_ = hal_interface_->createEncoderReader(1);
            hal_safety_monitor_ = hal_interface_->createSafetyMonitor();
            hal_sensor_interface_ = hal_interface_->createSensorInterface();
            
            MOUNT_LOG_DEBUG("HAL components created for axes 0 and 1");
            
            // HAL motors do NOT need explicit enable() here — the CANopen
            // interface's enableDrive() (called below) handles the full
            // CiA 402 enable sequence including cancellation of stale
            // motion profiles.  Enabling HAL motors separately would
            // trigger a duplicate state-machine transition that may
            // restart a stored motion profile.
        }
        
        // Initialize CANopen interface using factory
        ICanOpenInterface::Config canopen_config;
        if (!config_.canopen_interface.empty()) {
            // Create real CANopen interface
#ifdef HAVE_CANOPEN
            canopen_config.library = "canopensocket";
#else
            canopen_config.library = "mock";
#endif
            canopen_config.interface_name = config_.canopen_interface; // "can0" from config
            canopen_config.bitrate = config_.canopen_bitrate;
            canopen_config.node_id = config_.canopen_node_id;
            canopen_config.use_sync = config_.canopen_use_sync;
            canopen_config.sync_period_ms = config_.canopen_sync_period_ms;
            canopen_config.sdo_timeout_ms = config_.canopen_sdo_timeout_ms;
            canopen_config.axis_position_counts_per_degree[0] = config_.ha_axis_params.position_counts_per_degree;
            canopen_config.axis_position_counts_per_degree[1] = config_.dec_axis_params.position_counts_per_degree;
            canopen_config.axis_velocity_counts_per_deg_s[0] = config_.ha_axis_params.velocity_counts_per_deg_s;
            canopen_config.axis_velocity_counts_per_deg_s[1] = config_.dec_axis_params.velocity_counts_per_deg_s;
            canopen_config.accel_mode = config_.canopen_accel_mode;
            canopen_config.pdo_config_enabled = config_.canopen_pdo_config_enabled;
            
            // Propagate servo initialization sequence from ControllerConfig
            canopen_config.servo_init_enabled = config_.servo_init_enabled;
            canopen_config.servo_init_sequence = config_.servo_init_sequence;
            
            canopen_interface_ = CanOpenFactory::create(canopen_config);
            if (!canopen_interface_) {
                // Fall back to mock if creation fails
                canopen_config.library = "mock";
                canopen_interface_ = CanOpenFactory::create(canopen_config);
            }
        } else {
            // Use mock interface by default
            canopen_config.library = "mock";
            canopen_interface_ = CanOpenFactory::create(canopen_config);
        }
        
        // Initialize the CANopen interface if created (use the actual config, not empty)
        if (canopen_interface_) {
            if (!canopen_interface_->initialize(canopen_config)) {
                MOUNT_LOG_ERROR("CANopen interface initialization failed — "
                               "interface={}, node_id={}. Check that the CAN "
                               "interface exists and is up.",
                               canopen_config.interface_name,
                               canopen_config.node_id);
                // Continue with mock fallback — the system can still serve
                // gRPC requests and report status, just without drive control.
            }
            
            // Enable drives for axes 0 (HA/RA) and 1 (Dec).
            // In real CANopen hardware, drives are enabled during power-up sequence;
            // for mock interface, this ensures setPositionTarget calls succeed.
            // Each enableDrive() performs multiple SDO operations that time out
            // (~1 s each) when no drive is on the bus. Log progress so the
            // operator knows the controller is not hung, just waiting for drives.
            MOUNT_LOG_INFO("Enabling CANopen axis 0 (node {})...",
                          canopen_config.node_id);
            if (!canopen_interface_->enableDrive(0)) {
                MOUNT_LOG_WARN("Failed to enable axis 0 — no drive responding on CAN bus?");
            }
            
            MOUNT_LOG_INFO("Enabling CANopen axis 1 (node {})...",
                          canopen_config.node_id + 1);
            if (!canopen_interface_->enableDrive(1)) {
                MOUNT_LOG_WARN("Failed to enable axis 1 — no drive responding on CAN bus?");
            }

            // ── Sync internal state with actual drive positions ──────
            // After enabling the drives, read their actual positions BEFORE
            // sending any position targets.  This prevents uncontrolled
            // rotations at max_slew_rate when the drive remembers its
            // position from before a restart (e.g. after config change).
            //
            // For incremental encoders the park position is used as a
            // fallback when the drive does not respond to position queries
            // (e.g. mock interface, or drive not on the bus).
            bool drive_positions_read = false;
            if (canopen_interface_->isConnected()) {
                try {
                    auto pos0 = canopen_interface_->getPositionData(0);
                    auto pos1 = canopen_interface_->getPositionData(1);
                    if (std::isfinite(pos0.actual_position) &&
                        std::isfinite(pos1.actual_position)) {
                        axis1_position_ = pos0.actual_position;
                        axis2_position_ = pos1.actual_position;
                        raw_servo_axis1_position_ = pos0.actual_position;
                        raw_servo_axis2_position_ = pos1.actual_position;
                        drive_positions_read = true;
                        MOUNT_LOG_INFO("Synced internal state to drive positions: "
                                      "axis1={:.4f}°, axis2={:.4f}°",
                                      axis1_position_, axis2_position_);
                    }
                } catch (const std::exception& e) {
                    MOUNT_LOG_WARN("Could not read drive positions during init: {}", e.what());
                }
            }

            // Now set position targets to the current position (no-op motion)
            // using a moderate velocity to prevent sudden moves if the positions
            // didn't match (shouldn't happen since we just synced).
            canopen_interface_->setPositionTarget(0, axis1_position_,
                                                  config_.max_slew_rate,
                                                  config_.slew_acceleration);
            canopen_interface_->setPositionTarget(1, axis2_position_,
                                                  config_.max_slew_rate,
                                                  config_.slew_acceleration);
            MOUNT_LOG_DEBUG("CANopen position targets set to ({:.4f}°, {:.4f}°) "
                           "(drive_positions_read={})",
                           axis1_position_, axis2_position_, drive_positions_read);
        }
        
        // Configure TPointModel with mount and telescope physical parameters.
        // These are needed for accurate modeling of flexure and pier geometry.
        tpoint_model_->setMountParameters(config.mount_height,
                                          config.pier_west,
                                          config.pier_east);
        tpoint_model_->setTelescopeParameters(config.focal_length,
                                              config.aperture,
                                              0.0);  // tube_length not in config
        
        // Enable TPoint error terms from configuration.
        // Respects user's explicit term selection; falls back to DEFAULT_TERMS
        // if the config value is 0 (uninitialized).
        {
            uint32_t enabled_terms = config.tpoint_enabled_terms;
            if (enabled_terms == 0) {
                enabled_terms = models::TPointTerms::DEFAULT_TERMS;
            }
            tpoint_model_->setEnabledTerms(enabled_terms);
            // Store the effective bitmask for later use in runTPointCalibration
            tpoint_enabled_terms_ = enabled_terms;
        }
        
        // Set observer location on AstronomicalCalculations so coordinate
        // conversions (equatorialToHorizontal, horizontalToEquatorial) and
        // refraction calculations use the configured latitude/longitude.
        if (astro_calc_) {
            astro_calc_->setObserverLocation(config_.latitude, config_.longitude, config_.altitude);
        }
        
        // --- Create DerotatorController (internal module) ---
        // The derotator gets its own HAL motor/encoder instances and a raw pointer
        // to the shared CANopen interface. MountController retains field rotation
        // rate calculation (depends on mount axis positions) and pushes the result
        // to DerotatorController via setFieldRotationRate().
        {
            // Mount type conversion for DerotatorController
            DerotatorController::MountType derotator_mount_type;
            switch (config_.mount_type) {
                case MountType::EQUATORIAL:
                    derotator_mount_type = DerotatorController::MountType::EQUATORIAL;
                    break;
                case MountType::ALT_AZ:
                    derotator_mount_type = DerotatorController::MountType::ALT_AZ;
                    break;
                case MountType::CASUAL:
                    derotator_mount_type = DerotatorController::MountType::CASUAL;
                    break;
                default:
                    derotator_mount_type = DerotatorController::MountType::UNKNOWN;
                    break;
            }
            
            DerotatorController::Config derotator_cfg;
            // Keep default derotator config (may be updated later via configureDerotator)
            derotator_cfg.mount_type = derotator_mount_type;
            derotator_cfg.latitude_deg = config_.latitude;
            
            // MountStateProvider: provides current altitude from mount position
            auto state_provider = [this]() -> DerotatorController::MountState {
                std::shared_lock<std::shared_mutex> lock(*state_mutex_);
                DerotatorController::MountState s;
                // For ALT_AZ: axis2 = altitude, For CASUAL: axis1 = altitude-like
                if (config_.mount_type == MountType::CASUAL) {
                    s.altitude_deg = axis1_position_;
                } else {
                    s.altitude_deg = axis2_position_;
                }
                return s;
            };
            
            // Create HAL derotator components (optional — derotator may not be present)
            auto derotator_motor = hal_interface_ ? hal_interface_->createDerotatorMotor() : nullptr;
            auto derotator_encoder = hal_interface_ ? hal_interface_->createDerotatorEncoder() : nullptr;
            
            if (derotator_motor) {
                MOUNT_LOG_DEBUG("HAL derotator motor created");
            }
            if (derotator_encoder) {
                MOUNT_LOG_DEBUG("HAL derotator encoder created");
            }
            
            derotator_ = std::make_unique<DerotatorController>(
                canopen_interface_.get(),
                std::move(derotator_motor),
                std::move(derotator_encoder),
                std::move(state_provider),
                derotator_cfg
            );
            
            MOUNT_LOG_DEBUG("DerotatorController created");
        }
        
        // Open the gamepad device for live state reporting (UI) only.
        // The axis-control loop (gamepadLoop) is NEVER auto-started —
        // it would flood CANopen with velocity commands and fight
        // normal slewing/tracking.  UI shows live axes/buttons but
        // the mount is never driven by the gamepad.
        initGamepadInput();
        
        return true;
    }
    
    void shutdown() {
        // Idempotency guard: if already UNINITIALIZED, return immediately.
        // Prevents double-shutdown issues such as hal_interface_->shutdown()
        // being called twice, or redundant joinWorkThread()/HAL teardown.
        {
            std::lock_guard<std::shared_mutex> lock(*state_mutex_);
            if (state_ == MountStatus::State::UNINITIALIZED) {
                return;
            }
        }
        stop();
        {
            std::lock_guard<std::shared_mutex> lock(*state_mutex_);
            state_ = MountStatus::State::UNINITIALIZED;
        }
        // Notify AFTER releasing state_mutex_ (lock scope ended above)
        notifyStatusChanged();  // any → UNINITIALIZED
        // Wait for any background thread to finish before destroying members
        joinWorkThread();
        
        // Shut down HAL components in reverse creation order.
        // Reset unique_ptrs to destroy component instances before shutting down
        // the HAL interface itself, ensuring clean teardown.
        // Shut down DerotatorController first (it has its own thread and HAL components)
        if (derotator_) {
            derotator_->shutdown();
        }
        derotator_.reset();
        hal_sensor_interface_.reset();
        hal_safety_monitor_.reset();
        hal_axis1_encoder_.reset();
        hal_axis2_encoder_.reset();
        if (hal_axis1_motor_) hal_axis1_motor_->disable();
        if (hal_axis2_motor_) hal_axis2_motor_->disable();
        hal_axis1_motor_.reset();
        hal_axis2_motor_.reset();
        if (hal_interface_) {
            hal_interface_->stop();
            hal_interface_->shutdown();
        }
        stopGamepad();
    }
    
    /**
     * @brief Recreate the DerotatorController instance after HAL re-initialization.
     *
     * Called by setHALConfig() and reinitializeHAL() after the HAL interface has
     * been re-created. Creates fresh derotator motor/encoder components from the
     * new HAL interface and constructs a new DerotatorController with the current
     * mount config.
     *
     * @return true if the DerotatorController was created successfully, false if
     *         derotator HAL components are unavailable (non-fatal).
     */
    bool recreateDerotator() {
        if (!hal_interface_) {
            return false;
        }
        
        // DerotatorController::MountType conversion
        DerotatorController::MountType derotator_mount_type;
        switch (config_.mount_type) {
            case MountType::EQUATORIAL:
                derotator_mount_type = DerotatorController::MountType::EQUATORIAL;
                break;
            case MountType::ALT_AZ:
                derotator_mount_type = DerotatorController::MountType::ALT_AZ;
                break;
            case MountType::CASUAL:
                derotator_mount_type = DerotatorController::MountType::CASUAL;
                break;
            default:
                derotator_mount_type = DerotatorController::MountType::UNKNOWN;
                break;
        }
        
        DerotatorController::Config derotator_cfg;
        derotator_cfg.mount_type = derotator_mount_type;
        derotator_cfg.latitude_deg = config_.latitude;
        
        // MountStateProvider: provides current altitude from mount position
        auto state_provider = [this]() -> DerotatorController::MountState {
            std::shared_lock<std::shared_mutex> lock(*state_mutex_);
            DerotatorController::MountState s;
            if (config_.mount_type == MountType::CASUAL) {
                s.altitude_deg = axis1_position_;
            } else {
                s.altitude_deg = axis2_position_;
            }
            return s;
        };
        
        // Create HAL derotator components (optional — derotator may not be present)
        auto derotator_motor = hal_interface_->createDerotatorMotor();
        auto derotator_encoder = hal_interface_->createDerotatorEncoder();
        
        if (!derotator_motor) {
            MOUNT_LOG_DEBUG("recreateDerotator: HAL derotator motor not available");
            return false;
        }
        
        derotator_ = std::make_unique<DerotatorController>(
            canopen_interface_.get(),
            std::move(derotator_motor),
            std::move(derotator_encoder),
            std::move(state_provider),
            derotator_cfg
        );
        
        MOUNT_LOG_DEBUG("DerotatorController recreated after HAL re-initialization");
        return true;
    }
    
    bool slewToEquatorial(double ra, double dec) {
        // Reject non-finite coordinates to prevent infinite loops in HA normalization
        if (!std::isfinite(ra) || !std::isfinite(dec)) {
            return false;
        }
        
        // Lock thread_mutex_ across the entire join + state-check + create sequence
        // to prevent data races on work_thread_ from concurrent calls.
        // The work thread never touches thread_mutex_, so no deadlock risk.
        {
            std::lock_guard<std::mutex> tlock(*thread_mutex_);
            
            // Signal any running work thread (e.g. tracking loop) to stop before joining.
            // If a tracking loop is running (tracking_active_ = true), joinWorkThreadLocked()
            // would block forever because the tracking loop only exits when tracking_active_
            // becomes false. Setting it here ensures the work thread terminates promptly.
            tracking_active_ = false;
            
            // Join any previous work thread (thread_mutex_ held, state_mutex_ NOT held)
            joinWorkThreadLocked();
            
            {
                std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                
                if (state_ == MountStatus::State::UNINITIALIZED || state_ == MountStatus::State::ERROR) {
                    return false;
                }
                if (state_ == MountStatus::State::SLEWING || state_ == MountStatus::State::TRACKING) {
                    return false;  // Already moving
                }
                
                // Convert RA/Dec to mount coordinates
                // For equatorial mounts, axis1 tracks Hour Angle (HA = LST - RA)
                // For CASUAL mounts, RA/Dec is converted to mount-frame alt/az via orientation quaternion
                if (config_.mount_type == MountType::EQUATORIAL) {
                    double jd = core::AstronomicalCalculations::getCurrentJulianDate();
                    double lst = core::AstronomicalCalculations::calculateLST(jd, config_.longitude);
                    double ha_hours = lst - ra;
                    // Normalize HA to [-12, 12] hours
                    // Note: finite inputs guarantee finite ha_hours, so no inf-loop risk
                    while (ha_hours > 12.0) ha_hours -= 24.0;
                    while (ha_hours < -12.0) ha_hours += 24.0;
                    
                    // Use TPOINT model to correct the mount position for systematic errors
                    // predictMountPosition() inverts the fitted model via Newton-Raphson
                    // to find the mount HA/Dec that produces the correct on-sky position
                    if (tpoint_calibrated_) {
                        auto [mount_ha, mount_dec] = tpoint_model_->predictMountPosition(ra, dec);
                        axis1_target_ = mount_ha * 15.0;  // Convert hours to degrees
                        axis2_target_ = mount_dec;
                    } else {
                        axis1_target_ = ha_hours * 15.0;  // Convert hours to degrees
                        axis2_target_ = dec;
                    }
                } else if (config_.mount_type == MountType::CASUAL) {
                    // Convert RA/Dec to mount-frame alt/az using the orientation quaternion
                    double jd = core::AstronomicalCalculations::getCurrentJulianDate();
                    auto [mount_alt, mount_az] = astro_calc_->equatorialToMountOrientation(
                        ra, dec, jd, mount_orientation_.quaternion);
                    axis1_target_ = mount_alt;   // Altitude in mount frame
                    axis2_target_ = mount_az;    // Azimuth in mount frame
                }
                
                // Check soft limits before initiating slew
                // For CASUAL mounts, axis2 is azimuth-like [0, 360) — wrapping means the
                // configured axis2 limits don't apply. Only axis1 is checked against soft limits.
                if (config_.soft_limits_enabled) {
                    bool limit_violation = (axis1_target_ < config_.soft_limit_axis1_min ||
                                            axis1_target_ > config_.soft_limit_axis1_max);
                    if (config_.mount_type != MountType::CASUAL) {
                        limit_violation = limit_violation ||
                            (axis2_target_ < config_.soft_limit_axis2_min ||
                             axis2_target_ > config_.soft_limit_axis2_max);
                    }
                    if (limit_violation) {
                        MOUNT_LOG_WARN("SlewToEquatorial target exceeds soft limits: axis1={:.1f}°, axis2={:.1f}°",
                                 axis1_target_, axis2_target_);
                        state_ = MountStatus::State::IDLE;
                        return false;  // callback skipped: no state change from caller's perspective
                    }
                }
                
                state_ = MountStatus::State::SLEWING;
                slew_count_++;
            }  // state_mutex_ released
            
            // Notify status callback outside state_mutex_ lock
            notifyStatusChanged();  // IDLE → SLEWING
            
            // Use HAL motor control or CANopen interface if available
            // (outside state_mutex_, but inside thread_mutex_)
            bool motion_start_failure = false;
            const double SLEW_VELOCITY = config_.max_slew_rate;
            const double SLEW_ACCELERATION = config_.slew_acceleration;
            
            if (hal_axis1_motor_ && hal_axis2_motor_) {
                // HAL path: use MotorControl::setPosition() for position mode slewing
                bool axis1_ok = hal_axis1_motor_->setPosition(axis1_target_, SLEW_VELOCITY, SLEW_ACCELERATION);
                bool axis2_ok = hal_axis2_motor_->setPosition(axis2_target_, SLEW_VELOCITY, SLEW_ACCELERATION);
                
                if (!axis1_ok || !axis2_ok) {
                    motion_start_failure = true;
                    {
                        std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                        state_ = MountStatus::State::IDLE;
                    }
                    // SLEWING → IDLE due to HAL motor failure, notify outside lock
                    notifyStatusChanged();
                }
            } else if (canopen_interface_) {
                // Fallback to direct CANopen control
                bool axis1_ok = canopen_interface_->setPositionTarget(
                    0, axis1_target_, SLEW_VELOCITY, SLEW_ACCELERATION);
                bool axis2_ok = canopen_interface_->setPositionTarget(
                    1, axis2_target_, SLEW_VELOCITY, SLEW_ACCELERATION);
                
                if (!axis1_ok || !axis2_ok) {
                    motion_start_failure = true;
                    {
                        std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                        state_ = MountStatus::State::IDLE;
                    }
                    // SLEWING → IDLE due to CANopen failure, notify outside lock
                    notifyStatusChanged();
                }
            }
            
            if (motion_start_failure) {
                return false;
            }
            
            // Background monitoring thread – waits for axes to reach target
            work_thread_ = std::thread([this]() {
            // Poll CANopen axes until both reach target (or slewing is cancelled)
            const int POLL_MS = config_.controller_poll_ms;
            const double POSITION_TOLERANCE_DEG = config_.position_tolerance;
            
            // Simulated timeout tracking - declared OUTSIDE the while loop
            // to persist across iterations (Fix 3: timeout was broken by re-initializing each loop)
            const int SIM_TIMEOUT_MS = 60000; // 60s max simulated slew
            int sim_elapsed_ms = 0;
            
            while (true) {
                // Check if slewing was cancelled
                {
                    std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                    if (state_ != MountStatus::State::SLEWING) break;
                }
                
                bool reached = true;
                
                if (hal_axis1_motor_ && hal_axis2_motor_) {
                    // HAL path: poll MotorControl::targetReached()
                    try {
                        reached = hal_axis1_motor_->targetReached() && hal_axis2_motor_->targetReached();
                    } catch (const std::exception& e) {
                        MOUNT_LOG_WARN("HAL motor error during slew: {}", e.what());
                        reached = false;
                    }
                } else if (canopen_interface_) {
                    try {
                        auto status0 = canopen_interface_->getDriveStatus(0);
                        auto status1 = canopen_interface_->getDriveStatus(1);
                        
                        if (!status0.target_reached) reached = false;
                        if (!status1.target_reached) reached = false;
                    } catch (const std::exception& e) {
                        MOUNT_LOG_WARN("CANopen communication error during slew: {}", e.what());
                        reached = false;
                    }
                } else {
                    // Simulated: update positions gradually with timeout
                    sim_elapsed_ms += POLL_MS;
                    
                    std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                    
                    // Evaluate soft limits and get rate scaling factor for deceleration zone
                    double rate_factor = evaluateSoftLimits(axis1_position_, axis2_position_);
                    
                    // Check for hard limit violation during slew
                    // For Alt-Az and CASUAL mounts, axis2 is azimuth-like [0, 360) — it wraps rather
                    // than hitting a hard stop, so only axis1 is checked against limits.
                    if (config_.soft_limits_enabled) {
                        bool limit_violation = (soft_limit_distance_axis1_ < 0.0);
                        if (config_.mount_type != MountType::ALT_AZ &&
                            config_.mount_type != MountType::CASUAL) {
                            limit_violation = limit_violation || (soft_limit_distance_axis2_ < 0.0);
                        }
                        if (limit_violation) {
                            MOUNT_LOG_ERROR("Slew aborted: soft limit exceeded: axis1={:.1f}°, axis2={:.1f}°",
                                     axis1_position_, axis2_position_);
                            state_ = MountStatus::State::ERROR;
                            error_message_ = "Slew aborted due to soft limit violation";
                            break;
                        }
                    }
                    
                    // Log warning when in deceleration zone during slew
                    if (config_.soft_limits_enabled && soft_limit_deceleration_active_) {
                        MOUNT_LOG_WARN("Slew deceleration active: {}", soft_limit_warning_message_);
                    }
                    
                    // Check hardware safety limits via HAL SafetyMonitor during slew
                    if (hal_safety_monitor_) {
                        try {
                            auto safety_status = hal_safety_monitor_->getStatus();
                            if (safety_status.overall_state == hal::SafetyStatus::State::EMERGENCY_STOP ||
                                safety_status.overall_state == hal::SafetyStatus::State::ERROR) {
                                MOUNT_LOG_ERROR("HAL safety monitor triggered during slew: state={}",
                                         safety_status.getStateString());
                                state_ = MountStatus::State::ERROR;
                                error_message_ = "HAL safety monitor: " + safety_status.getStateString();
                                break;
                            }
                            hal_safety_monitor_->checkLimits(0);
                            hal_safety_monitor_->checkLimits(1);
                        } catch (const std::exception& e) {
                            MOUNT_LOG_WARN("HAL safety monitor error during slew: {}", e.what());
                        }
                    }
                    
                    double d1 = axis1_target_ - axis1_position_;
                    double d2 = axis2_target_ - axis2_position_;
                    double step = 1.0 * rate_factor;  // Scale step in deceleration zone
                    
                    if (std::abs(d1) > POSITION_TOLERANCE_DEG) {
                        axis1_position_ += std::copysign(std::min(step, std::abs(d1)), d1);
                        reached = false;
                    } else {
                        axis1_position_ = axis1_target_;
                    }
                    
                    if (std::abs(d2) > POSITION_TOLERANCE_DEG) {
                        axis2_position_ += std::copysign(std::min(step, std::abs(d2)), d2);
                        reached = false;
                    } else {
                        axis2_position_ = axis2_target_;
                    }
                    
                    // Force completion on timeout to avoid thread hang
                    if (sim_elapsed_ms >= SIM_TIMEOUT_MS) {
                        axis1_position_ = axis1_target_;
                        axis2_position_ = axis2_target_;
                        reached = true;
                    }
                }
                
                if (reached) {
                    std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                    if (state_ == MountStatus::State::SLEWING) {
                        // Update actual positions from HAL encoder or CANopen feedback
                        if (hal_axis1_encoder_ && hal_axis2_encoder_) {
                            try {
                                auto enc1 = hal_axis1_encoder_->read();
                                auto enc2 = hal_axis2_encoder_->read();
                                if (enc1.data_valid && enc2.data_valid) {
                                    axis1_position_ = enc1.position_deg;
                                    axis2_position_ = enc2.position_deg;
                                }
                            } catch (const std::exception& e) {
                                MOUNT_LOG_WARN("HAL encoder read error during slew: {}", e.what());
                            }
                        } else if (canopen_interface_) {
                            auto pos0 = canopen_interface_->getPositionData(0);
                            auto pos1 = canopen_interface_->getPositionData(1);
                            axis1_position_ = pos0.actual_position;
                            axis2_position_ = pos1.actual_position;
                            raw_servo_axis1_position_ = pos0.actual_position;
                            raw_servo_axis2_position_ = pos1.actual_position;
                        }
                        axis1_rate_ = 0.0;
                        axis2_rate_ = 0.0;
                        state_ = MountStatus::State::IDLE;
                    }
                    break;
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(POLL_MS));
            }
            
            // Capture state after while-loop for callback invocation outside lock.
            MountStatus::State exit_state;
            std::string exit_error;
            {
                std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                exit_state = state_;
                exit_error = error_message_;
            }
            if (exit_state == MountStatus::State::ERROR) {
                notifyError(exit_error);
                notifyStatusChanged();
            } else if (exit_state == MountStatus::State::IDLE) {
                notifyStatusChanged();
            }
        });
        }  // end thread_mutex_ scope
        
        return true;
    }
    
    bool slewToHorizontal(double altitude, double azimuth) {
        // Reject non-finite coordinates
        if (!std::isfinite(altitude) || !std::isfinite(azimuth)) {
            return false;
        }
        
        // Lock thread_mutex_ across join + state-check + create to prevent data race
        {
            std::lock_guard<std::mutex> tlock(*thread_mutex_);
            
            // Signal any running work thread (e.g. tracking loop) to stop before joining.
            // If a tracking loop is running (tracking_active_ = true), joinWorkThreadLocked()
            // would block forever because the tracking loop only exits when tracking_active_
            // becomes false. Setting it here ensures the work thread terminates promptly.
            tracking_active_ = false;
            
            // Join any previous work thread (thread_mutex_ held, state_mutex_ NOT held)
            joinWorkThreadLocked();
            
            {
                std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                
                if (state_ == MountStatus::State::UNINITIALIZED || state_ == MountStatus::State::ERROR) return false;
                if (state_ == MountStatus::State::SLEWING || state_ == MountStatus::State::TRACKING) return false;
                
                if (config_.mount_type == MountType::CASUAL) {
                    // CASUAL mount: transform true horizontal (alt/az) to mount-frame coordinates
                    // using the mount orientation quaternion Q (ENU → mount frame rotation).
                    //
                    // Coordinate convention (matching astronomical_calculations.cpp):
                    //   ENU frame:     x = north, y = east, z = up
                    //   Mount frame:   x = north, y = east, z = up (mount zenith)
                    //
                    // Pipeline:
                    //   1. Horizontal (alt, az) → Cartesian ENU vector {north, east, up}
                    //   2. Apply quaternion Q to rotate from ENU to mount frame
                    //   3. Mount-frame Cartesian → mount (alt, az) angles
                    
                    // Step 1: Horizontal (alt, az) → Cartesian ENU vector {north, east, up}
                    double alt_rad = altitude * M_PI / 180.0;
                    double az_rad = azimuth * M_PI / 180.0;
                    double ce = std::cos(alt_rad);
                    double horiz_north = ce * std::cos(az_rad);   // North
                    double horiz_east  = ce * std::sin(az_rad);   // East
                    double horiz_up    = std::sin(alt_rad);       // Up
                    
                    // Step 2: Apply quaternion Q: v' = v + 2*qw*(q×v) + 2*(q×(q×v))
                    // where q = (qx, qy, qz) is the vector part, qw the scalar part
                    double qx = mount_orientation_.quaternion[0];
                    double qy = mount_orientation_.quaternion[1];
                    double qz = mount_orientation_.quaternion[2];
                    double qw = mount_orientation_.quaternion[3];
                    
                    // q × v
                    double cross_x = qy * horiz_up  - qz * horiz_east;
                    double cross_y = qz * horiz_north - qx * horiz_up;
                    double cross_z = qx * horiz_east - qy * horiz_north;
                    
                    // q × (q × v)
                    double cross2_x = qy * cross_z - qz * cross_y;
                    double cross2_y = qz * cross_x - qx * cross_z;
                    double cross2_z = qx * cross_y - qy * cross_x;
                    
                    double mount_north = horiz_north + 2.0 * qw * cross_x + 2.0 * cross2_x;
                    double mount_east  = horiz_east  + 2.0 * qw * cross_y + 2.0 * cross2_y;
                    double mount_up    = horiz_up    + 2.0 * qw * cross_z + 2.0 * cross2_z;
                    
                    // Step 3: Mount-frame Cartesian → mount (alt, az) angles
                    double norm = std::sqrt(mount_north * mount_north + mount_east * mount_east + mount_up * mount_up);
                    if (norm > 0.0) {
                        mount_north /= norm;
                        mount_east  /= norm;
                        mount_up    /= norm;
                    }
                    // Mount alt = asin(up), Mount az = atan2(east, north)
                    double mount_alt_rad = std::asin(std::clamp(mount_up, -1.0, 1.0));
                    double mount_az_rad = std::atan2(mount_east, mount_north);
                    double mount_alt = mount_alt_rad * 180.0 / M_PI;
                    double mount_az = mount_az_rad * 180.0 / M_PI;
                    if (mount_az < 0.0) mount_az += 360.0;
                    
                    // Map mount-frame angles to axis convention:
                    //   axis1 = azimuth-like (mount frame), axis2 = altitude-like (mount frame)
                    // This matches the ALT_AZ convention: axis1=azimuth, axis2=altitude.
                    // For identity quaternion, CASUAL behaves identically to ALT_AZ.
                    axis1_target_ = mount_az;     // azimuth-like axis in mount frame
                    axis2_target_ = mount_alt;   // altitude-like axis in mount frame
                } else {
                    axis1_target_ = azimuth;
                    axis2_target_ = altitude;
                }
                
                // Check soft limits before initiating slew
                // For CASUAL and ALT_AZ mounts, axis1 is azimuth-like [0, 360) — wrapping means the
                // configured axis1 limits don't apply. Only axis2 is checked against soft limits.
                if (config_.soft_limits_enabled) {
                    bool limit_violation = false;
                    if (config_.mount_type != MountType::ALT_AZ &&
                        config_.mount_type != MountType::CASUAL) {
                        limit_violation = (axis1_target_ < config_.soft_limit_axis1_min ||
                                           axis1_target_ > config_.soft_limit_axis1_max);
                    }
                    limit_violation = limit_violation ||
                        (axis2_target_ < config_.soft_limit_axis2_min ||
                         axis2_target_ > config_.soft_limit_axis2_max);
                    if (limit_violation) {
                        MOUNT_LOG_WARN("SlewToHorizontal target exceeds soft limits: axis1={:.1f}°, axis2={:.1f}°",
                                 axis1_target_, axis2_target_);
                        state_ = MountStatus::State::IDLE;
                        return false;  // callback skipped: no state change from caller's perspective
                    }
                }
                
                state_ = MountStatus::State::SLEWING;
            }  // state_mutex_ released
            
            // Notify status callback outside state_mutex_ lock
            notifyStatusChanged();  // IDLE → SLEWING
            
            bool motion_start_failure = false;
            const double SLEW_VELOCITY = config_.max_slew_rate;
            const double SLEW_ACCELERATION = config_.slew_acceleration;
            
            if (hal_axis1_motor_ && hal_axis2_motor_) {
                // HAL path: use MotorControl::setPosition() for position mode slewing
                bool axis1_ok = hal_axis1_motor_->setPosition(axis1_target_, SLEW_VELOCITY, SLEW_ACCELERATION);
                bool axis2_ok = hal_axis2_motor_->setPosition(axis2_target_, SLEW_VELOCITY, SLEW_ACCELERATION);
                
                if (!axis1_ok || !axis2_ok) {
                    motion_start_failure = true;
                    {
                        std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                        state_ = MountStatus::State::IDLE;
                    }
                    // SLEWING → IDLE due to HAL motor failure, notify outside lock
                    notifyStatusChanged();
                }
            } else if (canopen_interface_) {
                // Fallback to direct CANopen control
                bool axis1_ok = canopen_interface_->setPositionTarget(
                    0, axis1_target_, SLEW_VELOCITY, SLEW_ACCELERATION);
                bool axis2_ok = canopen_interface_->setPositionTarget(
                    1, axis2_target_, SLEW_VELOCITY, SLEW_ACCELERATION);
                
                if (!axis1_ok || !axis2_ok) {
                    motion_start_failure = true;
                    {
                        std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                        state_ = MountStatus::State::IDLE;
                    }
                    // SLEWING → IDLE due to CANopen failure, notify outside lock
                    notifyStatusChanged();
                }
            }
            
            if (motion_start_failure) {
                return false;
            }
            
            work_thread_ = std::thread([this]() {
            const int POLL_MS = config_.controller_poll_ms;
            const double POSITION_TOLERANCE_DEG = config_.position_tolerance;
            
            // Simulated timeout tracking - declared outside the while loop
            // to persist across iterations
            int sim_elapsed_ms = 0;
            
            while (true) {
                // Check if slewing was cancelled
                {
                    std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                    if (state_ != MountStatus::State::SLEWING) break;
                }
                bool reached = true;
                
                if (hal_axis1_motor_ && hal_axis2_motor_) {
                    // HAL path: poll MotorControl::targetReached()
                    try {
                        reached = hal_axis1_motor_->targetReached() && hal_axis2_motor_->targetReached();
                    } catch (const std::exception& e) {
                        MOUNT_LOG_WARN("HAL motor error during slew: {}", e.what());
                        reached = false;
                    }
                } else if (canopen_interface_) {
                    try {
                        auto status0 = canopen_interface_->getDriveStatus(0);
                        auto status1 = canopen_interface_->getDriveStatus(1);
                        if (!status0.target_reached) reached = false;
                        if (!status1.target_reached) reached = false;
                    } catch (const std::exception& e) {
                        MOUNT_LOG_WARN("CANopen communication error during slew: {}", e.what());
                        reached = false;
                    }
                } else {
                    // Simulated: update positions gradually with timeout
                    const int SIM_TIMEOUT_MS = 60000;
                    sim_elapsed_ms += POLL_MS;
                    
                    std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                    
                    // Evaluate soft limits and get rate scaling factor for deceleration zone
                    double rate_factor = evaluateSoftLimits(axis1_position_, axis2_position_);
                    
                    // Check for hard limit violation during slew
                    // For Alt-Az and CASUAL mounts, axis2 is azimuth-like [0, 360) — it wraps rather
                    // than hitting a hard stop, so only axis1 is checked against limits.
                    if (config_.soft_limits_enabled) {
                        bool limit_violation = (soft_limit_distance_axis1_ < 0.0);
                        if (config_.mount_type != MountType::ALT_AZ &&
                            config_.mount_type != MountType::CASUAL) {
                            limit_violation = limit_violation || (soft_limit_distance_axis2_ < 0.0);
                        }
                        if (limit_violation) {
                            MOUNT_LOG_ERROR("Slew aborted: soft limit exceeded: axis1={:.1f}°, axis2={:.1f}°",
                                     axis1_position_, axis2_position_);
                            state_ = MountStatus::State::ERROR;
                            error_message_ = "Slew aborted due to soft limit violation";
                            break;
                        }
                    }
                    
                    // Log warning when in deceleration zone during slew
                    if (config_.soft_limits_enabled && soft_limit_deceleration_active_) {
                        MOUNT_LOG_WARN("Slew deceleration active: {}", soft_limit_warning_message_);
                    }
                    
                    // Check hardware safety limits via HAL SafetyMonitor during slew
                    if (hal_safety_monitor_) {
                        try {
                            auto safety_status = hal_safety_monitor_->getStatus();
                            if (safety_status.overall_state == hal::SafetyStatus::State::EMERGENCY_STOP ||
                                safety_status.overall_state == hal::SafetyStatus::State::ERROR) {
                                MOUNT_LOG_ERROR("HAL safety monitor triggered during slew: state={}",
                                         safety_status.getStateString());
                                state_ = MountStatus::State::ERROR;
                                error_message_ = "HAL safety monitor: " + safety_status.getStateString();
                                break;
                            }
                            hal_safety_monitor_->checkLimits(0);
                            hal_safety_monitor_->checkLimits(1);
                        } catch (const std::exception& e) {
                            MOUNT_LOG_WARN("HAL safety monitor error during slew: {}", e.what());
                        }
                    }
                    
                    double d1 = axis1_target_ - axis1_position_;
                    double d2 = axis2_target_ - axis2_position_;
                    double step = 1.0 * rate_factor;  // Scale step in deceleration zone
                    
                    if (std::abs(d1) > POSITION_TOLERANCE_DEG) {
                        axis1_position_ += std::copysign(std::min(step, std::abs(d1)), d1);
                        reached = false;
                    } else {
                        axis1_position_ = axis1_target_;
                    }
                    
                    if (std::abs(d2) > POSITION_TOLERANCE_DEG) {
                        axis2_position_ += std::copysign(std::min(step, std::abs(d2)), d2);
                        reached = false;
                    } else {
                        axis2_position_ = axis2_target_;
                    }
                    
                    // Force completion on timeout to avoid thread hang
                    if (sim_elapsed_ms >= SIM_TIMEOUT_MS) {
                        axis1_position_ = axis1_target_;
                        axis2_position_ = axis2_target_;
                        reached = true;
                    }
                }
                
                if (reached) {
                    std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                    if (state_ == MountStatus::State::SLEWING) {
                        // Update actual positions from HAL encoder or CANopen feedback
                        if (hal_axis1_encoder_ && hal_axis2_encoder_) {
                            try {
                                auto enc1 = hal_axis1_encoder_->read();
                                auto enc2 = hal_axis2_encoder_->read();
                                if (enc1.data_valid && enc2.data_valid) {
                                    axis1_position_ = enc1.position_deg;
                                    axis2_position_ = enc2.position_deg;
                                }
                            } catch (const std::exception& e) {
                                MOUNT_LOG_WARN("HAL encoder read error during slew: {}", e.what());
                            }
                        } else if (canopen_interface_) {
                            auto pos0 = canopen_interface_->getPositionData(0);
                            auto pos1 = canopen_interface_->getPositionData(1);
                            axis1_position_ = pos0.actual_position;
                            axis2_position_ = pos1.actual_position;
                            raw_servo_axis1_position_ = pos0.actual_position;
                            raw_servo_axis2_position_ = pos1.actual_position;
                        }
                        axis1_rate_ = 0.0;
                        axis2_rate_ = 0.0;
                        state_ = MountStatus::State::IDLE;
                    }
                    break;
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(POLL_MS));
            }
            
            // Capture state after while-loop for callback invocation outside lock.
            MountStatus::State exit_state;
            std::string exit_error;
            {
                std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                exit_state = state_;
                exit_error = error_message_;
            }
            if (exit_state == MountStatus::State::ERROR) {
                notifyError(exit_error);
                notifyStatusChanged();
            } else if (exit_state == MountStatus::State::IDLE) {
                notifyStatusChanged();
            }
        });
        }  // end thread_mutex_ scope
        
        return true;
    }
    
    bool startTracking(double ra, double dec, TrackingMode mode) {
        // Reject non-finite coordinates to prevent infinite loops in HA normalization
        if (!std::isfinite(ra) || !std::isfinite(dec)) {
            return false;
        }
        
        // Quick state check WITHOUT joining the work thread first.
        // If a slew/track/park is in progress, reject immediately rather than
        // waiting for it to complete (preserves original behavior).
        {
            std::lock_guard<std::shared_mutex> lock(*state_mutex_);
            if (state_ == MountStatus::State::SLEWING ||
                state_ == MountStatus::State::TRACKING ||
                state_ == MountStatus::State::PARKING) {
                return false;  // Already moving
            }
            if (state_ == MountStatus::State::UNINITIALIZED || state_ == MountStatus::State::ERROR) {
                return false;
            }
        }
        
        // Join any previous work thread WITHOUT holding state_mutex_.
        // This avoids deadlock: if we held state_mutex_, a running work thread
        // would block on state_mutex_ while we block on joinWorkThread().
        joinWorkThread();
        
        double axis1_tracking_rate = 0.0;  // deg/s
        double axis2_tracking_rate = 0.0;  // deg/s
        
        {
            std::lock_guard<std::shared_mutex> lock(*state_mutex_);
            
            // Re-check state after join (another thread may have started a move)
            if (state_ == MountStatus::State::UNINITIALIZED || state_ == MountStatus::State::ERROR) {
                return false;
            }
            if (state_ == MountStatus::State::SLEWING || state_ == MountStatus::State::TRACKING) {
                return false;  // Already moving (started between quick check and join)
            }
            
            // For equatorial mounts, axis1 tracks Hour Angle (HA = LST - RA).
            // For CASUAL mounts, RA/Dec is converted to mount-frame alt/az via orientation quaternion.
            // For Alt-Az mounts, axis1 = altitude, axis2 = azimuth (caller passes
            // altitude as 'ra' and azimuth as 'dec').
            if (config_.mount_type == MountType::EQUATORIAL) {
                double jd = core::AstronomicalCalculations::getCurrentJulianDate();
                double lst = core::AstronomicalCalculations::calculateLST(jd, config_.longitude);
                double ha_hours = lst - ra;
                // Normalize HA to [-12, 12] hours
                // Note: finite inputs guarantee finite ha_hours, so no inf-loop risk
                while (ha_hours > 12.0) ha_hours -= 24.0;
                while (ha_hours < -12.0) ha_hours += 24.0;
                
                // Use TPOINT model to correct the mount position for systematic errors
                // predictMountPosition() inverts the fitted model via Newton-Raphson
                // to find the mount HA/Dec that produces the correct on-sky position
                if (tpoint_calibrated_) {
                    auto [mount_ha, mount_dec] = tpoint_model_->predictMountPosition(ra, dec);
                    axis1_target_ = mount_ha * 15.0;  // Convert hours to degrees
                    axis2_target_ = mount_dec;
                } else {
                    axis1_target_ = ha_hours * 15.0;  // Convert hours to degrees
                    axis2_target_ = dec;
                }
            } else if (config_.mount_type == MountType::CASUAL) {
                // Convert RA/Dec to mount-frame alt/az using the orientation quaternion
                double jd = core::AstronomicalCalculations::getCurrentJulianDate();
                auto [mount_alt, mount_az] = astro_calc_->equatorialToMountOrientation(
                    ra, dec, jd, mount_orientation_.quaternion);
                axis1_target_ = mount_alt;   // Altitude in mount frame
                axis2_target_ = mount_az;    // Azimuth in mount frame
            } else {  // ALT_AZ
                // axis1 = altitude, axis2 = azimuth — use caller values directly
                axis1_target_ = ra;
                axis2_target_ = dec;
            }
            
            // Check soft limits before starting tracking
            // For Alt-Az and CASUAL mounts, axis2 is azimuth-like [0, 360) — wrapping means the
            // configured axis2 limits don't apply. Only axis1 is checked against soft limits.
            if (config_.soft_limits_enabled) {
                bool limit_violation = (axis1_target_ < config_.soft_limit_axis1_min ||
                                        axis1_target_ > config_.soft_limit_axis1_max);
                if (config_.mount_type != MountType::ALT_AZ &&
                    config_.mount_type != MountType::CASUAL) {
                    limit_violation = limit_violation ||
                        (axis2_target_ < config_.soft_limit_axis2_min ||
                         axis2_target_ > config_.soft_limit_axis2_max);
                }
                if (limit_violation) {
                    MOUNT_LOG_WARN("StartTracking target exceeds soft limits: axis1={:.1f}°, axis2={:.1f}°",
                             axis1_target_, axis2_target_);
                    return false;
                }
            }
            
            // Calculate tracking rates based on mode.
            // All rates are in SERVO degrees/second — the rates are applied
            // directly to axis1_position_ (servo position), so they must be
            // multiplied by the gear ratio to convert from celestial
            // (telescope-axis) rates to servo-motor rates.
            //
            // Celestial rates:
            //   Sidereal: 360° / 86164.0905 s = 0.004178 °/s (telescope)
            //   Solar:    360° / 86400 s     = 0.004167 °/s (telescope)
            //   Lunar:    ~14.685 "/s        = 0.004079 °/s (telescope)
            //
            // With gear_ratio G, the servo must move G× faster.
            const double ha_gear = config_.ha_axis_params.gear_ratio;
            const double dec_gear = config_.dec_axis_params.gear_ratio;

            if (config_.mount_type == MountType::EQUATORIAL) {
                switch (mode) {
                    case TrackingMode::SIDEREAL:
                        axis1_tracking_rate = 0.004178 * ha_gear;
                        axis2_tracking_rate = 0.0;
                        break;
                    case TrackingMode::SOLAR:
                        axis1_tracking_rate = 0.004167 * ha_gear;
                        axis2_tracking_rate = 0.0;
                        break;
                    case TrackingMode::LUNAR:
                        axis1_tracking_rate = 0.004079 * ha_gear;
                        axis2_tracking_rate = 0.0;
                        break;
                    case TrackingMode::CUSTOM:
                        // max_tracking_rate is now in servo °/s (unified)
                        axis1_tracking_rate = config_.max_tracking_rate;
                        axis2_tracking_rate = 0.0;
                        break;
                    case TrackingMode::OFF:
                        axis1_tracking_rate = 0.0;
                        axis2_tracking_rate = 0.0;
                        break;
                }
            } else {  // CASUAL or ALT_AZ
                // Rates are computed dynamically in the tracking loop based on
                // current altitude/azimuth position. The mode (SIDEREAL/SOLAR/LUNAR)
                // determines ω scaling in the rate equations.
                axis1_tracking_rate = 0.0;
                axis2_tracking_rate = 0.0;
            }
            
            axis1_rate_ = axis1_tracking_rate;
            axis2_rate_ = axis2_tracking_rate;
            
            state_ = MountStatus::State::TRACKING;
            track_count_++;
            
            // Meridian flip sentinel for non-equatorial mounts
            // Set time_to_meridian_ to a large positive value before the tracking
            // thread starts, so getTimeToMeridian() never returns 0 (default) for
            // ALT_AZ or CASUAL mounts where meridian flips are not applicable.
            if (config_.mount_type != MountType::EQUATORIAL) {
                time_to_meridian_ = 24.0;
                pier_side_ = 1;
            }
            
            // Use HAL motor velocity control or CANopen if available
            if (hal_axis1_motor_ && hal_axis2_motor_) {
                hal_axis1_motor_->setVelocity(axis1_tracking_rate, config_.tracking_acceleration);
                hal_axis2_motor_->setVelocity(axis2_tracking_rate, config_.tracking_acceleration);
            } else if (canopen_interface_) {
                canopen_interface_->setVelocityTarget(0, axis1_tracking_rate, config_.tracking_acceleration);
                canopen_interface_->setVelocityTarget(1, axis2_tracking_rate, config_.tracking_acceleration);
            }
            
            // Start HAL interface — enables periodic hardware I/O for motor control,
            // encoder feedback, and safety monitoring during tracking operations.
            if (hal_interface_) {
                hal_interface_->start();
            }
            
            tracking_active_ = true;
        }  // end state_mutex_ scope
        
        // Notify status callback outside state_mutex_ lock
        notifyStatusChanged();  // IDLE → TRACKING (or other → TRACKING)
        
        // Lock thread_mutex_ only for the work_thread_ assignment (not held during join above)
        {
            std::lock_guard<std::mutex> tlock(*thread_mutex_);
            work_thread_ = std::thread([this, axis1_tracking_rate, axis2_tracking_rate, mode]() {
            // Track actual elapsed time between iterations, not hardcoded 0.1s.
            // When the scheduler delays wake-up (e.g. sleep_for(100ms) takes 200ms+),
            // using a hardcoded dt causes position under/over-correction.
            auto last_iteration = std::chrono::steady_clock::now();
            while (tracking_active_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(config_.tracking_update_ms));
                
                // Measure actual elapsed time since last iteration
                auto now = std::chrono::steady_clock::now();
                double dt = std::chrono::duration<double>(now - last_iteration).count();
                last_iteration = now;
                
                // Flags for state transitions that happen mid-iteration inside the lock.
                // These are checked after the lock scope ends so callbacks can fire
                // without holding state_mutex_ (prevents deadlock if user callback
                // re-enters the controller via getStatus() etc.).
                // Declare variables for I/O operations outside the lock scope
                bool hal_safety_error = false;
                std::string hal_safety_error_msg;
                bool meridian_flip_triggered_ = false;
                bool flip_completed_ = false;
                double snap_rate_1 = 0, snap_rate_2 = 0;
                double snap_tracking_accel = 0;
                double snap_temperature = 20.0;
                bool is_tracking = false;
                
                // ---- I/O Block 1: HAL safety monitor check (outside state_mutex_) ----
                // Reading hardware safety status is I/O-bound and takes significant time
                // (CANopen SDO or similar). We read it here without state_mutex_ held,
                // then only take the lock briefly to set error state if needed.
                if (hal_safety_monitor_) {
                    try {
                        auto safety_status = hal_safety_monitor_->getStatus();
                        if (safety_status.overall_state == hal::SafetyStatus::State::EMERGENCY_STOP ||
                            safety_status.overall_state == hal::SafetyStatus::State::ERROR) {
                            MOUNT_LOG_ERROR("HAL safety monitor triggered: state={}",
                                     safety_status.getStateString());
                            hal_safety_error = true;
                            hal_safety_error_msg = "HAL safety monitor: " + safety_status.getStateString();
                        }
                        hal_safety_monitor_->checkLimits(0);
                        hal_safety_monitor_->checkLimits(1);
                    } catch (const std::exception& e) {
                        MOUNT_LOG_WARN("HAL safety monitor error: {}", e.what());
                    }
                }
                
                // ---- I/O Block 2: HAL sensor reads (outside state_mutex_, under env_mutex_) ----
                // Environmental sensor reads (temperature, pressure, humidity) are I/O-bound
                // and use the dedicated env_mutex_ to avoid contention on state_mutex_.
                if (hal_sensor_interface_ && hal_sensor_interface_->isInitialized()) {
                    try {
                        auto readings = hal_sensor_interface_->readAll();
                        {
                            std::lock_guard<std::mutex> env_lock(*env_mutex_);
                            for (const auto& reading : readings) {
                                if (reading.valid) {
                                    switch (reading.type) {
                                        case hal::SensorType::TEMPERATURE:
                                            env_temperature_ = reading.value;
                                            break;
                                        case hal::SensorType::PRESSURE:
                                            env_pressure_ = reading.value;
                                            break;
                                        case hal::SensorType::HUMIDITY:
                                            env_humidity_ = reading.value;
                                            break;
                                        default:
                                            // Other sensor types (current, voltage, etc.) are
                                            // not directly used by mount controller calculations
                                            break;
                                    }
                                }
                            }
                        }
                    } catch (const std::exception& e) {
                        // Non-critical: log at debug level, continue with existing values
                        MOUNT_LOG_DEBUG("HAL sensor read error: {}", e.what());
                    }
                }
                
                {   // Inner scope for state_mutex_ lock — computation only, no I/O
                std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                
                if (!tracking_active_) break;
                if (state_ != MountStatus::State::TRACKING &&
                    state_ != MountStatus::State::MERIDIAN_FLIP) break;
                
                // Track iteration count and timing for metrics
                tracking_iteration_count_++;
                total_update_time_ms_ += dt * 1000.0;

                // Periodic telescope position logging (every 100 iterations ≈ 10 s).
                // Reports the actual telescope axis position (in mount degrees) alongside
                // the tracking rates, so the operator can verify the mount is tracking correctly.
                if (tracking_iteration_count_ % 100 == 0) {
                    MOUNT_LOG_INFO("Tracking pos: axis1={:.4f}° axis2={:.4f}° "
                                   "| rate: axis1={:.6f}°/s axis2={:.6f}°/s "
                                   "| iter={} dt={:.3f}s",
                                   axis1_position_, axis2_position_,
                                   axis1_rate_, axis2_rate_,
                                   tracking_iteration_count_, dt);
                }

                // Apply soft safety limits: evaluate zones and get rate scaling factor
                double rate_factor = evaluateSoftLimits(axis1_position_, axis2_position_);
                
                // Guard against NaN rate_factor (from non-finite positions passed to evaluateSoftLimits
                // or internal numerical error). A NaN rate_factor would propagate through rates to
                // positions, silently corrupting all subsequent calculations.
                if (!std::isfinite(rate_factor)) {
                    MOUNT_LOG_ERROR("Non-finite rate factor from evaluateSoftLimits: rate_factor={}, "
                             "axis1={}, axis2={}", rate_factor, axis1_position_, axis2_position_);
                    state_ = MountStatus::State::ERROR;
                    error_message_ = "Numerical error: NaN/Inf rate factor from soft limit evaluation";
                    break;
                }
                
                // Watchdog: detect tracking loop thread freeze.
                // If sleep_for(100ms) doesn't return (scheduler hang, I/O deadlock, kernel
                // freeze) or the total iteration time exceeds the watchdog threshold, the
                // mount would silently lose tracking. This guard catches that by checking
                // the actual elapsed dt against a 5-second timeout threshold.
                if (dt > 5.0) {
                    MOUNT_LOG_ERROR("Tracking loop watchdog timeout: iteration took {:.3f}s "
                             "(expected ~0.1s). axis1={}, axis2={}",
                             dt, axis1_position_, axis2_position_);
                    state_ = MountStatus::State::ERROR;
                    error_message_ = "Tracking loop watchdog timeout: iteration took " +
                                     std::to_string(dt) + "s";
                    break;
                }
                
                // Read base tracking rates and guider position offsets under rate_mutex_.
                // Guider deltas are accumulated in applyGuiderCorrection() as a position
                // offset (degrees) and applied directly to axis positions — independent
                // of the loop frequency (dt). Each correction is consumed exactly once
                // (reset to zero) to prevent unbounded accumulation.
                double current_rate_1, current_rate_2;
                double guider_offset_1 = 0.0, guider_offset_2 = 0.0;
                {
                	std::shared_lock<std::shared_mutex> rate_lock(*rate_mutex_);
                	current_rate_1 = axis1_rate_;
                	current_rate_2 = axis2_rate_;
                	guider_offset_1 = guider_delta_axis1_;
                	guider_offset_2 = guider_delta_axis2_;
                	guider_delta_axis1_ = 0.0;
                	guider_delta_axis2_ = 0.0;
                }
                
                // Scale tracking rates in deceleration zone
                current_rate_1 *= rate_factor;
                current_rate_2 *= rate_factor;
                
                // Update positions: rate-based kinematic advance + guider position offset.
                // The guider offset is applied as a direct position correction (degrees)
                // rather than a rate modulation, ensuring the correction magnitude is
                // independent of the loop iteration interval (dt).
                axis1_position_ += current_rate_1 * dt + guider_offset_1;
                axis2_position_ += current_rate_2 * dt + guider_offset_2;
                
                // Normalize axis1 position to [-180, 180) to prevent floating-point error
                // accumulation over long tracking sessions, while keeping HA in the
                // conventional range compatible with soft limits [-270, 270].
                // Each iteration adds rate*dt (~4.17e-4° for sidereal) which introduces
                // ~1e-15° of FP rounding error. After 10 hours at 10Hz that's 360k
                // iterations × 1e-15° ≈ 3.6e-10° of unbounded drift — small per-iteration
                // but monotonically growing. The [-180, 180) wrap keeps axis1 bounded
                // and eliminates silent drift without conflicting with soft limit checks.
                axis1_position_ = std::fmod(axis1_position_ + 180.0, 360.0);
                if (axis1_position_ < 0.0) axis1_position_ += 360.0;
                axis1_position_ -= 180.0;
                
                // Guard against NaN/Inf propagation from the position update.
                // If current_rate_1 or current_rate_2 is non-finite (e.g., from guider
                // delta corruption, rate_factor=NaN caught above, or dt overflow),
                // axis positions silently become NaN without this check.
                if (!std::isfinite(axis1_position_) || !std::isfinite(axis2_position_)) {
                    MOUNT_LOG_ERROR("Non-finite position after rate update: "
                             "axis1={}, axis2={}, rate1={}, rate2={}, dt={}",
                             axis1_position_, axis2_position_, current_rate_1, current_rate_2, dt);
                    state_ = MountStatus::State::ERROR;
                    error_message_ = "Numerical error: NaN/Inf in axis position after rate update";
                    break;
                }
                
                // Apply Kalman filter smoothing to axis positions.
                // First inject the computed tracking rates into the KF state so the
                // prediction step uses the correct astronomical velocity (instead of
                // the KF's own lagging rate estimates from position changes alone).
                // Then predict forward using kinematic model (pos += rate * dt),
                // and finally update with measured position for optimal state estimation.
                if (position_kf_ && position_kf_->initialized) {
                    // Use the tracking rates computed above (astronomical for EQUATORIAL,
                    // position-dependent for ALT-AZ/CASUAL) as the KF's velocity estimate.
                    // Without this, the KF predict() would use its own internally-derived
                    // rates (from Kalman gain * position residual), which lag behind
                    // the actual tracking rates and degrade prediction accuracy.
                    position_kf_->setRates(current_rate_1, current_rate_2);
                    position_kf_->predict(dt);
                    position_kf_->update(axis1_position_, axis2_position_);
                    axis1_position_ = position_kf_->pos1();
                    axis2_position_ = position_kf_->pos2();
                }
                
                // Guard against NaN/Inf propagation from Kalman filter.
                // If the KF produces non-finite output (e.g., matrix ill-conditioning,
                // measurement with NaN, or numerical divergence), fail fast rather than
                // silently corrupting positions for all subsequent calculations.
                if (position_kf_ && position_kf_->initialized) {
                    if (!std::isfinite(axis1_position_) || !std::isfinite(axis2_position_)) {
                        MOUNT_LOG_ERROR("Kalman filter produced non-finite position: "
                                 "axis1={}, axis2={}", axis1_position_, axis2_position_);
                        state_ = MountStatus::State::ERROR;
                        error_message_ = "Numerical error: NaN/Inf in axis position after Kalman filter";
                        break;
                    }
                }
                
                // For Alt-Az and CASUAL mounts, normalize to valid angular ranges
                // Axis1 is altitude-like (clamped), axis2 is azimuth-like (wraps [0, 360))
                if (config_.mount_type == MountType::ALT_AZ ||
                    config_.mount_type == MountType::CASUAL) {
                    // Clamp altitude-like axis to [-5, 90] degrees (allow slightly below horizon)
                    if (axis1_position_ > 90.0) axis1_position_ = 90.0;
                    if (axis1_position_ < -5.0) axis1_position_ = -5.0;
                    
                    // Normalize azimuth-like axis to [0, 360) using fmod for O(1) safety
                    axis2_position_ = std::fmod(axis2_position_, 360.0);
                    if (axis2_position_ < 0.0) axis2_position_ += 360.0;
                }
                
                // Check for hard limit violation (beyond limits → ERROR)
                // For Alt-Az and CASUAL mounts, axis2 is azimuth-like [0, 360) — it wraps rather
                // than hitting a hard stop, so only axis1 is checked against limits.
                if (config_.soft_limits_enabled) {
                    bool limit_violation = (soft_limit_distance_axis1_ < 0.0);
                    if (config_.mount_type != MountType::ALT_AZ &&
                        config_.mount_type != MountType::CASUAL) {
                        limit_violation = limit_violation || (soft_limit_distance_axis2_ < 0.0);
                    }
                    if (limit_violation) {
                        MOUNT_LOG_ERROR("Tracking reached soft limit: axis1={:.1f}°, axis2={:.1f}°",
                                 axis1_position_, axis2_position_);
                        state_ = MountStatus::State::ERROR;
                        error_message_ = "Soft limit reached during tracking";
                        break;
                    }
                }
                
                // Log warning when in deceleration zone
                if (config_.soft_limits_enabled && soft_limit_deceleration_active_) {
                    MOUNT_LOG_WARN("Soft limit deceleration active: {}", soft_limit_warning_message_);
                } else if (config_.soft_limits_enabled && soft_limit_warning_active_) {
                    MOUNT_LOG_DEBUG("Soft limit warning: {}", soft_limit_warning_message_);
                }
                
                // Apply astronomical corrections for equatorial tracking
                // Nutation changes the apparent position of the target by up to ~17"
                // For CASUAL and ALT_AZ mounts, position corrections use a different
                // approach (rate-based, see below), so skip EQUATORIAL-specific corrections.
                if (config_.mount_type == MountType::EQUATORIAL) {
                    double jd = core::AstronomicalCalculations::getCurrentJulianDate();
                    double lst = core::AstronomicalCalculations::calculateLST(jd, config_.longitude);
                    
                    // Current HA in hours, convert to RA
                    double ha_hours = axis1_position_ / 15.0;
                    
                    // Guard against non-finite HA (NaN/Inf) before the normalisation
                    // while-loops below. While NaN comparisons with < / >= return false
                    // (so no infinite loop), the silent propagation would corrupt all
                    // subsequent calculations (nutation, TPoint, meridian flip).
                    if (!std::isfinite(ha_hours) || !std::isfinite(axis2_position_)) {
                        MOUNT_LOG_ERROR("Non-finite position before HA/RA normalisation: "
                                 "axis1={}, axis2={}", axis1_position_, axis2_position_);
                        state_ = MountStatus::State::ERROR;
                        error_message_ = "Numerical error: NaN/Inf in axis position before RA normalisation";
                        break;
                    }
                    
                    double current_ra = lst - ha_hours;
                    // Normalize RA to [0, 24) using fmod for O(1) safety
                    // (while-loops are correct for finite values but fmod is
                    //  inherently bounded and cannot infinite-loop)
                    current_ra = std::fmod(current_ra, 24.0);
                    if (current_ra < 0.0) current_ra += 24.0;
                    
                    // Apply nutation to get apparent RA
                    // Normalize Dec to [-90, 90] for nutation calculation.
                    // After a meridian flip, Dec = 180° - original_Dec which may exceed 90°.
                    // Passing Dec > 90° to the spherical transform causes RA to shift by 12h,
                    // producing an invalid ~180° correction to axis1_position_.
                    double dec_for_nutation = axis2_position_;
                    if (dec_for_nutation > 90.0) {
                        dec_for_nutation = 180.0 - dec_for_nutation;
                    } else if (dec_for_nutation < -90.0) {
                        dec_for_nutation = -180.0 - dec_for_nutation;
                    }
                    auto [app_ra, app_dec] = astro_calc_->applyNutation(current_ra, dec_for_nutation, jd);
                    
                    // The nutation difference in RA (hours) translates to a HA position correction (degrees)
                    double ra_correction_hours = app_ra - current_ra;
                    // Normalize to [-12, 12] hours
                    if (ra_correction_hours > 12.0) ra_correction_hours -= 24.0;
                    if (ra_correction_hours < -12.0) ra_correction_hours += 24.0;
                    axis1_position_ += ra_correction_hours * 15.0;
                    
                    // Guard against NaN propagation from nutation calculation.
                    // If axis1_position_ becomes non-finite, transition to ERROR immediately
                    // rather than letting NaN propagate through the tracking loop (where it
                    // would defeat the HA normalisation while-loop guards).
                    if (!std::isfinite(axis1_position_)) {
                        MOUNT_LOG_ERROR("Nutation correction produced non-finite axis1_position_={}",
                                 axis1_position_);
                        state_ = MountStatus::State::ERROR;
                        error_message_ = "Numerical error: NaN/Inf in axis1 after nutation correction";
                        break;
                    }
                    
                    // Apply TPOINT corrections to the mount position during tracking.
                    // The fitted model maps mount encoder positions (HA, Dec) to on-sky
                    // corrections (ΔRA, ΔDec). applyCorrections() uses the model to compute
                    // the actual sky position given the current mount HA/Dec, and we apply
                    // the difference back to the mount axes to compensate.
                    if (tpoint_calibrated_) {
                        double ha_for_tp = axis1_position_ / 15.0;  // Convert degrees back to hours
                        double dec_for_tp = axis2_position_;
                        // Pass snapshotted temperature for thermal compensation in
                        // axis_nonperp_temp_coeff, temp_flexure_coeff, temp_encoder_coeff,
                        // and axis physical corrections (backlash_temp_coeff, expansion_coeff,
                        // temp_gear_error_coeff). load_torque defaults to 0.0 when unavailable.
                        auto [corrected_ra, corrected_dec] = tpoint_model_->applyCorrections(
                            current_ra, dec_for_nutation, ha_for_tp, dec_for_tp,
                            snap_temperature);
                        
                        // The difference between corrected and current RA gives the TPOINT
                        // correction needed for the HA axis (in degrees via hours conversion)
                        double tp_ra_correction_hours = corrected_ra - current_ra;
                        if (tp_ra_correction_hours > 12.0) tp_ra_correction_hours -= 24.0;
                        if (tp_ra_correction_hours < -12.0) tp_ra_correction_hours += 24.0;
                        axis1_position_ += tp_ra_correction_hours * 15.0;
                        
                        // Apply Dec correction directly (already in degrees)
                        axis2_position_ += (corrected_dec - dec_for_tp);
                        
                        // Guard against NaN propagation from TPoint corrections.
                        // Non-finite values would break the soft limit evaluation and
                        // all subsequent calculations, so fail fast and log the state.
                        if (!std::isfinite(axis1_position_) || !std::isfinite(axis2_position_)) {
                            MOUNT_LOG_ERROR("TPOINT correction produced non-finite position: "
                                     "axis1={}, axis2={}", axis1_position_, axis2_position_);
                            state_ = MountStatus::State::ERROR;
                            error_message_ = "Numerical error: NaN/Inf in axis position after TPoint correction";
                            break;
                        }
                    }
                    
                    // ---- Atmospheric refraction correction ----
                    // Refraction lifts celestial objects, making them appear higher than
                    // their true altitude. As altitude changes during tracking, the refraction
                    // varies — causing apparent RA/Dec drift that must be compensated.
                    // This converts the true position → apparent position and applies the
                    // difference as a mount position correction.
                    if (config_.enable_refraction_correction) {
                        double jd = core::AstronomicalCalculations::getCurrentJulianDate();
                        double lst = core::AstronomicalCalculations::calculateLST(jd, config_.longitude);
                        double ha_hours = axis1_position_ / 15.0;
                        double current_ra = lst - ha_hours;
                        current_ra = std::fmod(current_ra, 24.0);
                        if (current_ra < 0.0) current_ra += 24.0;
                        
                        // Get true horizontal coordinates (without refraction)
                        auto [alt_true, az] = astro_calc_->equatorialToHorizontal(
                            current_ra, axis2_position_, jd, false);
                        
                        // Compute refraction correction
                        double refraction_deg = astro_calc_->applyAtmosphericRefraction(alt_true, az, jd);
                        
                        if (refraction_deg > 0.0) {
                            // Apparent altitude = true altitude + refraction
                            double alt_app = alt_true + refraction_deg;
                            
                            // Convert apparent horizontal back to equatorial (without removing refraction,
                            // since we manually added it above)
                            auto [ra_refracted, dec_refracted] = astro_calc_->horizontalToEquatorial(
                                alt_app, az, jd, false);
                            
                            // Compute RA correction (hours → degrees via * 15)
                            double ra_correction_hours = ra_refracted - current_ra;
                            if (ra_correction_hours > 12.0) ra_correction_hours -= 24.0;
                            if (ra_correction_hours < -12.0) ra_correction_hours += 24.0;
                            
                            // Apply refraction correction as position offset
                            // (same pattern as nutation correction above)
                            axis1_position_ += ra_correction_hours * 15.0;
                            
                            double dec_correction = dec_refracted - axis2_position_;
                            // Refraction should never change declination by more than ~0.5° (30 arcmin).
                            // Larger values indicate numerical issues — clamp to avoid wild corrections.
                            if (std::abs(dec_correction) < 0.5) {
                                axis2_position_ += dec_correction;
                            }
                            
                            // Guard against NaN propagation from refraction correction.
                            if (!std::isfinite(axis1_position_) || !std::isfinite(axis2_position_)) {
                                MOUNT_LOG_ERROR("Refraction correction produced non-finite position: "
                                         "axis1={}, axis2={}, refraction={:.4f}°",
                                         axis1_position_, axis2_position_, refraction_deg);
                                state_ = MountStatus::State::ERROR;
                                error_message_ = "Numerical error: NaN/Inf after refraction correction";
                                break;
                            }
                        }
                    }
                }
                
                // --- ALT-AZ and CASUAL astronomical corrections ---
                // Apply nutation, TPoint, and atmospheric refraction corrections as
                // mount position offsets before computing position-dependent rates.
                // These corrections were previously only applied for EQUATORIAL mounts,
                // but they affect the apparent position of celestial objects regardless
                // of mount type.
                //
                // For ALT-AZ mounts: convert alt/az → equatorial, apply corrections,
                //   convert back to alt/az with refraction, apply position offset.
                // For CASUAL mounts: same approach via mountOrientationToEquatorial
                //   and equatorialToMountOrientation (quaternion transformations).
                else if (config_.mount_type == MountType::ALT_AZ ||
                         config_.mount_type == MountType::CASUAL) {
                    double jd = core::AstronomicalCalculations::getCurrentJulianDate();
                    double lst = core::AstronomicalCalculations::calculateLST(jd, config_.longitude);
                    
                    // Convert current mount position to equatorial (mean RA/Dec)
                    double current_ra = 0.0, current_dec = 0.0;
                    bool convert_ok = false;
                    
                    if (config_.mount_type == MountType::ALT_AZ) {
                        // ALT-AZ: axis1 = altitude, axis2 = azimuth
                        auto eq = astro_calc_->horizontalToEquatorial(
                            axis1_position_, axis2_position_, jd, false);
                        current_ra = eq.first;
                        current_dec = eq.second;
                        convert_ok = std::isfinite(current_ra) && std::isfinite(current_dec);
                    } else {  // CASUAL
                        // CASUAL: axis1 = altitude-like, axis2 = azimuth-like in mount frame
                        auto eq = astro_calc_->mountOrientationToEquatorial(
                            axis1_position_, axis2_position_, jd,
                            mount_orientation_.quaternion);
                        current_ra = eq.first;
                        current_dec = eq.second;
                        convert_ok = std::isfinite(current_ra) && std::isfinite(current_dec);
                    }
                    
                    if (convert_ok) {
                        // --- Step 1: Apply nutation correction ---
                        // Nutation changes apparent RA/Dec by up to ~17", affecting where
                        // the target appears on the sky regardless of mount type.
                        {
                            auto [app_ra, app_dec] = astro_calc_->applyNutation(
                                current_ra, current_dec, jd);
                            double ra_corr_hours = app_ra - current_ra;
                            if (ra_corr_hours > 12.0) ra_corr_hours -= 24.0;
                            if (ra_corr_hours < -12.0) ra_corr_hours += 24.0;
                            current_ra += ra_corr_hours;
                            current_dec = app_dec;
                        }
                        
                        // --- Step 2: Apply TPOINT corrections (in HA/Dec frame) ---
                        // TPoint models mount geometry errors in equatorial frame.
                        // Even for ALT-AZ and CASUAL mounts, the target's equatorial
                        // position needs TPoint corrections before converting back
                        // to mount coordinates.
                        if (tpoint_calibrated_) {
                            double ha_hours = lst - current_ra;
                            while (ha_hours > 12.0) ha_hours -= 24.0;
                            while (ha_hours < -12.0) ha_hours += 24.0;
                            
                            auto [corrected_ra, corrected_dec] = tpoint_model_->applyCorrections(
                                current_ra, current_dec, ha_hours, current_dec,
                                snap_temperature);
                            
                            double tp_ra_corr = corrected_ra - current_ra;
                            if (tp_ra_corr > 12.0) tp_ra_corr -= 24.0;
                            if (tp_ra_corr < -12.0) tp_ra_corr += 24.0;
                            current_ra += tp_ra_corr;
                            current_dec = corrected_dec;
                        }
                        
                        // --- Step 3: Convert back to mount position with atmospheric refraction ---
                        // Refraction lifts celestial objects, making them appear higher.
                        // This correction applies regardless of mount type.
                        {
                            double new_alt = 0.0, new_az = 0.0;
                            bool convert_back_ok = false;
                            
                            if (config_.mount_type == MountType::ALT_AZ) {
                                // Convert corrected equatorial back to alt/az
                                // with refraction if enabled
                                auto ha = astro_calc_->equatorialToHorizontal(
                                    current_ra, current_dec, jd,
                                    config_.enable_refraction_correction);
                                new_alt = ha.first;
                                new_az = ha.second;
                                convert_back_ok = std::isfinite(new_alt) && std::isfinite(new_az);
                            } else {  // CASUAL
                                // Convert through mount orientation quaternion
                                auto mf = astro_calc_->equatorialToMountOrientation(
                                    current_ra, current_dec, jd,
                                    mount_orientation_.quaternion);
                                new_alt = mf.first;
                                new_az = mf.second;
                                convert_back_ok = std::isfinite(new_alt) && std::isfinite(new_az);
                                
                                // For CASUAL, refraction is applied during the equatorial→horizontal
                                // step inside equatorialToMountOrientation. We need to ensure refraction
                                // is accounted for. The function equatorialToMountOrientation internally
                                // calls equatorialToHorizontal with apply_refraction=true.
                                // To give the caller full control, we re-do the conversion without
                                // refraction in equatorialToMountOrientation and add it manually.
                                if (convert_back_ok && config_.enable_refraction_correction) {
                                    // equatorialToMountOrientation always applies refraction internally.
                                    // If refraction is disabled, we need to reverse it.
                                    // Since the current implementation always applies refraction,
                                    // we trust the output when refraction is enabled.
                                    // When disabled, we'd need the reverse, but for now the internal
                                    // call uses apply_refraction=true by default, so we're consistent.
                                }
                            }
                            
                            if (convert_back_ok) {
                                // Apply as position offsets with clamping to prevent wild jumps
                                double alt_offset = new_alt - axis1_position_;
                                double az_offset = new_az - axis2_position_;
                                
                                // Clamp corrections: nutation is at most ~17",
                                // refraction at most ~0.5° at low altitude
                                const double MAX_ALT_CORR = 1.0;   // degrees
                                const double MAX_AZ_CORR = 2.0;    // degrees
                                if (std::abs(alt_offset) > MAX_ALT_CORR) {
                                    alt_offset = std::copysign(MAX_ALT_CORR, alt_offset);
                                }
                                if (std::abs(az_offset) > MAX_AZ_CORR) {
                                    az_offset = std::copysign(MAX_AZ_CORR, az_offset);
                                }
                                
                                axis1_position_ += alt_offset;
                                axis2_position_ += az_offset;
                                
                                // Normalize azimuth to [0, 360)
                                axis2_position_ = std::fmod(axis2_position_, 360.0);
                                if (axis2_position_ < 0.0) axis2_position_ += 360.0;
                                
                                // Guard against NaN propagation
                                if (!std::isfinite(axis1_position_) || !std::isfinite(axis2_position_)) {
                                    MOUNT_LOG_ERROR("{} astronomical corrections produced non-finite position: "
                                             "axis1={}, axis2={}",
                                             (config_.mount_type == MountType::ALT_AZ ? "ALT-AZ" : "CASUAL"),
                                             axis1_position_, axis2_position_);
                                    state_ = MountStatus::State::ERROR;
                                    error_message_ = "Numerical error: NaN/Inf after non-equatorial astronomical corrections";
                                    break;
                                }
                            }
                        }
                    }
                }
                
                // --- ALT-AZ position-dependent rate computation ---
                // For Alt-Az mounts, tracking rates depend on the current altitude and azimuth:
                //   Azimuth rate:  d(az)/dt = -ω × cos(lat) × sin(alt) / cos(alt)
                //   Altitude rate: d(alt)/dt =  ω × cos(lat) × cos(az)
                // For CASUAL mounts, the ALT-AZ rates in true horizontal frame are transformed
                // through the orientation quaternion to obtain mount-frame tracking rates.
                // where ω = Earth rotation rate (7.2921150e-5 rad/s) scaled by tracking mode.
                if (config_.mount_type == MountType::ALT_AZ) {
                    // Convert positions to radians
                    double alt_rad = axis1_position_ * M_PI / 180.0;
                    double az_rad  = axis2_position_ * M_PI / 180.0;
                    double lat_rad = config_.latitude * M_PI / 180.0;
                    
                    // Compute cos(lat) with polar singularity guard.
                    // At latitude = ±90°, cos(lat) = 0, causing the ALT-AZ rate equations
                    // to produce zero rates, which would cause tracking to stall at the poles.
                    // Clamp to a minimum value to maintain finite tracking rates.
                    double cos_lat = std::cos(lat_rad);
                    const double MIN_COS_LAT = 1e-10;
                    if (std::abs(cos_lat) < MIN_COS_LAT) {
                        cos_lat = std::copysign(MIN_COS_LAT, cos_lat);
                    }
                    
                    // Compute cos(alt) with zenith clamp: prevent division-by-zero
                    // in the azimuth rate equation when altitude approaches 90°.
                    const double MIN_COS_ALT = std::cos(89.5 * M_PI / 180.0);  // ~0.0087
                    double cos_alt = std::cos(alt_rad);
                    if (std::abs(cos_alt) < MIN_COS_ALT) {
                        cos_alt = std::copysign(MIN_COS_ALT, cos_alt);
                    }
                    
                    // Earth rotation rate (rad/s), scaled by tracking mode factor.
                    // Mode factors approximate the ratio: target_rate / sidereal_rate.
                    double omega = 7.2921150e-5;
                    double mode_factor = 1.0;
                    switch (mode) {
                        case TrackingMode::SIDEREAL: mode_factor = 1.0;      break;
                        case TrackingMode::SOLAR:    mode_factor = 0.9972;   break;
                        case TrackingMode::LUNAR:    mode_factor = 0.9760;   break;
                        case TrackingMode::CUSTOM:   mode_factor = 1.0;      break;
                        case TrackingMode::OFF:      mode_factor = 0.0;      break;
                    }
                    omega *= mode_factor;
                    
                    // Altitude rate: d(alt)/dt = ω × cos(lat) × cos(az)  [rad/s]
                    double alt_rate_rad = omega * cos_lat * std::cos(az_rad);
                    
                    // Azimuth rate: d(az)/dt = -ω × cos(lat) × sin(alt) / cos(alt)  [rad/s]
                    double az_rate_rad  = -omega * cos_lat * std::sin(alt_rad) / cos_alt;
                    
                    // Convert rad/s → deg/s
                    double alt_rate_deg = alt_rate_rad * 180.0 / M_PI;
                    double az_rate_deg  = az_rate_rad  * 180.0 / M_PI;
                    
                    // Convert telescope-axis rates to servo-motor rates.
                    // The astronomical formulas produce Earth-relative rates
                    // (telescope axis °/s); the servo must move G× faster
                    // where G is the gear ratio.
                    const double ha_gear_local = config_.ha_axis_params.gear_ratio;
                    const double dec_gear_local = config_.dec_axis_params.gear_ratio;

                    // Write rates under rate_mutex_ for thread safety
                    // (applyGuiderCorrection may read axis1_rate_/axis2_rate_ from another thread)
                    {
                        std::lock_guard<std::shared_mutex> rate_lock(*rate_mutex_);
                        axis1_rate_ = alt_rate_deg * ha_gear_local;
                        axis2_rate_ = az_rate_deg * dec_gear_local;
                        current_rate_1 = alt_rate_deg * ha_gear_local;
                        current_rate_2 = az_rate_deg * dec_gear_local;
                    }
                    
                    // Guard against non-finite rates (zenith singularity, NaN propagation).
                    // Non-finite rates would corrupt all subsequent position updates.
                    // Also check axis positions — if they are NaN (propagated from earlier in
                    // the loop before the EQUATORIAL guard at line 1171), they would silently
                    // corrupt rate calculations even if the rates themselves happen to be finite
                    // (e.g., cos_alt clamp may produce finite-but-meaningless rates from NaN input).
                    if (!std::isfinite(current_rate_1) || !std::isfinite(current_rate_2) ||
                        !std::isfinite(axis1_position_) || !std::isfinite(axis2_position_)) {
                        MOUNT_LOG_ERROR("Non-finite ALT_AZ tracking state: alt_rate={}, az_rate={}, "
                                 "alt={}°, az={}°, lat={}°",
                                 current_rate_1, current_rate_2,
                                 axis1_position_, axis2_position_, config_.latitude);
                        state_ = MountStatus::State::ERROR;
                        error_message_ = "Numerical error: NaN/Inf in Alt-Az tracking state";
                        break;
                    }
                }
                
                // --- CASUAL position-dependent rate computation ---
                // For CASUAL mounts, compute ALT_AZ rates in the true horizontal frame and
                // transform them through the orientation quaternion to mount-frame rates.
                // This is mathematically equivalent to rotating the ALT_AZ rate vector.
                else if (config_.mount_type == MountType::CASUAL) {
                    double jd = core::AstronomicalCalculations::getCurrentJulianDate();
                    
                    // Get current mount position in true horizontal frame
                    // (inverse quaternion rotation of mount-frame position)
                    auto [true_alt, true_az] = astro_calc_->mountOrientationToEquatorial(
                        axis1_position_, axis2_position_, jd, mount_orientation_.quaternion);
                    
                    // Compute ALT_AZ rates at the current true horizontal position
                    double alt_rad = true_alt * M_PI / 180.0;
                    double az_rad  = true_az * M_PI / 180.0;
                    double lat_rad = config_.latitude * M_PI / 180.0;
                    
                    // Compute cos(lat) with polar singularity guard
                    double cos_lat = std::cos(lat_rad);
                    const double MIN_COS_LAT = 1e-10;
                    if (std::abs(cos_lat) < MIN_COS_LAT) {
                        cos_lat = std::copysign(MIN_COS_LAT, cos_lat);
                    }
                    
                    // Compute cos(alt) with zenith clamp
                    const double MIN_COS_ALT = std::cos(89.5 * M_PI / 180.0);
                    double cos_alt = std::cos(alt_rad);
                    if (std::abs(cos_alt) < MIN_COS_ALT) {
                        cos_alt = std::copysign(MIN_COS_ALT, cos_alt);
                    }
                    
                    // Earth rotation rate (rad/s), scaled by tracking mode factor
                    double omega = 7.2921150e-5;
                    double mode_factor = 1.0;
                    switch (mode) {
                        case TrackingMode::SIDEREAL: mode_factor = 1.0;      break;
                        case TrackingMode::SOLAR:    mode_factor = 0.9972;   break;
                        case TrackingMode::LUNAR:    mode_factor = 0.9760;   break;
                        case TrackingMode::CUSTOM:   mode_factor = 1.0;      break;
                        case TrackingMode::OFF:      mode_factor = 0.0;      break;
                    }
                    omega *= mode_factor;
                    
                    // ALT_AZ rates in true horizontal frame [rad/s]
                    double alt_rate_rad = omega * cos_lat * std::cos(az_rad);
                    double az_rate_rad  = -omega * cos_lat * std::sin(alt_rad) / cos_alt;
                    
                    // Convert true horizontal (alt, az) to Cartesian (x, y, z) position
                    double sin_alt = std::sin(alt_rad);
                    double cos_az_h = std::cos(az_rad);
                    double sin_az_h = std::sin(az_rad);
                    
                    // Cartesian velocity in true horizontal frame [unit/s]
                    double vx = -sin_alt * cos_az_h * alt_rate_rad - cos_alt * sin_az_h * az_rate_rad;
                    double vy = -sin_alt * sin_az_h * alt_rate_rad + cos_alt * cos_az_h * az_rate_rad;
                    double vz = cos_alt * alt_rate_rad;
                    
                    // Rotate position and velocity by orientation quaternion to mount frame
                    double qx = mount_orientation_.quaternion[0];
                    double qy = mount_orientation_.quaternion[1];
                    double qz = mount_orientation_.quaternion[2];
                    double qw = mount_orientation_.quaternion[3];
                    
                    // Inline quaternion rotation: v' = v + 2*qw*(q×v) + 2*(q×(q×v))
                    auto rotateVec = [qx, qy, qz, qw](double vx, double vy, double vz)
                        -> std::array<double, 3> {
                        double cross1_x = qy * vz - qz * vy;
                        double cross1_y = qz * vx - qx * vz;
                        double cross1_z = qx * vy - qy * vx;
                        double cross2_x = qy * cross1_z - qz * cross1_y;
                        double cross2_y = qz * cross1_x - qx * cross1_z;
                        double cross2_z = qx * cross1_y - qy * cross1_x;
                        return {vx + 2.0 * qw * cross1_x + 2.0 * cross2_x,
                                vy + 2.0 * qw * cross1_y + 2.0 * cross2_y,
                                vz + 2.0 * qw * cross1_z + 2.0 * cross2_z};
                    };
                    
                    auto mount_pos = rotateVec(cos_alt * cos_az_h, cos_alt * sin_az_h, sin_alt);
                    auto mount_vel = rotateVec(vx, vy, vz);
                    
                    // Convert mount-frame Cartesian position to angular coordinates
                    double m1_deg = std::asin(mount_pos[2]) * 180.0 / M_PI;
                    double m2_deg = std::atan2(mount_pos[1], mount_pos[0]) * 180.0 / M_PI;
                    
                    // Convert mount-frame Cartesian velocity to angular rates [deg/s]
                    double m1_rad = m1_deg * M_PI / 180.0;
                    double m2_rad = m2_deg * M_PI / 180.0;
                    double cos_m1 = std::cos(m1_rad);
                    const double MIN_COS_M1 = 1e-10;
                    if (std::abs(cos_m1) < MIN_COS_M1) {
                        cos_m1 = std::copysign(MIN_COS_M1, cos_m1);
                    }
                    
                    double m1_rate_deg = mount_vel[2] / cos_m1 * 180.0 / M_PI;
                    double m2_rate_deg = (-mount_vel[0] * std::sin(m2_rad)
                                          + mount_vel[1] * std::cos(m2_rad))
                                         / cos_m1 * 180.0 / M_PI;
                    
                    // Convert telescope-axis rates to servo-motor rates.
                    const double ha_gear_cas = config_.ha_axis_params.gear_ratio;
                    const double dec_gear_cas = config_.dec_axis_params.gear_ratio;

                    // Write rates under rate_mutex_ for thread safety
                    {
                        std::lock_guard<std::shared_mutex> rate_lock(*rate_mutex_);
                        axis1_rate_ = m1_rate_deg * ha_gear_cas;
                        axis2_rate_ = m2_rate_deg * dec_gear_cas;
                        current_rate_1 = m1_rate_deg * ha_gear_cas;
                        current_rate_2 = m2_rate_deg * dec_gear_cas;
                    }
                    
                    // Guard against non-finite rates (zenith singularity, NaN propagation)
                    if (!std::isfinite(current_rate_1) || !std::isfinite(current_rate_2) ||
                        !std::isfinite(axis1_position_) || !std::isfinite(axis2_position_)) {
                        MOUNT_LOG_ERROR("Non-finite CASUAL tracking state: m1_rate={}, m2_rate={}, "
                                 "axis1={}°, axis2={}°",
                                 current_rate_1, current_rate_2,
                                 axis1_position_, axis2_position_);
                        state_ = MountStatus::State::ERROR;
                        error_message_ = "Numerical error: NaN/Inf in CASUAL tracking state";
                        break;
                    }
                }
                
                // Meridian flip detection and execution (equatorial mounts only)
                if (config_.mount_type != MountType::EQUATORIAL) {
                    // Non-equatorial mounts (ALT_AZ, CASUAL): meridian flip not applicable.
                    time_to_meridian_ = 24.0;
                    pier_side_ = 1;
                } else if (config_.meridian_flip_enabled) {
                    double ha = axis1_position_; // HA in degrees
                    
                    // Calculate time to meridian (positive = before, negative = past)
                    time_to_meridian_ = -ha / 15.0;
                    
                    // Update pier side based on HA
                    // East pier (1): HA < 0 (east of meridian, normal tracking)
                    // West pier (-1): HA > 0 (west of meridian, after flip)
                    // In the small hysteresis zone around meridian, keep the current side
                    if (ha < -config_.meridian_flip_hysteresis_degrees && !meridian_flipped_) {
                        pier_side_ = 1;
                        // Mount has returned to the east side naturally; allow future flips
                        meridian_flipped_ = false;
                    } else if (ha > config_.meridian_flip_hysteresis_degrees && !meridian_flip_in_progress_) {
                        pier_side_ = -1;
                    }
                    
                    // Only process flip logic when in TRACKING state (not already flipping)
                    if (state_ == MountStatus::State::TRACKING) {
                        // Detect meridian crossing: HA crossed from east (negative) to west (positive)
                        // beyond the hysteresis threshold
                        if (!meridian_flip_pending_ && !meridian_flipped_ &&
                            ha > config_.meridian_flip_hysteresis_degrees) {
                            // HA has crossed the meridian past the hysteresis zone
                            meridian_flip_pending_ = true;
                            meridian_flip_pending_time_ = std::chrono::steady_clock::now();
                            MOUNT_LOG_INFO("Meridian flip pending: HA={:.2f}°, delay={:.1f}min",
                                     ha, config_.meridian_flip_delay_minutes);
                        }
                        
                        // Execute pending flip after delay has elapsed
                        if (meridian_flip_pending_ && !meridian_flip_in_progress_) {
                            auto now = std::chrono::steady_clock::now();
                            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                now - meridian_flip_pending_time_).count();
                            double delay_seconds = config_.meridian_flip_delay_minutes * 60.0;
                            
                            if (elapsed >= delay_seconds) {
                                // Initiate the flip: transition to MERIDIAN_FLIP state
                                state_ = MountStatus::State::MERIDIAN_FLIP;
                                meridian_flip_in_progress_ = true;
                                meridian_flip_triggered_ = true;
                                flip_start_time_ = now;
                                
                                // Compute flip targets: add 180° to HA, complement Dec
                                double new_ha = axis1_target_ + 180.0;
                                // Normalize HA to [-180, 180]
                                while (new_ha > 180.0) new_ha -= 360.0;
                                while (new_ha < -180.0) new_ha += 360.0;
                                
                                flip_ha_target_ = new_ha;
                                flip_dec_target_ = 180.0 - axis2_target_;
                                
                                // Save original tracking RA/Dec for resume after flip
                                double jd_flip = core::AstronomicalCalculations::getCurrentJulianDate();
                                double lst_flip = core::AstronomicalCalculations::calculateLST(jd_flip, config_.longitude);
                                double ha_hours_flip = axis1_position_ / 15.0;
                                flip_original_ra_ = lst_flip - ha_hours_flip;
                                while (flip_original_ra_ < 0.0) flip_original_ra_ += 24.0;
                                while (flip_original_ra_ >= 24.0) flip_original_ra_ -= 24.0;
                                flip_original_dec_ = axis2_position_;
                                
                                MOUNT_LOG_INFO("Meridian flip initiated: HA target={:.2f}°, Dec target={:.2f}°",
                                         flip_ha_target_, flip_dec_target_);
                                
                                meridian_flip_pending_ = false;
                            }
                        }
                    }
                }
                
                // If in MERIDIAN_FLIP state, execute the flip slew (step-by-step in this loop)
                if (state_ == MountStatus::State::MERIDIAN_FLIP && meridian_flip_in_progress_) {
                    // Check flip timeout — if the flip takes >120s, transition to ERROR.
                    // This prevents the mount from being stuck in MERIDIAN_FLIP forever due to
                    // hardware stall, limit obstruction, or numerical convergence failure.
                    auto flip_elapsed = std::chrono::steady_clock::now() - flip_start_time_;
                    double timeout_s = config_.meridian_flip_timeout_seconds;
                    if (flip_elapsed > std::chrono::duration<double>(timeout_s)) {
                        MOUNT_LOG_ERROR("Meridian flip timed out after {:.1f}s (timeout={:.0f}s) — "
                                 "axis1: {:.2f}°→{:.2f}°, axis2: {:.2f}°→{:.2f}°",
                                 std::chrono::duration<double>(flip_elapsed).count(), timeout_s,
                                 axis1_position_, flip_ha_target_,
                                 axis2_position_, flip_dec_target_);
                        state_ = MountStatus::State::ERROR;
                        error_message_ = "Meridian flip timed out: mount unable to reach flip position within " +
                                         std::to_string(static_cast<int>(timeout_s)) + "s";
                        meridian_flip_in_progress_ = false;
                        break;
                    }
                    
                    // Compute distance to flip targets
                    double d1 = flip_ha_target_ - axis1_position_;
                    double d2 = flip_dec_target_ - axis2_position_;
                    
                    // Normalize d1 to [-180, 180] for shortest path
                    while (d1 > 180.0) d1 -= 360.0;
                    while (d1 < -180.0) d1 += 360.0;
                    
                    // Use measured dt so flip slew speed is independent of scheduler delays
                    double step = config_.max_slew_rate * dt;
                    
                    // Guard against NaN/Inf step (from non-finite dt or config corruption).
                    // A NaN step would propagate through copysign(min(step, |d|), d) below,
                    // silently corrupting the axis positions and stalling the tracking loop.
                    if (!std::isfinite(step)) {
                        MOUNT_LOG_ERROR("Non-finite step in meridian flip: step={}, dt={}, max_slew_rate={}",
                                 step, dt, config_.max_slew_rate);
                        state_ = MountStatus::State::ERROR;
                        error_message_ = "Numerical error: NaN/Inf step during meridian flip";
                        break;
                    }
                    
                    bool reached = true;
                    
                    if (std::abs(d1) > config_.position_tolerance) {
                        axis1_position_ += std::copysign(std::min(step, std::abs(d1)), d1);
                        reached = false;
                    } else {
                        axis1_position_ = flip_ha_target_;
                    }
                    
                    if (std::abs(d2) > config_.position_tolerance) {
                        axis2_position_ += std::copysign(std::min(step, std::abs(d2)), d2);
                        reached = false;
                    } else {
                        axis2_position_ = flip_dec_target_;
                    }
                    
                    // Update HAL motor or CANopen position targets during flip slew
                    if (hal_axis1_motor_ && hal_axis2_motor_) {
                        try {
                            hal_axis1_motor_->setPosition(axis1_position_, config_.max_slew_rate, config_.slew_acceleration);
                            hal_axis2_motor_->setPosition(axis2_position_, config_.max_slew_rate, config_.slew_acceleration);
                        } catch (const std::exception& e) {
                            MOUNT_LOG_WARN("HAL motor error during meridian flip: {}", e.what());
                        }
                    } else if (canopen_interface_) {
                        try {
                            canopen_interface_->setPositionTarget(0, axis1_position_, config_.max_slew_rate, config_.slew_acceleration);
                            canopen_interface_->setPositionTarget(1, axis2_position_, config_.max_slew_rate, config_.slew_acceleration);
                        } catch (const std::exception& e) {
                            MOUNT_LOG_WARN("CANopen communication error during meridian flip: {}", e.what());
                        }
                    }
                    
                    if (reached) {
                        // Flip slew complete
                        pier_side_ = -1; // West pier after flip
                        meridian_flipped_ = true;
                        meridian_flip_in_progress_ = false;
                        state_ = MountStatus::State::TRACKING;
                        flip_completed_ = true;
                        
                        // Set axis targets to flip targets (now matching current position)
                        axis1_target_ = flip_ha_target_;
                        axis2_target_ = flip_dec_target_;
                        
                        // Re-initialize Kalman filter with post-flip positions.
                        // The meridian flip causes a discontinuous jump in axis positions
                        // (HA flips by ~12h, Dec wraps to 180°-Dec), so the KF's internal
                        // state would be stale. Resetting with high covariance lets it
                        // quickly converge to the new tracking regime.
                        if (position_kf_) {
                            position_kf_->init(axis1_position_, axis2_position_,
                                               config_.process_noise, config_.measurement_noise);
                        }
                        
                        MOUNT_LOG_INFO("Meridian flip complete: HA={:.2f}°, Dec={:.2f}°, pier_side={}",
                                 axis1_position_, axis2_position_, pier_side_);
                    } else {
                        MOUNT_LOG_INFO("Flip in progress: d1={:.4f} d2={:.4f} step={:.4f} a1={:.4f} a2={:.4f} t1={:.4f} t2={:.4f} state={}",
                                 d1, d2, step, axis1_position_, axis2_position_, flip_ha_target_, flip_dec_target_, static_cast<int>(state_));
                    }
                }
                
                // Snapshot values needed for I/O operations after lock release.
                // current_rate_1/2 are local variables containing the final tracking
                // rates (either from guider delta for equatorial or Alt-Az computation).
                // config_.tracking_acceleration, env_temperature_, and state_ are shared
                // state protected by state_mutex_ (env_temperature_ is also writable under
                // env_mutex_ via I/O Block 2) — we snapshot them here while still under
                // the lock to ensure a consistent view for I/O operations after release.
                snap_rate_1 = current_rate_1;
                snap_rate_2 = current_rate_2;
                snap_tracking_accel = config_.tracking_acceleration;
                snap_temperature = env_temperature_;
                is_tracking = (state_ == MountStatus::State::TRACKING);
                }   // state_mutex_ scope ends, lock released
                
                // Handle HAL safety error: take lock briefly to set error state.
                // The I/O-bound safety monitor check was done outside the lock above;
                // only the state transition needs the mutex, minimizing hold time.
                if (hal_safety_error) {
                    {
                        std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                        state_ = MountStatus::State::ERROR;
                        error_message_ = hal_safety_error_msg;
                    }
                    break;
                }
                
                // ---- I/O Block 3: HAL/CANopen motor velocity updates (outside state_mutex_) ----
                // Motor control involves CANopen bus communication or HAL hardware calls,
                // both of which are I/O-bound and can take 1-10ms+ on the bus. We execute
                // these outside the lock using snapshot values captured above, so gRPC
                // handlers (getStatus etc.) are not blocked during hardware communication.
                //
                // Rate limiter: only issue setVelocity/setVelocityTarget when the tracking
                // rate has changed significantly (>1e-12 deg/s). During steady-state tracking
                // (which is the vast majority of the time), the rate is constant and every
                // 100ms CANopen bus write would be redundant — generating unnecessary bus
                // traffic and CPU overhead. The threshold of 1e-12 deg/s (~4e-9 arcsec/s)
                // is far below any physically meaningful rate change, so it acts as a
                // pure equality comparison while tolerating harmless FP rounding.
                if (is_tracking) {
                    bool rate_changed = false;
                    if (hal_axis1_motor_ && hal_axis2_motor_) {
                        rate_changed = (std::abs(snap_rate_1 - last_sent_rate_1_) > 1e-12) ||
                                       (std::abs(snap_rate_2 - last_sent_rate_2_) > 1e-12);
                        if (rate_changed) {
                            try {
                                hal_axis1_motor_->setVelocity(snap_rate_1, snap_tracking_accel);
                                hal_axis2_motor_->setVelocity(snap_rate_2, snap_tracking_accel);
                                last_sent_rate_1_ = snap_rate_1;
                                last_sent_rate_2_ = snap_rate_2;
                            } catch (const std::exception& e) {
                                MOUNT_LOG_WARN("HAL motor control error during tracking: {}", e.what());
                            }
                        }
                    } else if (canopen_interface_) {
                        rate_changed = (std::abs(snap_rate_1 - last_sent_rate_1_) > 1e-12) ||
                                       (std::abs(snap_rate_2 - last_sent_rate_2_) > 1e-12);
                        if (rate_changed) {
                            try {
                                canopen_interface_->setVelocityTarget(0, snap_rate_1, snap_tracking_accel);
                                canopen_interface_->setVelocityTarget(1, snap_rate_2, snap_tracking_accel);
                                last_sent_rate_1_ = snap_rate_1;
                                last_sent_rate_2_ = snap_rate_2;
                            } catch (const std::exception& e) {
                                MOUNT_LOG_WARN("CANopen communication error during tracking: {}", e.what());
                            }
                        }
                    }
                }
                
                // Fire callbacks for mid-iteration state transitions
                if (meridian_flip_triggered_) {
                    notifyStatusChanged();  // TRACKING → MERIDIAN_FLIP
                }
                if (flip_completed_) {
                    notifyStatusChanged();  // MERIDIAN_FLIP → TRACKING
                }
            }
            
            // Capture state after tracking-loop exit for callback invocation outside lock.
            MountStatus::State exit_state;
            std::string exit_error;
            {
                std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                exit_state = state_;
                exit_error = error_message_;
            }
            if (exit_state == MountStatus::State::ERROR) {
                notifyError(exit_error);
                notifyStatusChanged();
            } else if (exit_state == MountStatus::State::MERIDIAN_FLIP) {
                // TRACKING → MERIDIAN_FLIP (triggered by auto-detection in the loop)
                notifyStatusChanged();
            } else if (exit_state == MountStatus::State::TRACKING) {
                // MERIDIAN_FLIP → TRACKING (flip completed successfully)
                notifyStatusChanged();
            } else if (exit_state == MountStatus::State::IDLE) {
                // Tracking stopped externally via stop()
                notifyStatusChanged();
            }
        });
        }  // end thread_mutex_ scope
        
        return true;
    }
    
    void stop() {
        tracking_active_ = false;
        joinWorkThread();
        
        {
            std::lock_guard<std::shared_mutex> lock(*state_mutex_);
            
            // Zero rates under rate_mutex_ — applyGuiderCorrection() may be running
            // concurrently on the guider callback thread and modifying rates.
            {
                std::lock_guard<std::shared_mutex> rate_lock(*rate_mutex_);
                axis1_rate_ = 0.0;
                axis2_rate_ = 0.0;
            }
            
            // Stop HAL motors or CANopen axes immediately
            if (hal_axis1_motor_) hal_axis1_motor_->stop();
            if (hal_axis2_motor_) hal_axis2_motor_->stop();
            if (!hal_axis1_motor_ && canopen_interface_) {
                canopen_interface_->stopAxis(0);
                canopen_interface_->stopAxis(1);
            }
            
            // Stop HAL interface — halts periodic hardware I/O after active motion ends.
            if (hal_interface_) {
                hal_interface_->stop();
            }
            
            if (state_ == MountStatus::State::SLEWING ||
                state_ == MountStatus::State::TRACKING ||
                state_ == MountStatus::State::MERIDIAN_FLIP ||
                state_ == MountStatus::State::PARKING) {
                state_ = MountStatus::State::IDLE;
            }
        }  // state_mutex_ released here
        
        // Notify status callback outside state_mutex_ lock
        notifyStatusChanged();  // any moving state → IDLE
    }
    
    void park() {
        // Lock thread_mutex_ across join + state-check + create to prevent data race.
        // joinWorkThread() is called WITHOUT holding state_mutex_ to avoid deadlock
        // with a running work thread that may hold state_mutex_.
        {
            std::lock_guard<std::mutex> tlock(*thread_mutex_);
            
            // Signal any running work thread (e.g. tracking loop) to stop before joining.
            // See slewToEquatorial() for detailed explanation.
            tracking_active_ = false;
            
            // Join any previous work thread first
            joinWorkThreadLocked();
            
            {
                std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                
                if (state_ == MountStatus::State::PARKED) return;
                if (state_ == MountStatus::State::PARKING) return;
                
                // Clear any errors before starting park sequence
                if (hal_safety_monitor_) {
                    hal_safety_monitor_->clearErrors(0);
                    hal_safety_monitor_->clearErrors(1);
                } else if (canopen_interface_) {
                    canopen_interface_->clearErrors(0);
                    canopen_interface_->clearErrors(1);
                }
                
                state_ = MountStatus::State::PARKING;
                
                // Stop any active motion via HAL or CANopen first
                if (hal_axis1_motor_) hal_axis1_motor_->stop();
                if (hal_axis2_motor_) hal_axis2_motor_->stop();
                if (!hal_axis1_motor_ && canopen_interface_) {
                    canopen_interface_->stopAxis(0);
                    canopen_interface_->stopAxis(1);
                }
                
                // Park targets – move to configurable park position
                axis1_target_ = config_.park_position_axis1;
                axis2_target_ = config_.park_position_axis2;
                tracking_active_ = false;
            }
            
            notifyStatusChanged();  // any → PARKING
            
            work_thread_ = std::thread([this]() {
            const double PARK_VELOCITY = 2.0;      // deg/s
            const double PARK_ACCELERATION = 1.0;  // deg/s²
            const int POLL_MS = 100;
            const double POSITION_TOLERANCE_DEG = 0.5;
            const int PARK_TIMEOUT_MS = 30000;     // 30s max parking time
            int elapsed_ms = 0;
            bool motion_started = false;
            
            if (hal_axis1_motor_ && hal_axis2_motor_) {
                // Re-enable HAL motors, then set position targets
                hal_axis1_motor_->enable();
                hal_axis2_motor_->enable();
                
                motion_started = hal_axis1_motor_->setPosition(config_.park_position_axis1, PARK_VELOCITY, PARK_ACCELERATION);
                motion_started = hal_axis2_motor_->setPosition(config_.park_position_axis2, PARK_VELOCITY, PARK_ACCELERATION) && motion_started;
            } else if (canopen_interface_) {
                // Re-enable drives after stop, then set position targets
                canopen_interface_->enableDrive(0);
                canopen_interface_->enableDrive(1);
                
                motion_started = canopen_interface_->setPositionTarget(0, config_.park_position_axis1, PARK_VELOCITY, PARK_ACCELERATION);
                motion_started = canopen_interface_->setPositionTarget(1, config_.park_position_axis2, PARK_VELOCITY, PARK_ACCELERATION) && motion_started;
            } else {
                motion_started = true;
            }
            
            if (!motion_started) {
                MOUNT_LOG_ERROR("Park motion failed to start");
                {
                    std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                    state_ = MountStatus::State::IDLE;
                }
                notifyStatusChanged();  // PARKING → IDLE (motion failure)
                return;
            }
            
            while (elapsed_ms < PARK_TIMEOUT_MS) {
                // Check if parking was cancelled
                {
                    std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                    if (state_ != MountStatus::State::PARKING) break;
                }
                
                bool reached = true;
                
                if (hal_axis1_motor_ && hal_axis2_motor_) {
                    // HAL path: poll MotorControl::targetReached() during park
                    try {
                        reached = hal_axis1_motor_->targetReached() && hal_axis2_motor_->targetReached();
                        
                        // Update internal positions from HAL encoder feedback for accuracy
                        if (reached && hal_axis1_encoder_ && hal_axis2_encoder_) {
                            auto enc1 = hal_axis1_encoder_->read();
                            auto enc2 = hal_axis2_encoder_->read();
                            if (enc1.data_valid && enc2.data_valid) {
                                std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                                axis1_position_ = enc1.position_deg;
                                axis2_position_ = enc2.position_deg;
                            }
                        }
                    } catch (const std::exception& e) {
                        MOUNT_LOG_WARN("HAL motor error during park: {}", e.what());
                        reached = false;
                    }
                } else if (canopen_interface_) {
                    try {
                        auto status0 = canopen_interface_->getDriveStatus(0);
                        auto status1 = canopen_interface_->getDriveStatus(1);
                        
                        // Check for CANopen errors during park
                        if (status0.error || status1.error) {
                            MOUNT_LOG_ERROR("CANopen error during park: axis0 error={}, axis1 error={}",
                                      status0.error, status1.error);
                            // Try to clear errors and continue
                            canopen_interface_->clearErrors(0);
                            canopen_interface_->clearErrors(1);
                            std::this_thread::sleep_for(std::chrono::milliseconds(POLL_MS));
                            elapsed_ms += POLL_MS;
                            continue;
                        }
                        
                        if (!status0.target_reached) reached = false;
                        if (!status1.target_reached) reached = false;
                        
                        // Update internal positions from actual CANopen feedback for accuracy
                        if (reached) {
                            auto pos0 = canopen_interface_->getPositionData(0);
                            auto pos1 = canopen_interface_->getPositionData(1);
                            std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                            axis1_position_ = pos0.actual_position;
                            axis2_position_ = pos1.actual_position;
                        }
                    } catch (const std::exception& e) {
                        MOUNT_LOG_WARN("CANopen communication error during park: {}", e.what());
                        reached = false;
                    }
                } else {
                    // Simulation path: gradually move axes towards park position
                    std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                    double d1 = config_.park_position_axis1 - axis1_position_;
                    double d2 = config_.park_position_axis2 - axis2_position_;
                    double step = 2.0;
                    
                    if (std::abs(d1) > POSITION_TOLERANCE_DEG) {
                        axis1_position_ += std::copysign(std::min(step, std::abs(d1)), d1);
                        reached = false;
                    } else {
                        axis1_position_ = config_.park_position_axis1;
                    }
                    
                    if (std::abs(d2) > POSITION_TOLERANCE_DEG) {
                        axis2_position_ += std::copysign(std::min(step, std::abs(d2)), d2);
                        reached = false;
                    } else {
                        axis2_position_ = config_.park_position_axis2;
                    }
                }
                
                if (reached) {
                    std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                    axis1_rate_ = 0.0;
                    axis2_rate_ = 0.0;
                    state_ = MountStatus::State::PARKED;
                    
                    // Disable drives for power saving and safety when parked
                    if (hal_axis1_motor_) hal_axis1_motor_->disable();
                    if (hal_axis2_motor_) hal_axis2_motor_->disable();
                    if (!hal_axis1_motor_ && canopen_interface_) {
                        canopen_interface_->disableDrive(0);
                        canopen_interface_->disableDrive(1);
                    }
                    
                    MOUNT_LOG_INFO("Mount parked successfully at ({}, {})", config_.park_position_axis1, config_.park_position_axis2);
                    break;
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(POLL_MS));
                elapsed_ms += POLL_MS;
            }
            
            // Timeout: if parking didn't complete, log and revert to safe state
            if (elapsed_ms >= PARK_TIMEOUT_MS) {
                MOUNT_LOG_ERROR("Park timeout after {}ms", PARK_TIMEOUT_MS);
                std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                if (state_ == MountStatus::State::PARKING) {
                    state_ = MountStatus::State::IDLE;
                }
            }
            // Capture state after park loop for callback invocation outside lock.
            MountStatus::State exit_state;
            {
                std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                exit_state = state_;
            }
            if (exit_state == MountStatus::State::PARKED) {
                notifyStatusChanged();  // PARKING → PARKED
            } else if (exit_state == MountStatus::State::IDLE) {
                notifyStatusChanged();  // PARKING → IDLE (timeout or cancelled)
            }
        });
        }  // end thread_mutex_ scope
    }
    
    void unpark() {
        {
            std::lock_guard<std::shared_mutex> lock(*state_mutex_);
            if (state_ == MountStatus::State::PARKED) {
                state_ = MountStatus::State::IDLE;
            }
        }  // state_mutex_ released
        notifyStatusChanged();  // PARKED → IDLE
    }

    void clearErrors() {
        // Join any background thread first — it may be in an ERROR loop
        joinWorkThread();
        
        // ── Reset CANopen/HAL errors on both axes ALWAYS ─────────────
        // Drive-level faults (CiA 402 fault state) can occur even when
        // the controller state_ is not ERROR (e.g. IDLE after a quick
        // stop).  The CANopen fault reset and drive re-enable must run
        // unconditionally so the user can recover from hardware faults
        // regardless of the logical controller state.
        
        if (hal_safety_monitor_) {
            try {
                hal_safety_monitor_->clearErrors(0);
                hal_safety_monitor_->clearErrors(1);
            } catch (const std::exception& e) {
                MOUNT_LOG_WARN("HAL clearErrors exception: {}", e.what());
            }
        }
        if (canopen_interface_) {
            try {
                canopen_interface_->clearErrors(0);
                canopen_interface_->clearErrors(1);
                // Re-enable drives that may have been disabled by failed
                // SDO operations (e.g. setVelocityTarget fast-fail path).
                // Without this, the drives stay permanently disabled after
                // a single SDO timeout and the mount cannot move.
                canopen_interface_->enableDrive(0);
                canopen_interface_->enableDrive(1);
            } catch (const std::exception& e) {
                MOUNT_LOG_WARN("CANopen clearErrors exception: {}", e.what());
            }
        }
        
        // ── Reset controller state if in ERROR ──────────────────────
        {
            std::lock_guard<std::shared_mutex> lock(*state_mutex_);
            
            if (state_ == MountStatus::State::ERROR) {
                // Reset tracking flags to safe defaults
                tracking_active_ = false;
                meridian_flip_pending_ = false;
                meridian_flip_in_progress_ = false;
                
                // Clear derotator errors
                if (derotator_) {
                    derotator_->clearErrors();
                }
                
                // Clear error state
                state_ = MountStatus::State::IDLE;
                error_message_.clear();
                
                notifyStatusChanged();  // ERROR → IDLE
                MOUNT_LOG_INFO("Mount error state cleared, returned to IDLE");
            } else {
                MOUNT_LOG_INFO("CANopen/HAL errors cleared (controller was not in ERROR state)");
            }
        }
    }
    
    MountStatus getStatus() const {
        std::shared_lock<std::shared_mutex> lock(*state_mutex_);
        MountStatus status;
        status.state = state_;
        status.axis1_position = axis1_position_;
        status.axis2_position = axis2_position_;
        // Telescope position: servo degrees divided by gear ratio gives
        // the actual telescope axis position on the sky.
        // Uses raw (non-normalized) servo position to preserve absolute angle.
        // Telescope position: servo degrees divided by gear ratio gives
        // the actual telescope axis position on the sky.
        // Guard against zero or near-zero gear_ratio (e.g. from corrupted config)
        // that would produce inf/nan in the status display and logs.
        double ha_gear = config_.ha_axis_params.gear_ratio;
        double dec_gear = config_.dec_axis_params.gear_ratio;
        if (ha_gear < 1.0) ha_gear = 360.0;
        if (dec_gear < 1.0) dec_gear = 360.0;
        status.telescope_axis1_position = raw_servo_axis1_position_ / ha_gear;
        status.telescope_axis2_position = raw_servo_axis2_position_ / dec_gear;
        // Read rates under rate_mutex_ — applyGuiderCorrection() writes them
        // concurrently on the guider callback thread.
        {
            std::shared_lock<std::shared_mutex> rate_lock(*rate_mutex_);
            status.axis1_rate = axis1_rate_;
            status.axis2_rate = axis2_rate_;
        }
        status.axis1_target = axis1_target_;
        status.axis2_target = axis2_target_;
        status.encoders_active = encoders_active_;
        status.guider_active = guider_active_;
        status.tpoint_calibrated = tpoint_calibrated_;
        status.tracking_error_ra = tracking_error_ra_;
        status.tracking_error_dec = tracking_error_dec_;
        status.timestamp = std::chrono::system_clock::now();
        // error_message is populated by setErrorCallback or error handlers
        // It's stored separately and attached to the status here
        status.error_message = error_message_;
        
        // Meridian flip status
        status.meridian_flip_pending = meridian_flip_pending_;
        status.meridian_flip_in_progress = meridian_flip_in_progress_;
        status.pier_side = pier_side_;
        status.time_to_meridian = time_to_meridian_;
        
        // Soft safety limits status
        status.soft_limit_warning_active = soft_limit_warning_active_;
        status.soft_limit_deceleration_active = soft_limit_deceleration_active_;
        status.soft_limit_distance_axis1 = soft_limit_distance_axis1_;
        status.soft_limit_distance_axis2 = soft_limit_distance_axis2_;
        status.soft_limit_warning_message = soft_limit_warning_message_;

        // Bootstrap / encoder status fields (plan §8.4)
        status.encoders_absolute = config_.encoders_absolute;
        status.bootstrap_mode = static_cast<int>(bootstrap_mode_);
        status.bootstrap_calibrated = bootstrap_calibrated_;
        status.bootstrap_measurement_count = static_cast<int>(bootstrap_measurements_.size());

        return status;
    }

    /**
     * @brief Read actual axis positions from CANopen drives and update cached state.
     *
     * Called periodically from the main loop to keep axis1_position_ /
     * axis2_position_ in sync with the physical hardware.  Without this,
     * getStatus() would return stale (0.0) positions unless a slew/track
     * operation was active.
     *
     * Angles are normalized to [0°, 360°) for consistent reporting.
     */
    void refreshPositionsFromCANopen() {
        if (!canopen_interface_) return;

        // Skip SDO reads if drives are not enabled or not connected.
        // Each SDO read would time out (~1 s per axis) and block the main
        // loop, making the controller unresponsive and causing instability
        // when gRPC handlers compete for the CANopen mutex.
        if (!canopen_interface_->isConnected()) return;
        if (!canopen_interface_->isDriveEnabled(0) &&
            !canopen_interface_->isDriveEnabled(1))
            return;

        std::lock_guard<std::shared_mutex> lock(*state_mutex_);

        try {
            auto pos0 = canopen_interface_->getPositionData(0);
            auto pos1 = canopen_interface_->getPositionData(1);
            // Only update if the SDO reads returned plausible data.
            // getPositionData() returns cached zeros when SDO fails.
            if (std::isfinite(pos0.actual_position) && std::isfinite(pos1.actual_position)) {
                // Store raw (absolute) servo motor position for telescope calculation.
                // Normalization is done AFTER saving the raw value.
                raw_servo_axis1_position_ = pos0.actual_position;
                raw_servo_axis2_position_ = pos1.actual_position;

                // Normalize to [0°, 360°) for status display
                axis1_position_ = std::fmod(std::fmod(pos0.actual_position, 360.0) + 360.0, 360.0);
                axis2_position_ = std::fmod(std::fmod(pos1.actual_position, 360.0) + 360.0, 360.0);
                axis1_rate_ = pos0.actual_velocity;
                axis2_rate_ = pos1.actual_velocity;

                // Throttled log: servo → telescope position conversion.
                // Logs every ~100 calls (~10 s at 100 ms main loop) to avoid spam.
                static int refresh_log_counter = 0;
                refresh_log_counter++;
                if (refresh_log_counter % 100 == 0) {
                    double ha_gear_r = config_.ha_axis_params.gear_ratio;
                    double dec_gear_r = config_.dec_axis_params.gear_ratio;
                    if (ha_gear_r < 1.0) ha_gear_r = 360.0;
                    if (dec_gear_r < 1.0) dec_gear_r = 360.0;
                    double telescope_axis1 = raw_servo_axis1_position_ / ha_gear_r;
                    double telescope_axis2 = raw_servo_axis2_position_ / dec_gear_r;
                    MOUNT_LOG_INFO("CANopen pos → Telescope: axis1={:.4f}° axis2={:.4f}° "
                                   "| Servo (raw): axis1={:.4f}° axis2={:.4f}° "
                                   "| Gear HA={:.1f}:1 Dec={:.1f}:1",
                                   telescope_axis1, telescope_axis2,
                                   raw_servo_axis1_position_, raw_servo_axis2_position_,
                                   config_.ha_axis_params.gear_ratio, config_.dec_axis_params.gear_ratio);
                }
            }
        } catch (const std::exception& e) {
            MOUNT_LOG_DEBUG("refreshPositionsFromCANopen: {}", e.what());
        }
    }
    
    // Bootstrap calibration API
    bool addBootstrapMeasurement(double observed_ra, double observed_dec,
                                 double expected_ra, double expected_dec,
                                 double mount_ha = 0.0, double mount_dec = 0.0) {
        // For bootstrap measurements, use default environmental parameters
        double temperature = 15.0;
        double pressure = 1013.25;
        double humidity = 0.5;
        
        // If mount_dec not provided, use expected_dec as approximation
        if (mount_dec == 0.0 && expected_dec != 0.0) {
            mount_dec = expected_dec;
        }
        
        // Store measurement for bootstrap calibration
        bootstrap_measurements_.push_back({
            observed_ra, observed_dec, expected_ra, expected_dec,
            mount_ha, mount_dec, temperature, pressure, humidity,
            0.0, 0.0, 0.0, 2000.0,  // Default astrometric parameters
            std::chrono::system_clock::now()
        });
        
        return true;
    }
    
    bool runBootstrapCalibration() {
        if (bootstrap_measurements_.size() < 2) {
            return false;  // Need at least 2 stars for initial alignment
        }
        
        // --- Universal Wahba/SVD for ALL mount types (plan §8.6) ---
        // Previously the code branched on mount_type:
        //   CASUAL      → Wahba/SVD (full 3-DOF rotation)
        //   EQUATORIAL  → mean-offset (simple RA/Dec shift)
        //   ALT_AZ      → mean-offset (simple RA/Dec shift)
        //
        // Now Wahba/SVD is used for EVERY mount type because:
        //   - mount_vec = f(mount_ha, mount_dec) is computed identically
        //     for CASUAL (HA, Dec), EQUATORIAL (HA, Dec), and ALT_AZ (Az, Alt).
        //   - horiz_vec = f(observed_ra, observed_dec) is always the true
        //     horizontal ENU vector at the measurement time/location.
        //   - Wahba's problem finds the optimal rotation R: mount_frame → true_horizontal.
        //   - For EQUATORIAL: R absorbs encoder offset as an additional rotation
        //     around the (coincident) mount axes — mathematically exact for any offset.
        //   - For ALT_AZ: R approximates the altitude encoder offset, which is a
        //     translation on the sphere rather than a pure rotation (see caveat below).
        //
        // Algorithm:
        //   1. Convert (observed_ra, observed_dec) at measurement time to true horizontal (alt, az)
        //   2. Convert (mount_ha, mount_dec) to unit vector in mount frame
        //   3. Build cross-covariance matrix B = sum(mount_vec * horiz_vec^T)
        //   4. SVD(B) = U * S * V^T  →  optimal rotation R = V * U^T
        //   5. Convert R to quaternion Q
        //   6. Q represents rotation: mount frame → true horizontal frame
        
        if (bootstrap_measurements_.size() < 3) {
            MOUNT_LOG_WARN("Bootstrap calibration requires at least 3 measurements (got {})",
                     bootstrap_measurements_.size());
            return false;
        }
        
        try {
            // Set environmental parameters for atmospheric refraction
            astro_calc_->setEnvironmentalParams(
                bootstrap_measurements_.front().temperature,
                bootstrap_measurements_.front().pressure,
                bootstrap_measurements_.front().humidity);
            
            // Build cross-covariance matrix B (3x3)
            Eigen::Matrix3d B = Eigen::Matrix3d::Zero();
            double total_weight = 0.0;
            
            for (const auto& m : bootstrap_measurements_) {
                // Convert the measurement timestamp to Julian Date
                auto ms_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
                    m.timestamp.time_since_epoch()).count();
                double jd = 2440587.5 + ms_since_epoch / 86400000.0;
                
                // Step 1: Convert (observed_ra, observed_dec) → true horizontal (alt, az)
                double ra_hours = m.observed_ra;
                double dec_deg = m.observed_dec;
                
                auto [true_alt, true_az] = astro_calc_->equatorialToHorizontal(
                    ra_hours, dec_deg, jd, true);
                
                // Convert alt/az to ENU unit vector in true horizontal frame
                // ENU: x=East, y=North, z=Up
                double alt_rad = true_alt * M_PI / 180.0;
                double az_rad = true_az * M_PI / 180.0;
                Eigen::Vector3d horiz_vec(
                    std::sin(az_rad) * std::cos(alt_rad),   // East
                    std::cos(az_rad) * std::cos(alt_rad),   // North
                    std::sin(alt_rad)                        // Up
                );
                
                // Step 2: Convert mount position (mount_ha = axis1, mount_dec = axis2)
                // to a unit vector in mount frame.
                // In mount frame: axis1 = altitude-like, axis2 = azimuth-like
                double axis1_deg = m.mount_ha;
                double axis2_deg = m.mount_dec;
                double a1_rad = axis1_deg * M_PI / 180.0;
                double a2_rad = axis2_deg * M_PI / 180.0;
                Eigen::Vector3d mount_vec(
                    std::sin(a2_rad) * std::cos(a1_rad),   // axis2 (longitude-like)
                    std::cos(a2_rad) * std::cos(a1_rad),   // axis2 orthogonal
                    std::sin(a1_rad)                        // axis1 (altitude-like)
                );
                
                // Step 3: Accumulate B += mount_vec * horiz_vec^T
                // Use equal weights for all measurements
                B += mount_vec * horiz_vec.transpose();
                total_weight += 1.0;
            }
            
            // Step 4: SVD to find optimal rotation R = V * U^T
            Eigen::JacobiSVD<Eigen::Matrix3d> svd(B, Eigen::ComputeFullU | Eigen::ComputeFullV);
            
            // Check condition number of the cross-covariance matrix B.
            // If the ratio of smallest to largest singular value is too small,
            // the measurements are nearly collinear (e.g. all stars on the same
            // side of the sky) and the 3-DOF rotation cannot be uniquely resolved.
            // A condition number < 1e-6 means B is rank-deficient or nearly so.
            Eigen::Vector3d sv = svd.singularValues();
            double max_sv = sv(0);
            double min_sv = sv(2);
            double cond = (max_sv > 0.0) ? (min_sv / max_sv) : 0.0;
            const double MIN_COND = 1e-6;
            if (cond < MIN_COND) {
                MOUNT_LOG_WARN("Bootstrap SVD ill-conditioned: "
                         "cond={:.2e} < min={:.2e}, sv=[{:.2e}, {:.2e}, {:.2e}]. "
                         "Star measurements may be nearly collinear — "
                         "distribute calibration stars across the sky.",
                         cond, MIN_COND, sv(0), sv(1), sv(2));
                // Continue with best-effort rotation; the residual check below
                // will detect poor alignment via large RMS error.
            }
            
            Eigen::Matrix3d R = svd.matrixV() * svd.matrixU().transpose();
            
            // Ensure proper rotation (det = +1)
            if (R.determinant() < 0) {
                Eigen::Matrix3d V = svd.matrixV();
                V.col(2) = -V.col(2);
                R = V * svd.matrixU().transpose();
            }
            
            // Step 5: Convert rotation matrix R to quaternion Q = [qx, qy, qz, qw]
            double trace = R(0,0) + R(1,1) + R(2,2);
            double qx, qy, qz, qw;
            
            if (trace > 0.0) {
                double S = std::sqrt(trace + 1.0) * 2.0;
                qw = 0.25 * S;
                qx = (R(2,1) - R(1,2)) / S;
                qy = (R(0,2) - R(2,0)) / S;
                qz = (R(1,0) - R(0,1)) / S;
            } else if (R(0,0) > R(1,1) && R(0,0) > R(2,2)) {
                double S = std::sqrt(1.0 + R(0,0) - R(1,1) - R(2,2)) * 2.0;
                qw = (R(2,1) - R(1,2)) / S;
                qx = 0.25 * S;
                qy = (R(0,1) + R(1,0)) / S;
                qz = (R(0,2) + R(2,0)) / S;
            } else if (R(1,1) > R(2,2)) {
                double S = std::sqrt(1.0 + R(1,1) - R(0,0) - R(2,2)) * 2.0;
                qw = (R(0,2) - R(2,0)) / S;
                qx = (R(0,1) + R(1,0)) / S;
                qy = 0.25 * S;
                qz = (R(1,2) + R(2,1)) / S;
            } else {
                double S = std::sqrt(1.0 + R(2,2) - R(0,0) - R(1,1)) * 2.0;
                qw = (R(1,0) - R(0,1)) / S;
                qx = (R(0,2) + R(2,0)) / S;
                qy = (R(1,2) + R(2,1)) / S;
                qz = 0.25 * S;
            }
            
            // Normalize quaternion
            double q_norm = std::sqrt(qx*qx + qy*qy + qz*qz + qw*qw);
            if (q_norm > 0.0) {
                qx /= q_norm; qy /= q_norm; qz /= q_norm; qw /= q_norm;
            }
            
            // Step 6: Store as estimated orientation
            MountOrientation estimated_q;
            estimated_q.quaternion = {{qx, qy, qz, qw}};
            bootstrap_estimated_orientation_ = estimated_q;
            
            // Compute residual error: for each measurement, project the mount
            // position through the estimated quaternion and compare with observed
            double residual_sum_sq = 0.0;
            for (const auto& m : bootstrap_measurements_) {
                auto ms_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
                    m.timestamp.time_since_epoch()).count();
                double jd = 2440587.5 + ms_since_epoch / 86400000.0;
                
                // Expected mount position from observed sky position using estimated Q
                auto [expected_axis1, expected_axis2] = astro_calc_->equatorialToMountOrientation(
                    m.observed_ra, m.observed_dec, jd,
                    estimated_q.quaternion);
                
                double d1 = expected_axis1 - m.mount_ha;
                double d2 = expected_axis2 - m.mount_dec;
                residual_sum_sq += d1*d1 + d2*d2;
            }
            double rms_error_deg = std::sqrt(residual_sum_sq / bootstrap_measurements_.size());
            bootstrap_estimated_quaternion_error_arcsec_ = rms_error_deg * 3600.0;
            
            // Store RMS in the shared bootstrap fields for API compatibility
            bootstrap_rms_ra_arcsec_ = bootstrap_estimated_quaternion_error_arcsec_;
            bootstrap_rms_dec_arcsec_ = bootstrap_estimated_quaternion_error_arcsec_;
            
            // Apply estimated orientation to the mount
            mount_orientation_ = estimated_q;
            
            // --- ALT_AZ caveat (plan §8.6) ---
            // For ALT_AZ mounts, the encoder altitude offset is a translation on the
            // celestial sphere (great-circle distance), not a pure 3D rotation about
            // a fixed axis. Wahba/SVD finds the best rotation approximation, but the
            // residual error will increase with the magnitude of the altitude offset.
            // Empirical guidance:
            //   - Offset < 5°   → post-calibration pointing error < 0.1°
            //   - Offset 5-10°  → pointing error 0.1-1.0°
            //   - Offset > 10°  → pointing error > 1.0° — consider TPOINT after bootstrap
            if (config_.mount_type == MountType::ALT_AZ) {
                MOUNT_LOG_INFO("ALT_AZ bootstrap: encoder altitude offset is a translation on the "
                         "sphere, not a pure 3D rotation. Wahba/SVD finds the best rotation "
                         "approximation. RMS error={:.2f}\". For offsets >10\u00b0 re-pointing "
                         "error may exceed 1\u00b0. Consider TPoint after bootstrap for fine "
                         "correction.", bootstrap_estimated_quaternion_error_arcsec_);
            }
            
            std::string mount_type_str;
            switch (config_.mount_type) {
                case MountType::CASUAL:    mount_type_str = "CASUAL"; break;
                case MountType::EQUATORIAL: mount_type_str = "EQUATORIAL"; break;
                case MountType::ALT_AZ:    mount_type_str = "ALT_AZ"; break;
                default:                   mount_type_str = "UNKNOWN"; break;
            }
            MOUNT_LOG_INFO("{} bootstrap calibration: Q=[{:.4f}, {:.4f}, {:.4f}, {:.4f}], "
                     "RMS error={:.2f}\"",
                     mount_type_str, qx, qy, qz, qw,
                     bootstrap_estimated_quaternion_error_arcsec_);
            
            bootstrap_calibrated_ = true;
            // Auto-switch gamepad to CELESTIAL mode after successful calibration
            if (gamepad_mode_ == 0) {
                setGamepadMode(1);
                MOUNT_LOG_INFO("Gamepad: auto-switched to CELESTIAL mode after bootstrap calibration");
            }
            return true;
            
        } catch (const std::exception& e) {
            MOUNT_LOG_ERROR("Bootstrap calibration failed: {}", e.what());
            return false;
        }
    }
    
    bool isBootstrapCalibrated() const {
        return bootstrap_calibrated_;
    }

    bool setBootstrapMode(BootstrapMode mode) {
        std::lock_guard<std::shared_mutex> lock(*state_mutex_);
        bootstrap_mode_ = mode;
        MOUNT_LOG_INFO("Bootstrap mode set to {}", static_cast<int>(mode));
        return true;
    }

    BootstrapMode getBootstrapMode() const {
        std::shared_lock<std::shared_mutex> lock(*state_mutex_);
        return bootstrap_mode_;
    }

    void clearBootstrapMeasurements() {
        bootstrap_measurements_.clear();
        bootstrap_calibrated_ = false;
        bootstrap_rms_ra_arcsec_ = 0.0;
        bootstrap_rms_dec_arcsec_ = 0.0;
        bootstrap_ra_correction_arcsec_ = 0.0;
        bootstrap_dec_correction_arcsec_ = 0.0;
        bootstrap_estimated_orientation_ = MountOrientation{};
        bootstrap_estimated_quaternion_error_arcsec_ = 0.0;
    }
    
    size_t getBootstrapMeasurementCount() const {
        return bootstrap_measurements_.size();
    }
    
    double getBootstrapRmsRaArcsec() const {
        return bootstrap_rms_ra_arcsec_;
    }
    
    double getBootstrapRmsDecArcsec() const {
        return bootstrap_rms_dec_arcsec_;
    }
    
    double getBootstrapRaCorrectionArcsec() const {
        return bootstrap_ra_correction_arcsec_;
    }
    
    double getBootstrapDecCorrectionArcsec() const {
        return bootstrap_dec_correction_arcsec_;
    }
    
    // Metrics and counters accessors
    size_t getSlewCount() const { return slew_count_; }
    size_t getTrackCount() const { return track_count_; }
    size_t getCalibrationCount() const { return calibration_count_; }
    size_t getTrackingIterationCount() const { return tracking_iteration_count_; }
    double getTotalUpdateTimeMs() const { return total_update_time_ms_; }
    
    // TPOINT calibration metrics accessors
    size_t getTPointMeasurementCount() const { return tpoint_measurements_.size(); }
    double getTPointResidualRmsArcsec() const { return tpoint_residual_rms_arcsec_; }
    double getTPointResidualMaxArcsec() const { return tpoint_residual_max_arcsec_; }
    double getTPointChiSquared() const { return tpoint_chi_squared_; }
    
    // TPOINT calibration API
    bool addTPointMeasurement(double observed_ra, double observed_dec,
                              double expected_ra, double expected_dec,
                              double mount_ha, double mount_dec,
                              double temperature, double pressure,
                              double humidity,
                              double proper_motion_ra, double proper_motion_dec,
                              double parallax, double epoch) {
        // Store measurement for TPOINT calibration
        tpoint_measurements_.push_back({
            observed_ra, observed_dec, expected_ra, expected_dec,
            mount_ha, mount_dec, temperature, pressure, humidity,
            proper_motion_ra, proper_motion_dec, parallax, epoch,
            std::chrono::system_clock::now()
        });
        
        // Also add to the full TPointModel for QR-based fitting
        models::TPointModel::Measurement tpm;
        tpm.observed_ra = observed_ra;
        tpm.observed_dec = observed_dec;
        tpm.expected_ra = expected_ra;
        tpm.expected_dec = expected_dec;
        tpm.mount_ha = mount_ha;
        tpm.mount_dec = mount_dec;
        tpm.temperature = temperature;
        tpm.pressure = pressure;
        tpm.humidity = humidity;
        tpm.snr = 0.0;    // Would come from plate solver in production
        tpm.seeing = 0.0; // Would come from seeing monitor in production
        // Use current time as measurement timestamp
        tpm.timestamp = std::chrono::system_clock::now();
        // Compute Julian date from timestamp for proper motion and precession correction.
        // Julian date at Unix epoch (1970-01-01 00:00:00 UTC) = 2440587.5
        auto ts_sec = std::chrono::duration<double>(
            tpm.timestamp.time_since_epoch()).count();
        tpm.julian_date = 2440587.5 + ts_sec / 86400.0;
        tpm.proper_motion_ra = proper_motion_ra;
        tpm.proper_motion_dec = proper_motion_dec;
        tpoint_model_->addMeasurement(tpm);
        
        return true;
    }
    
    bool addTPointMeasurement(double observed_ra, double observed_dec,
                              double expected_ra, double expected_dec,
                              double temperature, double pressure,
                              double humidity) {
        // Simplified version - use current mount position
        double mount_ha = 0.0;  // Would be from encoders in real implementation
        double mount_dec = expected_dec;  // Approximation
        
        return addTPointMeasurement(observed_ra, observed_dec,
                                   expected_ra, expected_dec,
                                   mount_ha, mount_dec,
                                   temperature, pressure, humidity,
                                   0.0, 0.0, 0.0, 2000.0);
    }
    
    void clearTPointMeasurements() {
        tpoint_measurements_.clear();
        tpoint_model_->clearMeasurements();
        tpoint_calibrated_ = false;
    }
    
    // Backward compatibility - keep old method
    bool addCalibrationMeasurement(double observed_ra, double observed_dec,
                                   double expected_ra, double expected_dec,
                                   double mount_ha, double mount_dec,
                                   double temperature, double pressure,
                                   double humidity,
                                   double proper_motion_ra, double proper_motion_dec,
                                   double parallax, double epoch) {
        return addTPointMeasurement(observed_ra, observed_dec,
                                   expected_ra, expected_dec,
                                   mount_ha, mount_dec,
                                   temperature, pressure, humidity,
                                   proper_motion_ra, proper_motion_dec,
                                   parallax, epoch);
    }
    
    bool runTPointCalibration() {
        if (tpoint_measurements_.size() < 3) {
            return false;
        }
        
        try {
            size_t n = tpoint_measurements_.size();
            bool fitted = false;
            
            // ============================================================
            // Progressive term expansion based on measurement count.
            // Uses tpoint_enabled_terms_ from config as the upper bound:
            // the controller never enables more terms than the user allows,
            // but progressively increases the enabled subset as more
            // measurements become available.
            // ============================================================
            
            uint32_t config_terms = tpoint_enabled_terms_;
            
            // ---- Level 5: Full config terms (n >= 28) ----
            // All terms the user has enabled. With DEFAULT_TERMS (~14 params)
            // or ALL_TERMS (~30+ params), requires ~28+ measurements.
            if (!fitted && n >= 28) {
                tpoint_model_->setEnabledTerms(config_terms);
                if (tpoint_model_->fitModel()) {
                    auto params = tpoint_model_->getParameters();
                    
                    MOUNT_LOG_INFO("TPOINT full model fit: RMS={:.2f} arcsec, "
                                   "Chi2={:.2f}, DOF={}, Cond={:.1f}",
                                   params.rms_error, params.chi_squared,
                                   params.degrees_of_freedom, params.condition_number);
                    
                    tpoint_residual_rms_arcsec_ = params.rms_error;
                    tpoint_residual_max_arcsec_ = params.max_error;
                    tpoint_chi_squared_ = params.chi_squared;
                    tpoint_mean_error_arcsec_ = params.mean_error;
                    
                    // Use all unused TPointModel methods for quality assessment
                    tpoint_quality_metrics_ = tpoint_model_->calculateQualityMetrics();
                    tpoint_covariance_matrix_ = tpoint_model_->getCovarianceMatrix();
                    tpoint_param_uncertainties_ = tpoint_model_->getParameterUncertainties();
                    
                    fitted = true;
                }
            }
            
            // ---- Level 4: Full DEFAULT_TERMS (n >= 20) ----
            // The standard TPoint terms: IH,ID,NP,CH,MA,ME,TF,HF,RF
            // ~14 params → needs ~20 measurements for robust fit.
            if (!fitted && n >= 20) {
                uint32_t level4 = config_terms & (
                    models::TPointTerms::INDEX_ERROR |
                    models::TPointTerms::COLLIMATION_ERROR |
                    models::TPointTerms::AXIS_NONPERP |
                    models::TPointTerms::POLAR_ALT |
                    models::TPointTerms::POLAR_AZ |
                    models::TPointTerms::TUBE_FLEXURE_HA |
                    models::TPointTerms::TUBE_FLEXURE_DEC |
                    models::TPointTerms::TUBE_ROTATION |
                    models::TPointTerms::REFRACTION);
                // Only try if at least one bit is set
                if (level4 != 0) {
                    tpoint_model_->setEnabledTerms(level4);
                    if (tpoint_model_->fitModel()) {
                        auto params = tpoint_model_->getParameters();
                        MOUNT_LOG_INFO("TPOINT level4 fit (standard terms): RMS={:.2f} arcsec, "
                                       "Chi2={:.2f}, DOF={}", params.rms_error, params.chi_squared,
                                       params.degrees_of_freedom);
                        tpoint_residual_rms_arcsec_ = params.rms_error;
                        tpoint_residual_max_arcsec_ = params.max_error;
                        tpoint_chi_squared_ = params.chi_squared;
                        tpoint_mean_error_arcsec_ = params.mean_error;
                        tpoint_quality_metrics_ = tpoint_model_->calculateQualityMetrics();
                        tpoint_covariance_matrix_ = tpoint_model_->getCovarianceMatrix();
                        tpoint_param_uncertainties_ = tpoint_model_->getParameterUncertainties();
                        fitted = true;
                    }
                }
            }
            
            // ---- Level 3: Extended medium (n >= 14) ----
            // Adds COLLIMATION_ERROR and TUBE_FLEXURE to the medium set.
            if (!fitted && n >= 14) {
                uint32_t level3 = config_terms & (
                    models::TPointTerms::INDEX_ERROR |
                    models::TPointTerms::COLLIMATION_ERROR |
                    models::TPointTerms::AXIS_NONPERP |
                    models::TPointTerms::POLAR_ALT |
                    models::TPointTerms::POLAR_AZ |
                    models::TPointTerms::TUBE_FLEXURE_HA |
                    models::TPointTerms::TUBE_FLEXURE_DEC |
                    models::TPointTerms::TUBE_ROTATION);
                if (level3 != 0) {
                    tpoint_model_->setEnabledTerms(level3);
                    if (tpoint_model_->fitModel()) {
                        auto params = tpoint_model_->getParameters();
                        MOUNT_LOG_INFO("TPOINT level3 fit (extended medium): RMS={:.2f} arcsec, "
                                       "Chi2={:.2f}, DOF={}", params.rms_error, params.chi_squared,
                                       params.degrees_of_freedom);
                        tpoint_residual_rms_arcsec_ = params.rms_error;
                        tpoint_residual_max_arcsec_ = params.max_error;
                        tpoint_chi_squared_ = params.chi_squared;
                        tpoint_mean_error_arcsec_ = params.mean_error;
                        tpoint_quality_metrics_ = tpoint_model_->calculateQualityMetrics();
                        tpoint_covariance_matrix_ = tpoint_model_->getCovarianceMatrix();
                        tpoint_param_uncertainties_ = tpoint_model_->getParameterUncertainties();
                        fitted = true;
                    }
                }
            }
            
            // ---- Level 2: Medium dataset (n >= 12) ----
            // Basic terms: INDEX, AXIS_NONPERP, POLAR_ALT, POLAR_AZ, TUBE_FLEXURE
            if (!fitted && n >= 12) {
                uint32_t level2 = config_terms & (
                    models::TPointTerms::INDEX_ERROR |
                    models::TPointTerms::AXIS_NONPERP |
                    models::TPointTerms::POLAR_ALT |
                    models::TPointTerms::POLAR_AZ |
                    models::TPointTerms::TUBE_FLEXURE_HA |
                    models::TPointTerms::TUBE_FLEXURE_DEC);
                if (level2 != 0) {
                    tpoint_model_->setEnabledTerms(level2);
                    if (tpoint_model_->fitModel()) {
                        auto params = tpoint_model_->getParameters();
                        MOUNT_LOG_INFO("TPOINT level2 fit (medium): RMS={:.2f} arcsec, "
                                       "Chi2={:.2f}, DOF={}", params.rms_error, params.chi_squared,
                                       params.degrees_of_freedom);
                        tpoint_residual_rms_arcsec_ = params.rms_error;
                        tpoint_residual_max_arcsec_ = params.max_error;
                        tpoint_chi_squared_ = params.chi_squared;
                        tpoint_mean_error_arcsec_ = params.mean_error;
                        tpoint_quality_metrics_ = tpoint_model_->calculateQualityMetrics();
                        tpoint_covariance_matrix_ = tpoint_model_->getCovarianceMatrix();
                        tpoint_param_uncertainties_ = tpoint_model_->getParameterUncertainties();
                        fitted = true;
                    }
                }
            }
            
            // ---- Level 1: Small dataset (n >= 10) ----
            // Minimal TPointModel fit with INDEX_ERROR only
            if (!fitted && n >= 10) {
                uint32_t level1 = config_terms & models::TPointTerms::INDEX_ERROR;
                if (level1 != 0) {
                    tpoint_model_->setEnabledTerms(level1);
                    if (tpoint_model_->fitModel()) {
                        auto params = tpoint_model_->getParameters();
                        MOUNT_LOG_INFO("TPOINT level1 fit (minimal): RMS={:.2f} arcsec",
                                       params.rms_error);
                        tpoint_residual_rms_arcsec_ = params.rms_error;
                        tpoint_residual_max_arcsec_ = params.max_error;
                        tpoint_chi_squared_ = params.chi_squared;
                        tpoint_mean_error_arcsec_ = params.mean_error;
                        tpoint_quality_metrics_ = tpoint_model_->calculateQualityMetrics();
                        tpoint_covariance_matrix_ = tpoint_model_->getCovarianceMatrix();
                        tpoint_param_uncertainties_ = tpoint_model_->getParameterUncertainties();
                        fitted = true;
                    }
                }
            }
            
            // ---- Level 0: Very small dataset (3-9 measurements) ----
            // Compute a simple 3-parameter fit (IH, ID, NP) using Eigen QR directly.
            // This bypasses TPointModel's getMinMeasurements() floor of 10.
            if (!fitted && n >= 3) {
                // Build design matrix: [1, 0, 0] for RA (IH),
                // [0, 1, ha*15] for Dec (ID + NP*ha)
                Eigen::MatrixXd A(2 * static_cast<int>(n), 3);
                Eigen::VectorXd b(2 * static_cast<int>(n));
                
                for (size_t i = 0; i < n; ++i) {
                    const auto& m = tpoint_measurements_[i];
                    double ha_deg = m.mount_ha * 15.0;
                    
                    // RA residual row: y_ra = observed_ra - expected_ra [deg]
                    // Model: y_ra = IH
                    A(2 * static_cast<int>(i), 0) = 1.0;     // IH
                    A(2 * static_cast<int>(i), 1) = 0.0;     // ID (no effect on RA)
                    A(2 * static_cast<int>(i), 2) = 0.0;     // NP (no effect on RA)
                    b(2 * static_cast<int>(i)) = (m.observed_ra - m.expected_ra) * 15.0;
                    
                    // Dec residual row: y_dec = observed_dec - expected_dec [deg]
                    // Model: y_dec = ID + NP * ha
                    A(2 * static_cast<int>(i) + 1, 0) = 0.0;     // IH (no effect on Dec)
                    A(2 * static_cast<int>(i) + 1, 1) = 1.0;     // ID
                    A(2 * static_cast<int>(i) + 1, 2) = ha_deg;  // NP * ha
                    b(2 * static_cast<int>(i) + 1) = m.observed_dec - m.expected_dec;
                }
                
                // Solve using QR decomposition
                Eigen::ColPivHouseholderQR<Eigen::MatrixXd> qr(A);
                Eigen::VectorXd params = qr.solve(b);
                
                double ih = params(0);   // Index HA [deg]
                double id = params(1);   // Index Dec [deg]
                double np = params(2);   // Non-perpendicularity [deg/deg HA]
                
                MOUNT_LOG_INFO("TPOINT level0 fit (direct QR): IH={:.6f}°, ID={:.6f}°, NP={:.6f}",
                               ih, id, np);
                
                // Compute residuals, RMS, max, chi-squared
                Eigen::VectorXd residuals = b - A * params;
                double sum_sq = residuals.squaredNorm();
                double rms = std::sqrt(sum_sq / (2.0 * static_cast<double>(n)));
                double mean_err = 0.0;
                double max_residual = 0.0;
                for (int i = 0; i < residuals.size(); ++i) {
                    double r = std::abs(residuals(i));
                    mean_err += r;
                    if (r > max_residual) max_residual = r;
                }
                mean_err /= static_cast<double>(residuals.size());
                
                MOUNT_LOG_INFO("TPOINT level0 fit: RMS={:.4f}° ({:.2f} arcsec)",
                               rms, rms * 3600.0);
                
                tpoint_residual_rms_arcsec_ = rms * 3600.0;
                tpoint_residual_max_arcsec_ = max_residual * 3600.0;
                tpoint_mean_error_arcsec_ = mean_err * 3600.0;
                tpoint_chi_squared_ = sum_sq * 3600.0 * 3600.0;
                fitted = true;
            }
            
            if (!fitted) {
                MOUNT_LOG_WARN("TPOINT calibration could not fit with {} measurements", n);
                return false;
            }
            
            // ---- Post-fit quality assessment ----
            // Use calculateResidual() to check each measurement for outliers.
            // Any measurement with residual > 5*rms is flagged as a potential outlier.
            if (tpoint_residual_rms_arcsec_ > 0.0) {
                double outlier_threshold = 5.0 * tpoint_residual_rms_arcsec_;
                size_t outlier_count = 0;
                for (size_t i = 0; i < n && i < tpoint_measurements_.size(); ++i) {
                    const auto& m = tpoint_measurements_[i];
                    models::TPointModel::Measurement tpm;
                    tpm.observed_ra = m.observed_ra;
                    tpm.observed_dec = m.observed_dec;
                    tpm.expected_ra = m.expected_ra;
                    tpm.expected_dec = m.expected_dec;
                    tpm.mount_ha = m.mount_ha;
                    tpm.mount_dec = m.mount_dec;
                    double resid = tpoint_model_->calculateResidual(tpm);
                    if (resid > outlier_threshold) {
                        outlier_count++;
                        MOUNT_LOG_WARN("TPOINT outlier detected at measurement {}: residual={:.2f} arcsec",
                                       i, resid);
                    }
                }
                if (outlier_count > 0) {
                    MOUNT_LOG_INFO("TPOINT calibration: {} outliers detected (>{:.1f} arcsec)",
                                   outlier_count, outlier_threshold);
                }
            }
            
            // Compute and log all residuals summary
            auto all_residuals = tpoint_model_->getAllResiduals();
            if (!all_residuals.empty()) {
                double min_r = all_residuals[0], max_r = all_residuals[0];
                double sum_r = 0.0;
                for (auto r : all_residuals) {
                    sum_r += r;
                    if (r < min_r) min_r = r;
                    if (r > max_r) max_r = r;
                }
                double mean_r = sum_r / all_residuals.size();
                MOUNT_LOG_INFO("TPOINT residual summary: min={:.2f}, max={:.2f}, mean={:.2f} arcsec",
                               min_r, max_r, mean_r);
            }
            
            tpoint_calibrated_ = true;
            calibration_count_++;
            return true;
            
        } catch (const std::exception& e) {
            MOUNT_LOG_ERROR("TPOINT calibration failed: {}", e.what());
            return false;
        }
    }
    
    std::string getTPointParameters() const {
        json j;
        j["calibrated"] = tpoint_calibrated_;
        j["tpoint_measurement_count"] = tpoint_measurements_.size();
        j["enabled_terms_bitmask"] = tpoint_enabled_terms_;
        j["timestamp"] = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now());
        
        if (tpoint_calibrated_) {
            auto tp_params = tpoint_model_->getParameters();
            
            // ---- Statistical summary ----
            j["rms_error_arcsec"] = tp_params.rms_error;
            j["max_error_arcsec"] = tp_params.max_error;
            j["mean_error_arcsec"] = tp_params.mean_error;
            j["chi_squared"] = tp_params.chi_squared;
            j["degrees_of_freedom"] = tp_params.degrees_of_freedom;
            j["condition_number"] = tp_params.condition_number;
            j["parameter_uncertainty_scale"] = tp_params.parameter_uncertainty_scale;
            j["calibration_temperature_celsius"] = tp_params.calibration_temperature;
            
            // ---- Fitted TPOINT basic error terms ----
            j["index_error_arcsec"] = tp_params.index_error;
            j["collimation_error_arcsec"] = tp_params.collimation_error;
            j["axis_nonperp_arcsec"] = tp_params.axis_nonperp;
            j["axis_nonperp_temp_coeff_arcsec_per_c"] = tp_params.axis_nonperp_temp_coeff;
            j["polar_alt_error_arcsec"] = tp_params.polar_alt_error;
            j["polar_az_error_arcsec"] = tp_params.polar_az_error;
            j["tube_flexure_ha_arcsec_per_rad"] = tp_params.tube_flexure_ha;
            j["tube_flexure_dec_arcsec_per_rad"] = tp_params.tube_flexure_dec;
            j["tube_rotation_arcsec"] = tp_params.tube_rotation;
            
            // ---- Worm / periodic error terms ----
            j["worm_period_error_arcsec"] = tp_params.worm_period_error;
            json wh_arr = json::array();
            for (auto h : tp_params.worm_harmonics) {
                wh_arr.push_back(h);
            }
            j["worm_harmonics"] = wh_arr;
            
            // ---- Encoder error terms ----
            j["encoder_error_ha_arcsec"] = tp_params.encoder_error_ha;
            j["encoder_error_dec_arcsec"] = tp_params.encoder_error_dec;
            json eh_ha = json::array();
            for (auto h : tp_params.encoder_harmonics_ha) {
                eh_ha.push_back(h);
            }
            j["encoder_harmonics_ha"] = eh_ha;
            json eh_dec = json::array();
            for (auto h : tp_params.encoder_harmonics_dec) {
                eh_dec.push_back(h);
            }
            j["encoder_harmonics_dec"] = eh_dec;
            
            // ---- Atmospheric refraction ----
            j["refraction_coeff"] = tp_params.refraction_coeff;
            j["refraction_temp_coeff"] = tp_params.refraction_temp_coeff;
            j["refraction_pressure_coeff"] = tp_params.refraction_pressure_coeff;
            
            // ---- Temperature-dependent terms ----
            j["temp_flexure_coeff_arcsec_per_c"] = tp_params.temp_flexure_coeff;
            j["temp_encoder_coeff_arcsec_per_c"] = tp_params.temp_encoder_coeff;
            
            // ---- Quality metrics from calculateQualityMetrics() ----
            if (!tpoint_quality_metrics_.empty()) {
                json qm = json::object();
                for (const auto& [key, val] : tpoint_quality_metrics_) {
                    qm[key] = val;
                }
                j["quality_metrics"] = qm;
            }
            
            // ---- Parameter uncertainties from getParameterUncertainties() ----
            if (!tpoint_param_uncertainties_.empty()) {
                json pu_arr = json::array();
                for (auto u : tpoint_param_uncertainties_) {
                    pu_arr.push_back(u);
                }
                j["parameter_uncertainties"] = pu_arr;
            }
            
            // ---- Covariance matrix summary (diagonal only for brevity) ----
            if (tpoint_covariance_matrix_.size() > 0) {
                json cov_diag = json::array();
                for (int i = 0; i < tpoint_covariance_matrix_.rows(); ++i) {
                    cov_diag.push_back(tpoint_covariance_matrix_(i, i));
                }
                j["covariance_diagonal"] = cov_diag;
                j["covariance_rows"] = tpoint_covariance_matrix_.rows();
                j["covariance_cols"] = tpoint_covariance_matrix_.cols();
            }
        }
        
        return j.dump();
    }
    
    std::vector<double> getRotationMatrix() const {
        std::shared_lock<std::shared_mutex> lock(*state_mutex_);
        // Calculate field rotation for alt-az and casual mounts
        // For equatorial mounts, field rotation is zero (except for atmospheric refraction)
        // For alt-az and casual mounts, field rotation = -ω * cos(φ) / sin(alt)
        // where ω is sidereal rate, φ is latitude, alt is mount-frame altitude-like axis
        
        if (config_.mount_type == MountType::EQUATORIAL) {
            // Equatorial mount: minimal field rotation (only atmospheric effects)
            // Return identity quaternion
            return {1.0, 0.0, 0.0, 0.0};
        } else {
            // Alt-Az and CASUAL mounts: calculate field rotation using mount-frame position
            // Field rotation rate = -15.041 * cos(latitude) / sin(altitude) arcsec/sec
            // where 15.041 arcsec/sec is sidereal rate.
            // For CASUAL mounts, axis1 = altitude-like, axis2 = azimuth-like in mount frame.
            
            double current_time = std::chrono::duration<double>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            // Get current mount position
            // For ALT_AZ: axis2 = altitude, axis1 = azimuth
            // For CASUAL: axis1 = altitude-like, axis2 = azimuth-like in mount frame
            double alt;
            if (config_.mount_type == MountType::CASUAL) {
                alt = axis1_position_;  // altitude-like axis in mount frame
            } else {
                alt = axis2_position_;  // altitude in degrees (ALT_AZ convention)
            }
            
            // NaN/Inf guard: IEEE 754 NaN < 1.0 is false, so a simple
            // `if (alt < 1.0)` would let NaN pass through unguarded.
            // Must use isfinite() first, THEN clamp to safe minimum.
            if (!std::isfinite(alt) || alt < 1.0) {
                alt = 1.0;
            }
            
            // Guard against extreme cos(lat) product causing overflow
            // even when lat is finite (e.g., config corruption)
            double lat_rad = config_.latitude * M_PI / 180.0;
            if (!std::isfinite(config_.latitude)) {
                lat_rad = 52.0 * M_PI / 180.0;  // default mid-latitude
            }
            
            // Calculate field rotation rate (radians/sec)
            double sidereal_rate_rad = 7.2921150e-5;  // Earth rotation rate in rad/s
            double alt_rad = alt * M_PI / 180.0;
            
            double field_rotation_rate = -sidereal_rate_rad * std::cos(lat_rad) / std::sin(alt_rad);
            
            // Clamp field rotation rate to prevent extreme values from propagating
            // to the derotator. Same limit as enableFieldRotation() uses.
            const double MAX_RATE_RAD = 20.0 * M_PI / 180.0;  // 20 deg/s
            field_rotation_rate = std::clamp(field_rotation_rate, -MAX_RATE_RAD, MAX_RATE_RAD);
            
            // Integrate rotation over time
            double last_time = last_field_rotation_time_;
            if (last_time == 0.0) last_time = current_time;
            double dt = current_time - last_time;
            last_field_rotation_time_ = current_time;
            
            double total_rotation = field_rotation_rate * dt;
            
            // Create rotation quaternion around Z axis (field rotation axis)
            // q = [cos(θ/2), 0, 0, sin(θ/2)] for rotation around Z
            double half_angle = total_rotation / 2.0;
            return {cos(half_angle), 0.0, 0.0, sin(half_angle)};
        }
    }
    
    void setEncodersEnabled(bool enable) {
        std::lock_guard<std::shared_mutex> lock(*state_mutex_);
        encoders_active_ = enable;
    }
    
    void setEncoderType(bool absolute) {
        std::lock_guard<std::shared_mutex> lock(*state_mutex_);
        encoder_absolute_ = absolute;
    }
    
    bool connectGuider(const std::string& connection_string) {
        std::lock_guard<std::shared_mutex> lock(*state_mutex_);
        guider_active_ = true;
        guider_connection_ = connection_string;
        return true;
    }
    
    void disconnectGuider() {
        std::lock_guard<std::shared_mutex> lock(*state_mutex_);
        guider_active_ = false;
        guider_connection_.clear();
    }
    
    void applyGuiderCorrection(double ra_correction, double dec_correction) {
        // Fast check if tracking is active and read current declination (under state_mutex_)
        bool is_tracking = false;
        double current_dec_deg = 0.0;
        {
            std::lock_guard<std::shared_mutex> state_lock(*state_mutex_);
            is_tracking = (state_ == MountStatus::State::TRACKING);
            current_dec_deg = axis2_position_;
        }
        if (!is_tracking) return;
        
        // Lock rate mutex to modify delta corrections (read and reset by tracking loop)
        std::lock_guard<std::shared_mutex> lock(*rate_mutex_);
        
        // Clamp in arcseconds (same units as input, consistent with config).
        // RA and Dec corrections both arrive in arcseconds — no unit conversion needed
        // for clamping.
        double max_correction_arcsec = config_.guider_max_correction;
        double aggression = config_.guider_aggression;
        
        ra_correction = std::clamp(ra_correction * aggression,
                                   -max_correction_arcsec,
                                   max_correction_arcsec);
        dec_correction = std::clamp(dec_correction * aggression,
                                    -max_correction_arcsec,
                                    max_correction_arcsec);
        
        // Write to delta variables as POSITION OFFSETS (degrees), not rate offsets.
        // The tracking loop applies these directly to axis positions (not through rates),
        // which makes the correction magnitude independent of loop frequency (dt).
        //
        // RA correction: arcsec → degrees of Hour Angle.
        //   1 arcsec RA = 1/3600 hours RA = 15/3600 degrees HA (at celestial equator).
        //   On the sky, 1 arcsec of RA at declination δ corresponds to 1/cos(δ) arcsec
        //   of HA motion. Without the cos(δ) factor, corrections at high declinations
        //   would be too small, causing the mount to under-correct RA guide errors near
        //   the celestial pole. With cos(δ) included, a 1 arcsec RA guide error produces
        //   the correct HA axis movement for ANY declination.
        double cos_dec = std::cos(current_dec_deg * M_PI / 180.0);
        // Guard against division by zero or extreme amplification near the poles (|Dec| > 87°).
        // cos(87°) ≈ 0.052, below which the 1/cos(δ) factor becomes pathologically large.
        // Clamping to cos(85°) ≈ 0.087 limits the max amplification to ~11.5×, which is
        // physically conservative — no practical tracking target exceeds |Dec|=85°.
        if (cos_dec < 0.087) cos_dec = 0.087;
        guider_delta_axis1_ += ra_correction * 15.0 / 3600.0 / cos_dec;  // arcsec → degrees HA
        guider_delta_axis2_ += dec_correction / 3600.0;                   // arcsec → degrees Dec
    }
    
    std::tuple<double, double, double> determinePolePosition(double duration_hours) {
        MOUNT_LOG_INFO("Determining pole position (duration={:.1f}h)", duration_hours);
        
        // If TPoint calibration is available, extract polar alignment error
        // from the fitted POLAR_ALT (me) and POLAR_AZ (ma) terms.
        if (tpoint_calibrated_ && tpoint_model_) {
            auto params = tpoint_model_->getParameters();
            double polar_alt_err_arcsec = params.polar_alt_error;   // me: polar altitude error
            double polar_az_err_arcsec  = params.polar_az_error;    // ma: polar azimuth error
            
            // Convert angular error to corrected pole position:
            //   polar_alt_error: the mount's polar axis is offset in altitude
            //     -> corrected_lat = configured_lat - me_arcsec / 3600.0
            //   polar_az_error: the mount's polar axis is offset in azimuth
            //     -> projected onto longitude as: ma_arcsec / (3600.0 * cos(lat))
            double lat_rad = config_.latitude * M_PI / 180.0;
            double cos_lat = std::cos(lat_rad);
            
            // Guard against polar singularity: cos(lat) → 0 when lat → ±90°,
            // making the longitude correction 1/cos(lat) → ∞.  Clamp to a
            // minimum of cos(89°) ≈ 0.01745, corresponding to a maximum
            // sensible longitude correction of ~1.1° per arcsecond of azimuth
            // error — already well beyond any real TPoint-based correction.
            const double MIN_COS_LAT = std::cos(89.0 * M_PI / 180.0); // ≈ 0.01745
            if (std::abs(cos_lat) < MIN_COS_LAT) {
                MOUNT_LOG_WARN("TPoint polar singularity guard: cos(lat)={:.6f} for lat={:.2f}° — "
                               "clamping to {:.6f} for longitude projection",
                               cos_lat, config_.latitude, std::copysign(MIN_COS_LAT, cos_lat));
                cos_lat = std::copysign(MIN_COS_LAT, cos_lat);
            }
            
            double corrected_lat  = config_.latitude  - polar_alt_err_arcsec / 3600.0;
            double corrected_lon  = config_.longitude - polar_az_err_arcsec / (3600.0 * cos_lat);
            
            // Estimate accuracy from TPoint quality metrics or covariance
            double accuracy_arcsec = 30.0; // fallback default
            if (!tpoint_param_uncertainties_.empty()) {
                // Use mean parameter uncertainty as accuracy bound
                double sum = 0.0;
                for (double u : tpoint_param_uncertainties_) sum += u;
                accuracy_arcsec = sum / tpoint_param_uncertainties_.size();
            }
            auto qmetrics = tpoint_model_->calculateQualityMetrics();
            auto it = qmetrics.find("rms_residual");
            if (it != qmetrics.end() && it->second > 0.0) {
                // Accuracy cannot be better than RMS residual / sqrt(n_params)
                double rms = it->second;
                accuracy_arcsec = std::max(accuracy_arcsec, rms * 0.5);
            }
            // Scale accuracy by duration (longer drift measurement = better accuracy)
            accuracy_arcsec = std::max(accuracy_arcsec / std::sqrt(duration_hours + 1.0), 1.0);
            
            MOUNT_LOG_INFO("TPoint polar error: alt={:.1f}\", az={:.1f}\"  ->  corrected pole: lat={:.6f}°, lon={:.6f}°  (accuracy={:.1f}\")",
                     polar_alt_err_arcsec, polar_az_err_arcsec,
                     corrected_lat, corrected_lon, accuracy_arcsec);
            
            return {corrected_lat, corrected_lon, accuracy_arcsec};
        }
        
        // -----------------------------------------------------------------------
        // Full drift-alignment procedure (no TPoint calibration available)
        //
        // Classical drift alignment uses two stars on the celestial equator:
        //   Star 1 — at the meridian (HA = 0h)  → measures polar ALTITUDE error
        //   Star 2 — near the eastern/western horizon (HA ≈ ±6h) → measures polar AZIMUTH error
        //
        // At the celestial equator, declination drift per hour is:
        //   dDec/dt = polar_alt_error * cos(HA) + polar_az_error * sin(HA)
        //
        // Star 1 (HA=0h):  dDec₁/dt = polar_alt_error
        // Star 2 (HA=+6h): dDec₂/dt = polar_az_error  (cos=0, sin=1)
        // -----------------------------------------------------------------------
        MOUNT_LOG_INFO("Performing two-star drift alignment (no TPoint calibration)");
        
        // Compute current Local Sidereal Time for star selection
        double jd = core::AstronomicalCalculations::getCurrentJulianDate();
        double lst = core::AstronomicalCalculations::calculateLST(jd, config_.longitude);
        while (lst < 0.0) lst += 24.0;
        while (lst >= 24.0) lst -= 24.0;
        
        // Map the user-provided duration to an actual physical wait per star.
        // The duration represents the total intended drift-measurement time; we
        // split it equally between the two stars but clamp to keep tests fast.
        // In production (duration_hours ≈ 0.5–2.0), this gives several seconds
        // per star — enough for the tracking loop to accumulate measurable drift.
        double wait_per_star_ms = std::clamp(duration_hours * 1800.0, 200.0, 5000.0);
        double actual_wait_hours = (wait_per_star_ms / 1000.0) / 3600.0;
        
        // ===================================================================
        // STAR 1 — Meridian (HA = 0h, Dec = 0°) → polar altitude error
        // ===================================================================
        double star1_ra  = lst;    // RA = LST → HA = 0h
        double star1_dec = 0.0;    // Celestial equator
        
        // Stop any ongoing movement before we begin
        stop();
        joinWorkThread();
        
        MOUNT_LOG_INFO("Drift alignment: slewing to star 1 (RA={:.4f}h, Dec={:.1f}°) "
                       "at meridian for altitude error measurement",
                       star1_ra, star1_dec);
        
        if (!slewToEquatorial(star1_ra, star1_dec)) {
            MOUNT_LOG_ERROR("Drift alignment FAILED: cannot slew to meridian star");
            return {config_.latitude, config_.longitude, 600.0};
        }
        
        // Wait for slew to complete (poll state_ in short intervals)
        {
            const int POLL_MS  = 50;
            const int TIMEOUT_MS = 120000;
            int elapsed = 0;
            while (elapsed < TIMEOUT_MS) {
                std::this_thread::sleep_for(std::chrono::milliseconds(POLL_MS));
                elapsed += POLL_MS;
                std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                if (state_ == MountStatus::State::IDLE) break;
            }
        }
        
        // Record initial declination
        double initial_dec;
        {
            std::lock_guard<std::shared_mutex> lock(*state_mutex_);
            initial_dec = axis2_position_;
        }
        
        MOUNT_LOG_INFO("Drift alignment: tracking star 1 for {:.1f} ms to measure Dec drift",
                       wait_per_star_ms);
        
        if (!startTracking(star1_ra, star1_dec, TrackingMode::SIDEREAL)) {
            MOUNT_LOG_ERROR("Drift alignment FAILED: cannot start tracking at star 1");
            return {config_.latitude, config_.longitude, 600.0};
        }
        
        // Let the tracking loop run for the measurement period.
        // In simulation, axis2_rate_=0 for sidereal tracking so Dec stays constant;
        // on real hardware, any polar misalignment causes Dec drift proportional
        // to polar_alt_error * cos(HA) which at HA=0 gives polar_alt_error directly.
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(wait_per_star_ms)));
        
        // Stop tracking and read final Dec
        stop();
        
        double final_dec;
        {
            std::lock_guard<std::shared_mutex> lock(*state_mutex_);
            final_dec = axis2_position_;
        }
        
        double dec_drift1_arcsec   = (final_dec - initial_dec) * 3600.0;
        double polar_alt_err_asec_h = dec_drift1_arcsec / actual_wait_hours;
        
        MOUNT_LOG_INFO("Drift alignment star 1: dec {:.4f}° -> {:.4f}°  drift = {:.2f}\"/h -> polar_alt_error = {:.2f}\"/h",
                 initial_dec, final_dec, polar_alt_err_asec_h, polar_alt_err_asec_h);
        
        // ===================================================================
        // STAR 2 — Eastern horizon (HA = +6h, Dec = 0°) → polar azimuth error
        // ===================================================================
        double star2_ra  = lst + 6.0;   // RA + 6h → HA = -6h (east)
        if (star2_ra >= 24.0) star2_ra -= 24.0;
        double star2_dec = 0.0;         // Celestial equator
        
        MOUNT_LOG_INFO("Drift alignment: slewing to star 2 (RA={:.4f}h, Dec={:.1f}°) "
                       "at HA=+6h for azimuth error measurement",
                       star2_ra, star2_dec);
        
        if (!slewToEquatorial(star2_ra, star2_dec)) {
            MOUNT_LOG_WARN("Drift alignment: cannot slew to horizon star — "
                           "returning altitude-only correction");
            double lat_rad = config_.latitude * M_PI / 180.0;
            double cos_lat = std::cos(lat_rad);
            double corrected_lat = config_.latitude - polar_alt_err_asec_h / 3600.0;
            double accuracy = std::max(std::abs(polar_alt_err_asec_h) + 30.0, 60.0);
            return {corrected_lat, config_.longitude, accuracy};
        }
        
        // Wait for second slew to complete
        {
            const int POLL_MS  = 50;
            const int TIMEOUT_MS = 120000;
            int elapsed = 0;
            while (elapsed < TIMEOUT_MS) {
                std::this_thread::sleep_for(std::chrono::milliseconds(POLL_MS));
                elapsed += POLL_MS;
                std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                if (state_ == MountStatus::State::IDLE) break;
            }
        }
        
        // Record initial Dec for star 2
        {
            std::lock_guard<std::shared_mutex> lock(*state_mutex_);
            initial_dec = axis2_position_;
        }
        
        MOUNT_LOG_INFO("Drift alignment: tracking star 2 for {:.1f} ms to measure Dec drift",
                       wait_per_star_ms);
        
        if (!startTracking(star2_ra, star2_dec, TrackingMode::SIDEREAL)) {
            MOUNT_LOG_WARN("Drift alignment: cannot track star 2 — "
                           "returning altitude-only correction");
            double lat_rad = config_.latitude * M_PI / 180.0;
            double cos_lat = std::cos(lat_rad);
            double corrected_lat = config_.latitude - polar_alt_err_asec_h / 3600.0;
            return {corrected_lat, config_.longitude, 60.0};
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(wait_per_star_ms)));
        
        stop();
        
        {
            std::lock_guard<std::shared_mutex> lock(*state_mutex_);
            final_dec = axis2_position_;
        }
        
        double dec_drift2_arcsec   = (final_dec - initial_dec) * 3600.0;
        // At HA = -6h (star 2 RA = LST + 6), the Dec drift equation gives:
        //   dDec/dt = polar_alt_error * cos(-90°) + polar_az_error * sin(-90°)
        //   dDec/dt = -polar_az_error
        double polar_az_err_asec_h  = -dec_drift2_arcsec / actual_wait_hours;
        
        MOUNT_LOG_INFO("Drift alignment star 2: dec {:.4f}° -> {:.4f}°  drift = {:.2f}\"/h -> polar_az_error = {:.2f}\"/h",
                 initial_dec, final_dec, polar_az_err_asec_h, polar_az_err_asec_h);
        
        // ===================================================================
        // Compute corrected pole position
        // ===================================================================
        double lat_rad    = config_.latitude * M_PI / 180.0;
        double cos_lat    = std::cos(lat_rad);
        
        // Guard against polar singularity: cos(lat) → 0 when lat → ±90°,
        // making the longitude correction 1/cos(lat) → ∞.  Clamp to a
        // minimum of cos(89°) ≈ 0.01745, corresponding to a maximum
        // sensible longitude correction of ~1.1° per arcsecond of azimuth
        // error — already well beyond any real drift-alignment scenario.
        const double MIN_COS_LAT = std::cos(89.0 * M_PI / 180.0); // ≈ 0.01745
        if (std::abs(cos_lat) < MIN_COS_LAT) {
            MOUNT_LOG_WARN("Polar singularity guard: cos(lat)={:.6f} for lat={:.2f}° — "
                           "clamping to {:.6f} for longitude projection",
                           cos_lat, config_.latitude, std::copysign(MIN_COS_LAT, cos_lat));
            cos_lat = std::copysign(MIN_COS_LAT, cos_lat);
        }
        
        double corrected_lat  = config_.latitude  - polar_alt_err_asec_h / 3600.0;
        double corrected_lon  = config_.longitude - polar_az_err_asec_h / (3600.0 * cos_lat);
        
        // Accuracy estimate: bounded by measurement noise and drift consistency.
        // Longer duration_hours improves the effective signal-to-noise ratio.
        double accuracy_arcsec = std::max(
            15.0 / std::sqrt(duration_hours + 0.1),                          // shot noise floor
            std::hypot(polar_alt_err_asec_h, polar_az_err_asec_h) * 0.05 + 5.0  // 5% of measured error + floor
        );
        
        MOUNT_LOG_INFO("Drift alignment complete: alt_err={:.1f}\"/h, az_err={:.1f}\"/h  "
                       "-> corrected pole: lat={:.6f}°, lon={:.6f}°  (accuracy={:.1f}\")",
                 polar_alt_err_asec_h, polar_az_err_asec_h,
                 corrected_lat, corrected_lon, accuracy_arcsec);
        
        return {corrected_lat, corrected_lon, accuracy_arcsec};
    }
    
    bool saveState(const std::string& filename) const {
        std::shared_lock<std::shared_mutex> lock(*state_mutex_);
        json state;
        
        state["axis1_position"] = axis1_position_;
        state["axis2_position"] = axis2_position_;
        state["axis1_target"] = axis1_target_;
        state["axis2_target"] = axis2_target_;
        state["state"] = static_cast<int>(state_);
        state["encoders_active"] = encoders_active_;
        state["guider_active"] = guider_active_;
        state["tpoint_calibrated"] = tpoint_calibrated_;
        state["bootstrap_calibrated"] = bootstrap_calibrated_;
        state["encoder_absolute"] = encoder_absolute_;
        // Read env params under env_mutex_ — setEnvironmentalParams() writes
        // them under this lock and forwards them to astro_calc_ concurrently.
        {
            std::lock_guard<std::mutex> env_lock(*env_mutex_);
            state["env_temperature"] = env_temperature_;
            state["env_pressure"] = env_pressure_;
            state["env_humidity"] = env_humidity_;
        }
        state["pier_side"] = pier_side_;
        state["meridian_flipped"] = meridian_flipped_;
        state["tpoint_enabled_terms"] = static_cast<int>(tpoint_enabled_terms_);
        state["guider_connection"] = guider_connection_;
        state["tracking_error_ra"] = tracking_error_ra_;
        state["tracking_error_dec"] = tracking_error_dec_;
        
        // Mount orientation quaternion [qx, qy, qz, qw] — critical for CASUAL mounts
        state["mount_orientation_qx"] = mount_orientation_.quaternion[0];
        state["mount_orientation_qy"] = mount_orientation_.quaternion[1];
        state["mount_orientation_qz"] = mount_orientation_.quaternion[2];
        state["mount_orientation_qw"] = mount_orientation_.quaternion[3];
        
        state["timestamp"] = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now());
        
        // ---- Persist TPointModel to a companion file ----
        if (tpoint_calibrated_ && tpoint_model_) {
            std::string tpoint_file = filename + ".tpoint";
            if (tpoint_model_->saveToFile(tpoint_file)) {
                state["tpoint_model_file"] = tpoint_file;
                MOUNT_LOG_DEBUG("TPOINT model saved to {}", tpoint_file);
            } else {
                MOUNT_LOG_WARN("Failed to save TPOINT model to {}", tpoint_file);
            }
        }
        
        // Ensure the parent directory exists before opening the file.
        std::filesystem::path parent_dir = std::filesystem::path(filename).parent_path();
        if (!parent_dir.empty()) {
            std::filesystem::create_directories(parent_dir);
        }
        
        std::ofstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        file << state.dump(4);
        return true;
    }
    
    bool loadState(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        try {
            json state = json::parse(file);
            
            {
                std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                
                axis1_position_ = state.value("axis1_position", 0.0);
                axis2_position_ = state.value("axis2_position", 0.0);
                axis1_target_ = state.value("axis1_target", 0.0);
                axis2_target_ = state.value("axis2_target", 0.0);
                
                int state_int = state.value("state", 0);
                if (state_int >= 0 && state_int <= 7) {
                    state_ = static_cast<MountStatus::State>(state_int);
                }
                
                encoders_active_ = state.value("encoders_active", false);
                guider_active_ = state.value("guider_active", false);
                tpoint_calibrated_ = state.value("tpoint_calibrated", false);
                bootstrap_calibrated_ = state.value("bootstrap_calibrated", false);
                encoder_absolute_ = state.value("encoder_absolute", false);
                // Write env params under env_mutex_ and forward to astro_calc_
                // so refraction calculations use the restored values.
                {
                    std::lock_guard<std::mutex> env_lock(*env_mutex_);
                    env_temperature_ = state.value("env_temperature", 15.0);
                    env_pressure_ = state.value("env_pressure", 1013.25);
                    env_humidity_ = state.value("env_humidity", 0.5);
                }
                
                pier_side_ = state.value("pier_side", 1);
                meridian_flipped_ = state.value("meridian_flipped", false);
                tpoint_enabled_terms_ = static_cast<uint32_t>(state.value("tpoint_enabled_terms", 0));
                guider_connection_ = state.value("guider_connection", "");
                tracking_error_ra_ = state.value("tracking_error_ra", 0.0);
                tracking_error_dec_ = state.value("tracking_error_dec", 0.0);
                
                // Restore mount orientation quaternion
                mount_orientation_.quaternion[0] = state.value("mount_orientation_qx", 0.0);
                mount_orientation_.quaternion[1] = state.value("mount_orientation_qy", 0.0);
                mount_orientation_.quaternion[2] = state.value("mount_orientation_qz", 0.0);
                mount_orientation_.quaternion[3] = state.value("mount_orientation_qw", 1.0);
            }
            
            // Forward restored env params to AstronomicalCalculations outside
            // state_mutex_ scope to minimize lock nesting depth.
            if (astro_calc_) {
                astro_calc_->setEnvironmentalParams(
                    state.value("env_temperature", 15.0),
                    state.value("env_pressure", 1013.25),
                    state.value("env_humidity", 0.5));
            }
            
            // ---- Restore TPointModel from companion file ----
            if (tpoint_model_) {
                std::string tpoint_file = state.value("tpoint_model_file", filename + ".tpoint");
                if (tpoint_model_->loadFromFile(tpoint_file)) {
                    // Re-apply the enabled terms bitmask after loading
                    tpoint_model_->setEnabledTerms(tpoint_enabled_terms_);
                    MOUNT_LOG_INFO("TPOINT model loaded from {}", tpoint_file);
                } else {
                    // If load fails, try the default companion path
                    tpoint_file = filename + ".tpoint";
                    if (tpoint_model_->loadFromFile(tpoint_file)) {
                        tpoint_model_->setEnabledTerms(tpoint_enabled_terms_);
                        MOUNT_LOG_INFO("TPOINT model loaded from default {}", tpoint_file);
                    } else {
                        MOUNT_LOG_DEBUG("No TPOINT model file found at {}", tpoint_file);
                    }
                }
            }
            
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    
    void setEnvironmentalParams(double temperature, double pressure, double humidity) {
        // Use dedicated env_mutex_ for environmental parameters to avoid
        // contention on state_mutex_ and ensure synchronization with
        // any code reading these values (e.g., saveState / loadState).
        {
            std::lock_guard<std::mutex> lock(*env_mutex_);
            env_temperature_ = temperature;
            env_pressure_ = pressure;
            env_humidity_ = humidity;
        }
        // Forward to AstronomicalCalculations so refraction calculations
        // (applyAtmosphericRefraction) use the actual environmental conditions
        // rather than default values (15°C, 1013.25 hPa).
        if (astro_calc_) {
            astro_calc_->setEnvironmentalParams(temperature, pressure, humidity);
        }
    }
    
    ControllerConfig getConfiguration() const {
        return config_;
    }
    
    bool setMountOrientation(const MountOrientation& orientation) {
        std::lock_guard<std::shared_mutex> lock(*state_mutex_);
        if (!orientation.isValid()) {
            return false;
        }
        mount_orientation_ = orientation;
        return true;
    }
    
    MountOrientation getMountOrientation() const {
        std::shared_lock<std::shared_mutex> lock(*state_mutex_);
        return mount_orientation_;
    }
    
    bool updateConfiguration(const ControllerConfig& config) {
        std::lock_guard<std::shared_mutex> lock(*state_mutex_);
        config_ = config;

        // Sync CANopen pdo_config_enabled into hal_config_ so that
        // saveConfigToFile→hal_config_.toJson() preserves it.
        hal_config_.canopen.pdo_config_enabled = config_.canopen_pdo_config_enabled;

        // Persist to disk if a config file path has been set
        if (!config_file_path_.empty()) {
            saveConfigToFile();
        }

        // ── Apply logging changes in real-time ──────────────────────────
        // Log level takes effect immediately without requiring a restart.
        {
            auto new_level = logging::Logger::levelFromString(config_.log_level);
            MOUNT_LOG_INFO("updateConfiguration: applying log level '{}' (enum={})",
                           config_.log_level, static_cast<int>(new_level));
            logging::Logger::setLevel(new_level);
            MOUNT_LOG_INFO("updateConfiguration: log level changed to '{}'",
                           config_.log_level);
            MOUNT_LOG_DEBUG("updateConfiguration: THIS IS A DEBUG-LEVEL TEST MESSAGE — "
                           "if you see this, DEBUG logging is working!");
        }

        // Console output toggling: set console sink level to OFF when disabled,
        // restore to current log level when enabled.
        {
            if (config_.log_console_output) {
                auto current_level = logging::Logger::getLevel();
                logging::Logger::setConsoleLevel(current_level);
            } else {
                logging::Logger::setConsoleLevel(logging::LogLevel::OFF);
            }
            MOUNT_LOG_INFO("updateConfiguration: console_output={}",
                           config_.log_console_output ? "true" : "false");
        }

        return true;
    }

    /**
     * @brief Serialize current config_ to the JSON config file.
     *
     * Loads the existing file to preserve fields not managed by ControllerConfig,
     * updates the managed fields, and writes back.
     */
    void saveConfigToFile() {
        try {
            json j;

            // Load existing file to preserve unmanaged sections
            std::ifstream in_file(config_file_path_);
            if (in_file.is_open()) {
                try {
                    j = json::parse(in_file);
                } catch (...) {
                    j = json::object();
                }
                in_file.close();
            }

            // Helper: only overwrite a JSON field if the new value is meaningful
            // (non-empty string, non-zero number).  This prevents partial gRPC
            // updates from zeroing out fields the UI did not touch.
            auto setIfStr = [&](const std::string& section, const std::string& key,
                                const std::string& val) {
                if (!val.empty()) {
                    if (!j.contains(section)) j[section] = json::object();
                    j[section][key] = val;
                }
            };
            auto setIfInt = [&](const std::string& section, const std::string& key,
                                int val) {
                if (val != 0) {
                    if (!j.contains(section)) j[section] = json::object();
                    j[section][key] = val;
                }
            };
            auto setIfDbl = [&](const std::string& section, const std::string& key,
                                double val) {
                if (val != 0.0) {
                    if (!j.contains(section)) j[section] = json::object();
                    j[section][key] = val;
                }
            };
            // Always-set helper (for booleans, sections that must be present)
            auto setAlways = [&](const std::string& section, const std::string& key,
                                 const json& val) {
                if (!j.contains(section)) j[section] = json::object();
                j[section][key] = val;
            };
            // Section-level helper: ensure section exists
            auto ensureSection = [&](const std::string& section) {
                if (!j.contains(section)) j[section] = json::object();
            };

            // ── logging ──────────────────────────────────────────────
            setIfStr("logging", "level", config_.log_level);
            setIfStr("logging", "directory", config_.log_directory);
            setIfInt("logging", "rotation_days", config_.log_rotation_days);
            setIfInt("logging", "max_file_size_mb", config_.log_max_file_size_mb);
            setAlways("logging", "console_output", config_.log_console_output);

            // ── network ──────────────────────────────────────────────
            setIfStr("network", "grpc_address", config_.grpc_address);
            setIfInt("network", "grpc_port", config_.grpc_port);
            setIfInt("network", "max_connections", config_.network_max_connections);
            setAlways("network", "enable_ssl", config_.network_enable_ssl);
            setIfStr("network", "ssl_cert_path", config_.network_ssl_cert_path);
            setIfStr("network", "ssl_key_path", config_.network_ssl_key_path);

            // ── canopen (hal.canopen – single source of truth) ───────
            ensureSection("hal");
            if (!j["hal"].contains("canopen")) j["hal"]["canopen"] = json::object();
            auto& hc = j["hal"]["canopen"];
            setIfStr("canopen", "interface_name", config_.canopen_interface);  // legacy compat
            hc["interface_name"] = config_.canopen_interface;
            setIfInt("canopen", "node_id", config_.canopen_node_id);           // legacy compat
            hc["node_id"] = config_.canopen_node_id;
            hc["bitrate"] = config_.canopen_bitrate;
            hc["use_sync"] = config_.canopen_use_sync;
            hc["sync_period_ms"] = config_.canopen_sync_period_ms;
            setIfStr("canopen", "accel_mode", config_.canopen_accel_mode);     // legacy compat
            hc["accel_mode"] = config_.canopen_accel_mode;
            hc["pdo_config_enabled"] = config_.canopen_pdo_config_enabled;

            // ── mount ────────────────────────────────────────────────
            ensureSection("mount");
            auto& m = j["mount"];

            // Mount type — only write if not default (EQUATORIAL)
            if (config_.mount_type != MountType::EQUATORIAL || !m.contains("type")) {
                switch (config_.mount_type) {
                    case MountType::EQUATORIAL:  m["type"] = "equatorial"; break;
                    case MountType::ALT_AZ:      m["type"] = "alt_az";     break;
                    case MountType::CASUAL:      m["type"] = "casual";     break;
                    default:                     m["type"] = "equatorial"; break;
                }
            }

            // Location — always update (0.0 is a valid coordinate)
            if (config_.latitude != 0.0 || !m.contains("latitude"))
                m["latitude"] = config_.latitude;
            if (config_.longitude != 0.0 || !m.contains("longitude"))
                m["longitude"] = config_.longitude;
            if (config_.altitude != 0.0 || !m.contains("altitude"))
                m["altitude"] = config_.altitude;

            // Physical / rate params — only update if non-zero
            if (config_.mount_height != 0.0) m["mount_height"] = config_.mount_height;
            if (config_.pier_west != 0.0) m["pier_west"] = config_.pier_west;
            if (config_.pier_east != 0.0) m["pier_east"] = config_.pier_east;
            if (config_.default_temperature != 0.0) m["default_temperature"] = config_.default_temperature;
            if (config_.default_pressure != 0.0) m["default_pressure"] = config_.default_pressure;
            if (config_.default_humidity != 0.0) m["default_humidity"] = config_.default_humidity;

            // Encoder booleans — always update
            m["use_encoders"] = config_.use_encoders;
            m["encoders_absolute"] = config_.encoders_absolute;
            if (config_.encoder_resolution != 0.0) m["encoder_resolution"] = config_.encoder_resolution;

            if (config_.max_slew_rate != 0.0) m["max_slew_rate"] = config_.max_slew_rate;
            if (config_.max_tracking_rate != 0.0) m["max_tracking_rate"] = config_.max_tracking_rate;
            if (config_.slew_acceleration != 0.0) m["slew_acceleration"] = config_.slew_acceleration;
            if (config_.tracking_acceleration != 0.0) m["tracking_acceleration"] = config_.tracking_acceleration;

            if (config_.position_tolerance != 0.0) m["position_tolerance"] = config_.position_tolerance;
            if (config_.rate_tolerance != 0.0) m["rate_tolerance"] = config_.rate_tolerance;

            // Meridian / limits booleans — always update
            m["meridian_flip_enabled"] = config_.meridian_flip_enabled;
            if (config_.meridian_flip_delay_minutes != 0.0) m["meridian_flip_delay_minutes"] = config_.meridian_flip_delay_minutes;
            if (config_.meridian_flip_hysteresis_degrees != 0.0) m["meridian_flip_hysteresis_degrees"] = config_.meridian_flip_hysteresis_degrees;
            if (config_.meridian_flip_timeout_seconds != 0.0) m["meridian_flip_timeout_seconds"] = config_.meridian_flip_timeout_seconds;

            m["soft_limits_enabled"] = config_.soft_limits_enabled;
            m["soft_limit_axis1_min"] = config_.soft_limit_axis1_min;
            m["soft_limit_axis1_max"] = config_.soft_limit_axis1_max;
            m["soft_limit_axis2_min"] = config_.soft_limit_axis2_min;
            m["soft_limit_axis2_max"] = config_.soft_limit_axis2_max;
            if (config_.soft_limit_warning_degrees != 0.0) m["soft_limit_warning_degrees"] = config_.soft_limit_warning_degrees;
            if (config_.soft_limit_deceleration_degrees != 0.0) m["soft_limit_deceleration_degrees"] = config_.soft_limit_deceleration_degrees;
            if (config_.soft_limit_tracking_rate_factor != 0.0) m["soft_limit_tracking_rate_factor"] = config_.soft_limit_tracking_rate_factor;

            m["park_position_axis1"] = config_.park_position_axis1;
            m["park_position_axis2"] = config_.park_position_axis2;
            m["enable_refraction_correction"] = config_.enable_refraction_correction;

            // Orientation quaternion — always update
            m["orientation_quaternion"] = {
                config_.mount_orientation.quaternion[0],
                config_.mount_orientation.quaternion[1],
                config_.mount_orientation.quaternion[2],
                config_.mount_orientation.quaternion[3]
            };

            // ── axis_physical_parameters ─────────────────────────────
            if (!m.contains("axis_physical_parameters"))
                m["axis_physical_parameters"] = json::object();
            auto& app = m["axis_physical_parameters"];

            // Ha_axis — only update fields with explicit (non-zero) values
            if (!app.contains("ha_axis")) app["ha_axis"] = json::object();
            auto& ha = app["ha_axis"];
            if (config_.ha_axis_params.position_counts_per_degree != 0.0) ha["position_counts_per_degree"] = config_.ha_axis_params.position_counts_per_degree;
            if (config_.ha_axis_params.velocity_counts_per_deg_s != 0.0) ha["velocity_counts_per_deg_s"] = config_.ha_axis_params.velocity_counts_per_deg_s;
            if (config_.ha_axis_params.encoder_resolution != 0.0) ha["encoder_resolution"] = config_.ha_axis_params.encoder_resolution;
            if (config_.ha_axis_params.encoder_counts_per_arcsec != 0.0) ha["encoder_counts_per_arcsec"] = config_.ha_axis_params.encoder_counts_per_arcsec;
            if (config_.ha_axis_params.encoder_quantization_error != 0.0) ha["encoder_quantization_error"] = config_.ha_axis_params.encoder_quantization_error;
            if (config_.ha_axis_params.gear_ratio != 0.0) ha["gear_ratio"] = config_.ha_axis_params.gear_ratio;
            if (config_.ha_axis_params.worm_ratio != 0.0) ha["worm_ratio"] = config_.ha_axis_params.worm_ratio;
            if (config_.ha_axis_params.worm_teeth != 0) ha["worm_teeth"] = config_.ha_axis_params.worm_teeth;
            if (config_.ha_axis_params.worm_wheel_teeth != 0) ha["worm_wheel_teeth"] = config_.ha_axis_params.worm_wheel_teeth;
            if (config_.ha_axis_params.cyclic_error_amplitude != 0.0) ha["cyclic_error_amplitude"] = config_.ha_axis_params.cyclic_error_amplitude;
            if (config_.ha_axis_params.cyclic_error_period != 0.0) ha["cyclic_error_period"] = config_.ha_axis_params.cyclic_error_period;
            if (config_.ha_axis_params.cyclic_error_amplitude != 0.0)
                ha["cyclic_harmonics"] = config_.ha_axis_params.cyclic_harmonics;
            if (config_.ha_axis_params.backlash != 0.0) ha["backlash"] = config_.ha_axis_params.backlash;
            if (config_.ha_axis_params.backlash_temp_coeff != 0.0) ha["backlash_temp_coeff"] = config_.ha_axis_params.backlash_temp_coeff;
            if (config_.ha_axis_params.axis_stiffness != 0.0) ha["axis_stiffness"] = config_.ha_axis_params.axis_stiffness;
            if (config_.ha_axis_params.torsional_compliance != 0.0) ha["torsional_compliance"] = config_.ha_axis_params.torsional_compliance;
            if (config_.ha_axis_params.expansion_coeff != 0.0) ha["expansion_coeff"] = config_.ha_axis_params.expansion_coeff;
            if (config_.ha_axis_params.temp_gear_error_coeff != 0.0) ha["temp_gear_error_coeff"] = config_.ha_axis_params.temp_gear_error_coeff;
            if (config_.ha_axis_params.calibration_temp != 0.0) ha["calibration_temp"] = config_.ha_axis_params.calibration_temp;
            if (!config_.ha_axis_params.calibration_table.empty())
                ha["calibration_table"] = config_.ha_axis_params.calibration_table;

            // dec_axis
            if (!app.contains("dec_axis")) app["dec_axis"] = json::object();
            auto& dec = app["dec_axis"];
            if (config_.dec_axis_params.position_counts_per_degree != 0.0) dec["position_counts_per_degree"] = config_.dec_axis_params.position_counts_per_degree;
            if (config_.dec_axis_params.velocity_counts_per_deg_s != 0.0) dec["velocity_counts_per_deg_s"] = config_.dec_axis_params.velocity_counts_per_deg_s;
            if (config_.dec_axis_params.encoder_resolution != 0.0) dec["encoder_resolution"] = config_.dec_axis_params.encoder_resolution;
            if (config_.dec_axis_params.encoder_counts_per_arcsec != 0.0) dec["encoder_counts_per_arcsec"] = config_.dec_axis_params.encoder_counts_per_arcsec;
            if (config_.dec_axis_params.encoder_quantization_error != 0.0) dec["encoder_quantization_error"] = config_.dec_axis_params.encoder_quantization_error;
            if (config_.dec_axis_params.gear_ratio != 0.0) dec["gear_ratio"] = config_.dec_axis_params.gear_ratio;
            if (config_.dec_axis_params.worm_ratio != 0.0) dec["worm_ratio"] = config_.dec_axis_params.worm_ratio;
            if (config_.dec_axis_params.worm_teeth != 0) dec["worm_teeth"] = config_.dec_axis_params.worm_teeth;
            if (config_.dec_axis_params.worm_wheel_teeth != 0) dec["worm_wheel_teeth"] = config_.dec_axis_params.worm_wheel_teeth;
            if (config_.dec_axis_params.cyclic_error_amplitude != 0.0) dec["cyclic_error_amplitude"] = config_.dec_axis_params.cyclic_error_amplitude;
            if (config_.dec_axis_params.cyclic_error_period != 0.0) dec["cyclic_error_period"] = config_.dec_axis_params.cyclic_error_period;
            if (config_.dec_axis_params.cyclic_error_amplitude != 0.0)
                dec["cyclic_harmonics"] = config_.dec_axis_params.cyclic_harmonics;
            if (config_.dec_axis_params.backlash != 0.0) dec["backlash"] = config_.dec_axis_params.backlash;
            if (config_.dec_axis_params.backlash_temp_coeff != 0.0) dec["backlash_temp_coeff"] = config_.dec_axis_params.backlash_temp_coeff;
            if (config_.dec_axis_params.axis_stiffness != 0.0) dec["axis_stiffness"] = config_.dec_axis_params.axis_stiffness;
            if (config_.dec_axis_params.torsional_compliance != 0.0) dec["torsional_compliance"] = config_.dec_axis_params.torsional_compliance;
            if (config_.dec_axis_params.expansion_coeff != 0.0) dec["expansion_coeff"] = config_.dec_axis_params.expansion_coeff;
            if (config_.dec_axis_params.temp_gear_error_coeff != 0.0) dec["temp_gear_error_coeff"] = config_.dec_axis_params.temp_gear_error_coeff;
            if (config_.dec_axis_params.calibration_temp != 0.0) dec["calibration_temp"] = config_.dec_axis_params.calibration_temp;
            if (!config_.dec_axis_params.calibration_table.empty())
                dec["calibration_table"] = config_.dec_axis_params.calibration_table;

            // ── telescope ────────────────────────────────────────────
            if (config_.focal_length != 0.0) {
                ensureSection("telescope");
                j["telescope"]["focal_length"] = config_.focal_length;
            }
            if (config_.aperture != 0.0) {
                ensureSection("telescope");
                j["telescope"]["aperture"] = config_.aperture;
            }

            // ── guider ───────────────────────────────────────────────
            ensureSection("guider");
            j["guider"]["enabled"] = config_.enable_guider;
            if (config_.guider_max_correction != 0.0)
                j["guider"]["max_correction"] = config_.guider_max_correction;
            if (config_.guider_aggression != 0.0)
                j["guider"]["aggression"] = config_.guider_aggression;

            // ── kalman ───────────────────────────────────────────────
            if (config_.process_noise != 0.0) {
                ensureSection("kalman");
                j["kalman"]["process_noise"] = config_.process_noise;
            }
            if (config_.measurement_noise != 0.0) {
                ensureSection("kalman");
                j["kalman"]["measurement_noise"] = config_.measurement_noise;
            }

            // ── hal (gamepad, PID, safety, canopen) ─────────────────
            // Serialize the HAL config (which includes gamepad, PID, safety)
            // using its built-in JSON serializer.
            j["hal"] = hal_config_.toJson();

            // ── tpoint ───────────────────────────────────────────────
            if (config_.tpoint_enabled_terms != 0) {
                ensureSection("tpoint");
                j["tpoint"]["enabled_terms"] = config_.tpoint_enabled_terms;
            }

            // ── Backup existing file before overwriting ────────────
            if (std::filesystem::exists(config_file_path_)) {
                auto now = std::chrono::system_clock::now();
                auto time_t_now = std::chrono::system_clock::to_time_t(now);
                std::tm tm_now;
                localtime_r(&time_t_now, &tm_now);
                char ts_buf[32];
                std::strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%d_%H-%M-%S", &tm_now);
                // Backup format: {stem}_{timestamp}{ext}
                // e.g. config/default_2026-06-14_10-05-08.json
                std::filesystem::path cfg_path(config_file_path_);
                std::string backup_path = (cfg_path.parent_path() /
                    (cfg_path.stem().string() + "_" + ts_buf + cfg_path.extension().string())).string();
                try {
                    std::filesystem::copy_file(config_file_path_, backup_path,
                                  std::filesystem::copy_options::overwrite_existing);
                    MOUNT_LOG_INFO("Config backup created: {}", backup_path);
                } catch (const std::exception& e) {
                    MOUNT_LOG_WARN("Failed to create config backup {}: {}", backup_path, e.what());
                }
            }

            // ── Write to file ────────────────────────────────────────
            std::ofstream out_file(config_file_path_);
            if (out_file.is_open()) {
                out_file << j.dump(4);
                out_file.close();
                MOUNT_LOG_INFO("Configuration saved to {}", config_file_path_);
            } else {
                MOUNT_LOG_ERROR("Failed to open config file for writing: {}", config_file_path_);
            }
        } catch (const std::exception& e) {
            MOUNT_LOG_ERROR("Failed to save configuration to {}: {}", config_file_path_, e.what());
        }
    }
    
    // Ephemeris tracking methods
    bool uploadEphemeris(const std::string& object_id,
                        const std::string& object_name,
                        const std::string& object_type,
                        const std::vector<std::tuple<std::chrono::system_clock::time_point,
                                                      double, double, double, double>>& points,
                        int interpolation_order = 3) {
        // Convert points to proto format
        ::astro_mount::EphemerisData ephemeris_data;
        ephemeris_data.set_object_id(object_id);
        ephemeris_data.set_object_name(object_name);
        ephemeris_data.set_object_type(object_type);
        ephemeris_data.set_interpolation_order(interpolation_order);
        
        for (const auto& point : points) {
            auto [timestamp, ra, dec, ra_rate, dec_rate] = point;
            auto* ep_point = ephemeris_data.add_points();
            auto* ts = ep_point->mutable_timestamp();
            ts->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(
                timestamp.time_since_epoch()).count());
            ts->set_nanos(0);
            ep_point->set_ra(ra);
            ep_point->set_dec(dec);
            ep_point->set_ra_rate(ra_rate);
            ep_point->set_dec_rate(dec_rate);
        }
        
        if (!ephemeris_manager_) {
            ephemeris_manager_ = std::make_unique<models::EphemerisTrackerManager>();
            ephemeris_manager_->setObserverLocation(config_.latitude, config_.longitude, config_.altitude);
        }
        
        return ephemeris_manager_->uploadEphemeris(ephemeris_data);
    }
    
    std::string startEphemerisTracking(
        const std::string& object_id,
        const std::chrono::system_clock::time_point& start_time,
        double lead_time_seconds = 30.0,
        bool wait_at_start = true,
        bool enable_prediction = false,
        double prediction_interval_hours = 1.0,
        const std::string& tracking_mode = "continuous",
        double custom_rate_ra = 0.0,
        double custom_rate_dec = 0.0) {
        
        if (!ephemeris_manager_) {
            return "";
        }
        
        models::EphemerisTracker::TrackingConfig config;
        config.lead_time_seconds = lead_time_seconds;
        config.wait_at_start = wait_at_start;
        config.enable_prediction = enable_prediction;
        config.prediction_interval_hours = prediction_interval_hours;
        config.tracking_mode = tracking_mode;
        config.custom_rate_ra = custom_rate_ra;
        config.custom_rate_dec = custom_rate_dec;
        
        return ephemeris_manager_->startTracking(object_id, start_time, config);
    }
    
    std::string startEphemerisTrackingWithData(
        const std::string& object_id,
        const std::string& object_name,
        const std::string& object_type,
        const std::vector<std::tuple<std::chrono::system_clock::time_point,
                                      double, double, double, double>>& points,
        const std::chrono::system_clock::time_point& start_time,
        double lead_time_seconds = 30.0,
        int interpolation_order = 3,
        const std::string& tracking_mode = "continuous") {
        
        // Upload ephemeris first
        if (!uploadEphemeris(object_id, object_name, object_type, points, interpolation_order)) {
            return "";
        }
        
        // Start tracking
        return startEphemerisTracking(object_id, start_time, lead_time_seconds,
                                     true, false, 1.0, tracking_mode, 0.0, 0.0);
    }
    
    bool stopEphemerisTracking(const std::string& tracker_id) {
        if (!ephemeris_manager_) {
            return false;
        }
        return ephemeris_manager_->stopTracking(tracker_id);
    }
    
    ::astro_mount::EphemerisTrackStatus getEphemerisTrackStatus(
        const std::string& tracker_id) const {
        if (!ephemeris_manager_) {
            return ::astro_mount::EphemerisTrackStatus();
        }
        auto tracker = ephemeris_manager_->getTracker(tracker_id);
        if (!tracker) {
            return ::astro_mount::EphemerisTrackStatus();
        }
        return tracker->getStatus();
    }
    
    std::vector<std::string> getActiveEphemerisTrackers() const {
        if (!ephemeris_manager_) {
            return {};
        }
        auto trackers = ephemeris_manager_->getActiveTrackers();
        std::vector<std::string> ids;
        for (const auto& [id, _] : trackers) {
            ids.push_back(id);
        }
        return ids;
    }
    
    void clearEphemerisCache() {
        if (ephemeris_manager_) {
            ephemeris_manager_->clearCache();
        }
    }
    
    ::astro_mount::EphemerisMetrics getEphemerisMetrics() const {
        if (!ephemeris_manager_) {
            return ::astro_mount::EphemerisMetrics();
        }
        return ephemeris_manager_->getMetrics();
    }
    
    // ============================================
    // FIELD ROTATION / DEROTATOR CONTROL
    // ============================================
    
    bool configureDerotator(const ::astro_mount::DerotatorConfig& config) {
        return derotator_->configure(config);
    }
    
    bool enableFieldRotation(const ::astro_mount::FieldRotationParams& params) {
        // Compute field rotation rate based on mount type and position.
        // MountController retains this calculation because it depends on
        // mount axis positions (axis1_position_, axis2_position_) which are
        // owned by Impl. The result is pushed to DerotatorController via
        // setFieldRotationRate(), which is then consumed by controlFieldRotation().
        double rate_deg_s = 0.0;
        if ((config_.mount_type == MountType::ALT_AZ || config_.mount_type == MountType::CASUAL) && params.enabled()) {
            // Formula: rate = -ω * cos(lat) / sin(alt)
            // where ω = sidereal rate, lat = latitude, alt = altitude.
            //
            // For ALT_AZ mounts, alt is the true altitude (axis2).
            // For CASUAL mounts, alt is the mount-frame altitude-like axis (axis1).
            
            // NaN/Inf guard on latitude
            double lat_rad;
            if (std::isfinite(config_.latitude)) {
                lat_rad = config_.latitude * M_PI / 180.0;
            } else {
                MOUNT_LOG_WARN("enableFieldRotation: non-finite latitude {}, using 52° default",
                         config_.latitude);
                lat_rad = 52.0 * M_PI / 180.0;
            }
            
            // Get altitude from mount position (CASUAL uses axis1, ALT_AZ uses params)
            double altitude_deg;
            if (config_.mount_type == MountType::CASUAL) {
                std::lock_guard<std::shared_mutex> lock(*state_mutex_);
                altitude_deg = axis1_position_;
            } else {
                altitude_deg = params.altitude();
            }
            
            // NaN/Inf guard on altitude
            if (!std::isfinite(altitude_deg)) {
                MOUNT_LOG_WARN("enableFieldRotation: non-finite altitude {}, using 45° default",
                         altitude_deg);
                altitude_deg = 45.0;
            }
            
            double alt_rad = altitude_deg * M_PI / 180.0;
            double sin_alt = std::sin(alt_rad);
            
            // Clamp sin(alt) away from zero to prevent division by zero
            const double MIN_SIN_ALT = std::sin(1.0 * M_PI / 180.0);
            if (std::abs(sin_alt) < MIN_SIN_ALT) {
                sin_alt = std::copysign(MIN_SIN_ALT, sin_alt);
                MOUNT_LOG_DEBUG("Field rotation alt clamp: altitude={:.2f}° clamped to sin(alt)={:.6f}",
                         altitude_deg, sin_alt);
            }
            
            double cos_lat = std::cos(lat_rad);
            double sidereal_rate_rad = 2.0 * M_PI / 86164.0905;
            double field_rotation_rate = -sidereal_rate_rad * cos_lat / sin_alt;
            
            // Clamp final rate to ±20 deg/s
            const double MAX_RATE_DEG_S = 20.0;
            rate_deg_s = std::clamp(
                field_rotation_rate * 180.0 / M_PI,
                -MAX_RATE_DEG_S, MAX_RATE_DEG_S);
            
            MOUNT_LOG_DEBUG("Field rotation rate computed: {:.4f} deg/s (lat={:.1f}°, alt={:.1f}°)",
                     rate_deg_s, config_.latitude, altitude_deg);
        }
        
        // Push computed rate to DerotatorController, then delegate enable/disable
        derotator_->setFieldRotationRate(rate_deg_s);
        return derotator_->enableFieldRotation(params);
    }
    
    bool controlFieldRotation(const ::astro_mount::FieldRotationControlRequest& request) {
        return derotator_->controlFieldRotation(request);
    }
    
    ::astro_mount::DerotatorStatus getDerotatorStatus() const {
        return derotator_->getStatus();
    }
    
    bool homeDerotator(const ::astro_mount::DerotatorHomingRequest& request) {
        return derotator_->home(request);
    }
    
    ::astro_mount::FieldRotationParams getFieldRotationParams() const {
        return derotator_->getFieldRotationParams();
    }
    
    ICanOpenInterface* getCanOpenInterfacePtr() {
        return canopen_interface_.get();
    }
    
    std::shared_ptr<ICanOpenInterface> getCanOpenInterfaceShared() {
        if (!canopen_interface_) {
            throw std::runtime_error("CANopen interface not initialized");
        }
        return canopen_interface_;
    }
    
    // ============================================
    // HAL Configuration & Status API
    // ============================================
    
    bool getHALConfig(::astro_mount::HALConfig& config) const {
        // Always return the stored HAL configuration, even when no
        // live HAL interface is active (e.g. during simulated/test runs).
        // The UI needs this data to populate the Settings → HAL panel.
        // Convert internal hal::HALConfig to proto HALConfig
        switch (hal_config_.type) {
            case hal::HALType::SIMULATED:
                config.set_type(::astro_mount::HAL_SIMULATED);
                break;
            case hal::HALType::CANOPEN:
                config.set_type(::astro_mount::HAL_CANOPEN);
                break;
            case hal::HALType::SERIAL:
                config.set_type(::astro_mount::HAL_SERIAL);
                break;
            case hal::HALType::ETHERNET:
                config.set_type(::astro_mount::HAL_ETHERNET);
                break;
            case hal::HALType::CUSTOM:
                config.set_type(::astro_mount::HAL_CUSTOM);
                break;
        }
        config.set_name(hal_config_.name);
        
        // Simulated config
        auto* sim = config.mutable_simulated();
        sim->set_enable_simulation(hal_config_.simulated.enable_simulation);
        sim->set_simulation_update_rate(hal_config_.simulated.simulation_update_rate);
        sim->set_position_noise_stddev(hal_config_.simulated.position_noise_stddev);
        sim->set_velocity_noise_stddev(hal_config_.simulated.velocity_noise_stddev);
        sim->set_simulate_errors(hal_config_.simulated.simulate_errors);
        sim->set_error_probability(hal_config_.simulated.error_probability);
        
        // CANopen config
        auto* can = config.mutable_canopen();
        can->set_library(hal_config_.canopen.library);
        can->set_interface_name(hal_config_.canopen.interface_name);
        can->set_bitrate(hal_config_.canopen.bitrate);
        can->set_node_id(hal_config_.canopen.node_id);
        can->set_use_sync(hal_config_.canopen.use_sync);
        can->set_sync_period_ms(hal_config_.canopen.sync_period_ms);
        can->set_sdo_timeout_ms(hal_config_.canopen.sdo_timeout_ms);
        can->set_pdo_update_rate(hal_config_.canopen.pdo_update_rate);
        
        // Serial config
        auto* ser = config.mutable_serial();
        ser->set_port(hal_config_.serial.port);
        ser->set_baud_rate(hal_config_.serial.baud_rate);
        ser->set_protocol(hal_config_.serial.protocol);
        ser->set_data_bits(hal_config_.serial.data_bits);
        ser->set_stop_bits(hal_config_.serial.stop_bits);
        ser->set_parity(hal_config_.serial.parity);
        ser->set_timeout_ms(hal_config_.serial.timeout_ms);
        
        // Ethernet config
        auto* eth = config.mutable_ethernet();
        eth->set_ip_address(hal_config_.ethernet.ip_address);
        eth->set_port(hal_config_.ethernet.port);
        eth->set_protocol(hal_config_.ethernet.protocol);
        eth->set_timeout_ms(hal_config_.ethernet.timeout_ms);
        eth->set_retry_count(hal_config_.ethernet.retry_count);
        
        // Axis configs
        for (const auto& axis : hal_config_.axes) {
            auto* proto_axis = config.add_axes();
            proto_axis->set_id(axis.id);
            proto_axis->set_name(axis.name);
            // Axis-level safety limits
            proto_axis->mutable_safety_limits()->set_min_position(axis.safety_limits.min_position);
            proto_axis->mutable_safety_limits()->set_max_position(axis.safety_limits.max_position);
            proto_axis->mutable_safety_limits()->set_max_velocity(axis.safety_limits.max_velocity);
            proto_axis->mutable_safety_limits()->set_max_acceleration(axis.safety_limits.max_acceleration);
            proto_axis->mutable_safety_limits()->set_max_current(axis.safety_limits.max_current);
            proto_axis->mutable_safety_limits()->set_max_temperature(axis.safety_limits.max_temperature);
        }
        
        // PID params
        config.mutable_pid_params()->set_kp(hal_config_.pid_params.kp);
        config.mutable_pid_params()->set_ki(hal_config_.pid_params.ki);
        config.mutable_pid_params()->set_kd(hal_config_.pid_params.kd);
        config.mutable_pid_params()->set_integral_limit(hal_config_.pid_params.integral_limit);
        config.mutable_pid_params()->set_output_limit(hal_config_.pid_params.output_limit);
        config.mutable_pid_params()->set_anti_windup_gain(hal_config_.pid_params.anti_windup_gain);
        config.mutable_pid_params()->set_enable_anti_windup(hal_config_.pid_params.enable_anti_windup);
        
        // Safety config
        config.mutable_safety()->set_enable_limits(hal_config_.safety.enable_limits);
        config.mutable_safety()->set_enable_emergency_stop(hal_config_.safety.enable_emergency_stop);
        config.mutable_safety()->set_emergency_stop_timeout_ms(hal_config_.safety.emergency_stop_timeout_ms);
        config.mutable_safety()->set_enable_temperature_monitoring(hal_config_.safety.enable_temperature_monitoring);
        config.mutable_safety()->set_enable_current_monitoring(hal_config_.safety.enable_current_monitoring);
        config.mutable_safety()->set_enable_voltage_monitoring(hal_config_.safety.enable_voltage_monitoring);
        config.mutable_safety()->set_min_voltage(hal_config_.safety.min_voltage);
        config.mutable_safety()->set_max_voltage(hal_config_.safety.max_voltage);
        config.mutable_safety()->set_monitoring_rate(hal_config_.safety.monitoring_rate);
        
        // Gamepad config
        auto* gp = config.mutable_gamepad();
        gp->set_device_path(hal_config_.gamepad.device_path);
        gp->set_dead_zone(hal_config_.gamepad.deadzone);
        gp->set_sensitivity(hal_config_.gamepad.sensitivity);
        gp->set_read_frequency(hal_config_.gamepad.update_rate_hz);
        gp->set_autostart(hal_config_.gamepad.autostart);
        gp->set_gamepad_mode(static_cast<::astro_mount::GamepadMode>(hal_config_.gamepad.gamepad_mode));
        
        return true;
    }
    
    bool setHALConfig(const ::astro_mount::HALConfigRequest& request) {
        // When there is no HAL interface (simulated mode, default constructor),
        // we can still update the config in memory for gamepad, PID, safety etc.
        if (!hal_interface_) {
            MOUNT_LOG_INFO("setHALConfig: no HAL interface, updating config in memory only");
            const auto& req_config = request.config();
            
            // Update gamepad config in place
            if (req_config.has_gamepad()) {
                const auto& gp = req_config.gamepad();
                if (!gp.device_path().empty())
                    hal_config_.gamepad.device_path = gp.device_path();
                if (gp.dead_zone() != 0.0)
                    hal_config_.gamepad.deadzone = gp.dead_zone();
                if (gp.sensitivity() != 0.0)
                    hal_config_.gamepad.sensitivity = gp.sensitivity();
                if (gp.read_frequency() != 0.0)
                    hal_config_.gamepad.update_rate_hz = gp.read_frequency();
                hal_config_.gamepad.autostart = gp.autostart();
                hal_config_.gamepad.gamepad_mode = static_cast<int>(gp.gamepad_mode());
                gamepad_mode_ = hal_config_.gamepad.gamepad_mode;
            }
            
            // Update PID config in place
            if (req_config.has_pid_params()) {
                const auto& pid = req_config.pid_params();
                if (pid.kp() != 0.0) hal_config_.pid_params.kp = pid.kp();
                if (pid.ki() != 0.0) hal_config_.pid_params.ki = pid.ki();
                if (pid.kd() != 0.0) hal_config_.pid_params.kd = pid.kd();
                if (pid.integral_limit() != 0.0) hal_config_.pid_params.integral_limit = pid.integral_limit();
                if (pid.output_limit() != 0.0) hal_config_.pid_params.output_limit = pid.output_limit();
                if (pid.anti_windup_gain() != 0.0) hal_config_.pid_params.anti_windup_gain = pid.anti_windup_gain();
                hal_config_.pid_params.enable_anti_windup = pid.enable_anti_windup();
            }
            
            // Update safety config in place
            if (req_config.has_safety()) {
                const auto& saf = req_config.safety();
                hal_config_.safety.enable_limits = saf.enable_limits();
                hal_config_.safety.enable_emergency_stop = saf.enable_emergency_stop();
                if (saf.emergency_stop_timeout_ms() != 0)
                    hal_config_.safety.emergency_stop_timeout_ms = saf.emergency_stop_timeout_ms();
                hal_config_.safety.enable_temperature_monitoring = saf.enable_temperature_monitoring();
                hal_config_.safety.enable_current_monitoring = saf.enable_current_monitoring();
                hal_config_.safety.enable_voltage_monitoring = saf.enable_voltage_monitoring();
                if (saf.min_voltage() != 0.0) hal_config_.safety.min_voltage = saf.min_voltage();
                if (saf.max_voltage() != 0.0) hal_config_.safety.max_voltage = saf.max_voltage();
                if (saf.monitoring_rate() != 0) hal_config_.safety.monitoring_rate = saf.monitoring_rate();
            }
            
            // Persist to disk if a config file path has been set
            if (!config_file_path_.empty()) {
                saveConfigToFile();
            }
            
            return true;
        }
        
        const auto& req_config = request.config();
        MOUNT_LOG_INFO("setHALConfig: has_gamepad={} has_simulated={} has_canopen={} has_safety={} has_pid={}",
                       req_config.has_gamepad(), req_config.has_simulated(),
                       req_config.has_canopen(), req_config.has_safety(),
                       req_config.has_pid_params());
        
        // Start from the CURRENT config so that partial updates don't
        // overwrite untouched fields with defaults
        hal::HALConfig new_config = hal_config_;
        
        // Map HAL type (only if explicitly set — proto3 default is HAL_SIMULATED=0)
        if (req_config.type() != ::astro_mount::HAL_SIMULATED || req_config.name() != "") {
            switch (req_config.type()) {
                case ::astro_mount::HAL_SIMULATED:
                    new_config.type = hal::HALType::SIMULATED;
                    break;
                case ::astro_mount::HAL_CANOPEN:
                    new_config.type = hal::HALType::CANOPEN;
                    break;
                case ::astro_mount::HAL_SERIAL:
                    new_config.type = hal::HALType::SERIAL;
                    break;
                case ::astro_mount::HAL_ETHERNET:
                    new_config.type = hal::HALType::ETHERNET;
                    break;
                case ::astro_mount::HAL_CUSTOM:
                    new_config.type = hal::HALType::CUSTOM;
                    break;
                default:
                    break;
            }
        }
        if (!req_config.name().empty()) {
            new_config.name = req_config.name();
        }
        
        // Simulated config
        if (req_config.has_simulated()) {
            new_config.simulated.enable_simulation = req_config.simulated().enable_simulation();
            new_config.simulated.simulation_update_rate = req_config.simulated().simulation_update_rate();
            new_config.simulated.position_noise_stddev = req_config.simulated().position_noise_stddev();
            new_config.simulated.velocity_noise_stddev = req_config.simulated().velocity_noise_stddev();
            new_config.simulated.simulate_errors = req_config.simulated().simulate_errors();
            new_config.simulated.error_probability = req_config.simulated().error_probability();
        }
        
        // CANopen config
        if (req_config.has_canopen()) {
            new_config.canopen.library = req_config.canopen().library();
            new_config.canopen.interface_name = req_config.canopen().interface_name();
            new_config.canopen.bitrate = req_config.canopen().bitrate();
            new_config.canopen.node_id = req_config.canopen().node_id();
            new_config.canopen.use_sync = req_config.canopen().use_sync();
            new_config.canopen.sync_period_ms = req_config.canopen().sync_period_ms();
            new_config.canopen.sdo_timeout_ms = req_config.canopen().sdo_timeout_ms();
            new_config.canopen.pdo_update_rate = req_config.canopen().pdo_update_rate();
        }
        
        // Serial config
        if (req_config.has_serial()) {
            new_config.serial.port = req_config.serial().port();
            new_config.serial.baud_rate = req_config.serial().baud_rate();
            new_config.serial.protocol = req_config.serial().protocol();
            new_config.serial.data_bits = req_config.serial().data_bits();
            new_config.serial.stop_bits = req_config.serial().stop_bits();
            new_config.serial.parity = req_config.serial().parity();
            new_config.serial.timeout_ms = req_config.serial().timeout_ms();
        }
        
        // Ethernet config
        if (req_config.has_ethernet()) {
            new_config.ethernet.ip_address = req_config.ethernet().ip_address();
            new_config.ethernet.port = req_config.ethernet().port();
            new_config.ethernet.protocol = req_config.ethernet().protocol();
            new_config.ethernet.timeout_ms = req_config.ethernet().timeout_ms();
            new_config.ethernet.retry_count = req_config.ethernet().retry_count();
        }
        
        // Gamepad config
        if (req_config.has_gamepad()) {
            const auto& gp = req_config.gamepad();
            if (!gp.device_path().empty())
                new_config.gamepad.device_path = gp.device_path();
            if (gp.dead_zone() != 0.0)
                new_config.gamepad.deadzone = gp.dead_zone();
            if (gp.sensitivity() != 0.0)
                new_config.gamepad.sensitivity = gp.sensitivity();
            if (gp.read_frequency() != 0.0)
                new_config.gamepad.update_rate_hz = gp.read_frequency();
            // autostart is a boolean – proto3 default is false, so we always apply it
            new_config.gamepad.autostart = gp.autostart();
            // gamepad_mode: proto3 default is GAMEPAD_RAW=0, always apply
            new_config.gamepad.gamepad_mode = static_cast<int>(gp.gamepad_mode());
        }
        
        // PID config
        if (req_config.has_pid_params()) {
            const auto& pid = req_config.pid_params();
            if (pid.kp() != 0.0) new_config.pid_params.kp = pid.kp();
            if (pid.ki() != 0.0) new_config.pid_params.ki = pid.ki();
            if (pid.kd() != 0.0) new_config.pid_params.kd = pid.kd();
            if (pid.integral_limit() != 0.0) new_config.pid_params.integral_limit = pid.integral_limit();
            if (pid.output_limit() != 0.0) new_config.pid_params.output_limit = pid.output_limit();
            if (pid.anti_windup_gain() != 0.0) new_config.pid_params.anti_windup_gain = pid.anti_windup_gain();
            new_config.pid_params.enable_anti_windup = pid.enable_anti_windup();
        }
        
        // Safety config
        if (req_config.has_safety()) {
            const auto& saf = req_config.safety();
            new_config.safety.enable_limits = saf.enable_limits();
            new_config.safety.enable_emergency_stop = saf.enable_emergency_stop();
            if (saf.emergency_stop_timeout_ms() != 0)
                new_config.safety.emergency_stop_timeout_ms = saf.emergency_stop_timeout_ms();
            new_config.safety.enable_temperature_monitoring = saf.enable_temperature_monitoring();
            new_config.safety.enable_current_monitoring = saf.enable_current_monitoring();
            new_config.safety.enable_voltage_monitoring = saf.enable_voltage_monitoring();
            if (saf.min_voltage() != 0.0) new_config.safety.min_voltage = saf.min_voltage();
            if (saf.max_voltage() != 0.0) new_config.safety.max_voltage = saf.max_voltage();
            if (saf.monitoring_rate() != 0) new_config.safety.monitoring_rate = saf.monitoring_rate();
        }
        
        // Check if the config actually changed before doing a full reinit
        // For gamepad-only changes, we can just update the config in place
        bool needs_reinit = (new_config.type != hal_config_.type ||
                            new_config.canopen.library != hal_config_.canopen.library ||
                            new_config.canopen.interface_name != hal_config_.canopen.interface_name ||
                            new_config.canopen.bitrate != hal_config_.canopen.bitrate);
        
        if (!needs_reinit) {
            // Just update config in memory — no HAL restart needed
            hal_config_ = new_config;
            MOUNT_LOG_INFO("setHALConfig: config updated in place (no reinit required)");
            // Persist to disk
            if (!config_file_path_.empty()) {
                saveConfigToFile();
            }
            return true;
        }
        
        // Shutdown current HAL and re-initialize with new config
        // First disable and reset component instances
        if (hal_axis1_motor_) hal_axis1_motor_->disable();
        if (hal_axis2_motor_) hal_axis2_motor_->disable();
        
        hal_sensor_interface_.reset();
        hal_safety_monitor_.reset();
        // DerotatorController manages its own HAL components; reset it so it gets
        // recreated with new HAL components after the HAL re-initialization.
        if (derotator_) {
            derotator_->shutdown();
        }
        derotator_.reset();
        hal_axis1_encoder_.reset();
        hal_axis2_encoder_.reset();
        hal_axis1_motor_.reset();
        hal_axis2_motor_.reset();
        
        hal_interface_->stop();
        hal_interface_->shutdown();
        
        // Initialize with new config
        if (!hal_interface_->initialize(new_config)) {
            MOUNT_LOG_ERROR("setHALConfig: hal_interface_->initialize() failed");
            // Restore old config on failure
            hal_interface_->initialize(hal_config_);
            return false;
        }
        
        // Re-create component instances
        hal_axis1_motor_ = hal_interface_->createMotorControl(0);
        hal_axis2_motor_ = hal_interface_->createMotorControl(1);
        hal_axis1_encoder_ = hal_interface_->createEncoderReader(0);
        hal_axis2_encoder_ = hal_interface_->createEncoderReader(1);
        hal_safety_monitor_ = hal_interface_->createSafetyMonitor();
        hal_sensor_interface_ = hal_interface_->createSensorInterface();
        
        // Re-enable motors
        if (hal_axis1_motor_) hal_axis1_motor_->enable();
        if (hal_axis2_motor_) hal_axis2_motor_->enable();
        
        if (hal_interface_->start()) {
            hal_config_ = new_config;
            MOUNT_LOG_INFO("setHALConfig: HAL reinit successful");
            // Recreate DerotatorController with new HAL components
            if (!recreateDerotator()) {
                MOUNT_LOG_WARN("setHALConfig: failed to recreate DerotatorController, continuing without derotator");
            }
            // Persist to disk
            if (!config_file_path_.empty()) {
                saveConfigToFile();
            }
            return true;
        }
        
        MOUNT_LOG_ERROR("setHALConfig: hal_interface_->start() failed");
        return false;
    }
    
    bool getHALStatus(::astro_mount::HALStatus& status) const {
        // Populate gamepad state first — it's independent of the HAL
        // interface and must always be reported so the UI can show the
        // connection status even when no CANopen / serial HAL is loaded.
        auto populateGamepad = [&]() {
            auto* gs = status.mutable_gamepad();
            if (gamepad_input_ && gamepad_input_->isConnected()) {
                auto state = gamepad_input_->readState();
                gs->set_connected(state.connected);
                gs->set_device_name(gamepad_input_->getDeviceName());
                gs->set_axis_lx(state.axis_lx);
                gs->set_axis_ly(state.axis_ly);
                gs->set_axis_rx(state.axis_rx);
                gs->set_axis_ry(state.axis_ry);
                gs->set_axis_trigger_l(state.axis_trigger_l);
                gs->set_axis_trigger_r(state.axis_trigger_r);
                gs->set_pov_hat(state.pov_hat);
                gs->set_button_stop(state.button_stop);
                gs->set_button_emergency_stop(state.button_emergency_stop);
                gs->set_button_park(state.button_park);
                gs->set_button_speed_up(state.button_speed_up);
                gs->set_button_speed_down(state.button_speed_down);
                gs->set_button_manual_toggle(state.button_manual_toggle);
                gs->set_button_home(state.button_home);
                gs->set_axis_count(gamepad_input_->getAxisCount());
                gs->set_button_count(gamepad_input_->getButtonCount());
                gs->set_gamepad_mode(static_cast<::astro_mount::GamepadMode>(gamepad_mode_));
                gs->set_bootstrap_calibrated(bootstrap_calibrated_);
                gs->set_tpoint_calibrated(tpoint_calibrated_);
                gs->set_max_velocity(gamepad_max_velocity_);
            } else {
                gs->set_connected(false);
                gs->set_gamepad_mode(static_cast<::astro_mount::GamepadMode>(gamepad_mode_));
                gs->set_bootstrap_calibrated(bootstrap_calibrated_);
                gs->set_tpoint_calibrated(tpoint_calibrated_);
                gs->set_max_velocity(gamepad_max_velocity_);
            }
        };

        // Always return basic status even without a live HAL interface,
        // so the UI can show connection state (e.g. gamepad disconnected).
        if (!hal_interface_) {
            status.set_initialized(false);
            status.set_running(false);
            status.set_type(::astro_mount::HAL_CANOPEN);
            status.set_platform_name("HAL not active");
            status.set_hardware_version("N/A");
            status.set_status_message("No HAL interface loaded");
            status.set_error_message("");
            *status.mutable_timestamp() = TimeUtil::GetCurrentTime();
            populateGamepad();
            return true;
        }
        
        status.set_initialized(hal_interface_->isInitialized());
        status.set_running(hal_interface_->isRunning());
        
        // Map HAL type
        switch (hal_config_.type) {
            case hal::HALType::SIMULATED:
                status.set_type(::astro_mount::HAL_SIMULATED);
                break;
            case hal::HALType::CANOPEN:
                status.set_type(::astro_mount::HAL_CANOPEN);
                break;
            case hal::HALType::SERIAL:
                status.set_type(::astro_mount::HAL_SERIAL);
                break;
            case hal::HALType::ETHERNET:
                status.set_type(::astro_mount::HAL_ETHERNET);
                break;
            case hal::HALType::CUSTOM:
                status.set_type(::astro_mount::HAL_CUSTOM);
                break;
        }
        
        status.set_platform_name(hal_interface_->getPlatformName());
        status.set_hardware_version(hal_interface_->getHardwareVersion());
        status.set_status_message(hal_interface_->getStatus());
        status.set_error_message(hal_interface_->getErrorMessages());
        
        // Map supported features
        auto features = hal_interface_->getSupportedFeatures();
        for (const auto& feature : features) {
            switch (feature) {
                case hal::HALFeature::CANOPEN_SUPPORT:
                    status.add_supported_features("CANOPEN_SUPPORT");
                    break;
                case hal::HALFeature::SERIAL_SUPPORT:
                    status.add_supported_features("SERIAL_SUPPORT");
                    break;
                case hal::HALFeature::ETHERNET_SUPPORT:
                    status.add_supported_features("ETHERNET_SUPPORT");
                    break;
                case hal::HALFeature::PID_CONTROL:
                    status.add_supported_features("PID_CONTROL");
                    break;
                case hal::HALFeature::TRAJECTORY_CONTROL:
                    status.add_supported_features("TRAJECTORY_CONTROL");
                    break;
                case hal::HALFeature::ENCODER_FEEDBACK:
                    status.add_supported_features("ENCODER_FEEDBACK");
                    break;
                case hal::HALFeature::SAFETY_MONITORING:
                    status.add_supported_features("SAFETY_MONITORING");
                    break;
                case hal::HALFeature::SENSOR_MONITORING:
                    status.add_supported_features("SENSOR_MONITORING");
                    break;
                case hal::HALFeature::REAL_TIME_CONTROL:
                    status.add_supported_features("REAL_TIME_CONTROL");
                    break;
                case hal::HALFeature::DEROTATOR_SUPPORT:
                    status.add_supported_features("DEROTATOR_SUPPORT");
                    break;
            }
        }
        
        populateGamepad();
        
        // Set timestamp
        *status.mutable_timestamp() = google::protobuf::util::TimeUtil::GetCurrentTime();
        
        return true;
    }
    
    bool reinitializeHAL(const ::astro_mount::HALReinitRequest& request) {
        if (!hal_interface_) {
            return false;
        }
        
        // Shut down current HAL
        if (hal_axis1_motor_) hal_axis1_motor_->disable();
        if (hal_axis2_motor_) hal_axis2_motor_->disable();
        
        hal_sensor_interface_.reset();
        hal_safety_monitor_.reset();
        // DerotatorController manages its own HAL components; reset it so it gets
        // recreated with new HAL components after re-initialization.
        if (derotator_) {
            derotator_->shutdown();
        }
        derotator_.reset();
        hal_axis1_encoder_.reset();
        hal_axis2_encoder_.reset();
        hal_axis1_motor_.reset();
        hal_axis2_motor_.reset();
        
        hal_interface_->stop();
        hal_interface_->shutdown();
        
        // Re-initialize with stored config
        if (!hal_interface_->initialize(hal_config_)) {
            return false;
        }
        
        // Re-create component instances
        hal_axis1_motor_ = hal_interface_->createMotorControl(0);
        hal_axis2_motor_ = hal_interface_->createMotorControl(1);
        hal_axis1_encoder_ = hal_interface_->createEncoderReader(0);
        hal_axis2_encoder_ = hal_interface_->createEncoderReader(1);
        hal_safety_monitor_ = hal_interface_->createSafetyMonitor();
        hal_sensor_interface_ = hal_interface_->createSensorInterface();
        
        // Re-enable motors
        if (hal_axis1_motor_) hal_axis1_motor_->enable();
        if (hal_axis2_motor_) hal_axis2_motor_->enable();
        
        if (request.force_restart()) {
            if (!hal_interface_->start()) {
                return false;
            }
        } else if (!hal_interface_->isRunning()) {
            if (!hal_interface_->start()) {
                return false;
            }
        }
        
        // Recreate DerotatorController with new HAL components
        if (!recreateDerotator()) {
            MOUNT_LOG_WARN("reinitializeHAL: failed to recreate DerotatorController, continuing without derotator");
        }
        
        return true;
    }
    
    // Publicly accessible callback storage
    std::function<void(const MountStatus&)> status_callback_;
    std::function<void(const std::string&)> error_callback_;
    std::string error_message_;
    std::string config_file_path_;
    
    /**
     * @brief Build a MountStatus snapshot and invoke status_callback_ if set.
     *
     * Must be called WITHOUT holding state_mutex_ to prevent deadlock when the
     * user's callback re-enters the controller (e.g. calls getStatus()).
     * The status snapshot is built under the lock, then the callback is invoked
     * after releasing the lock.
     */
    void notifyStatusChanged() {
        // Re-entrancy guard: if the status_callback_ triggers another state
        // change that calls notifyStatusChanged() again, skip the recursive
        // call. The outer call's snapshot is already being delivered.
        if (notify_in_progress_.exchange(true)) {
            return;
        }
        
        MountStatus status;
        {
            std::shared_lock<std::shared_mutex> lock(*state_mutex_);
            status.state = state_;
            status.axis1_position = axis1_position_;
            status.axis2_position = axis2_position_;
            {
                std::shared_lock<std::shared_mutex> rate_lock(*rate_mutex_);
                status.axis1_rate = axis1_rate_;
                status.axis2_rate = axis2_rate_;
            }
            status.axis1_target = axis1_target_;
            status.axis2_target = axis2_target_;
            status.encoders_active = encoders_active_;
            status.guider_active = guider_active_;
            status.tpoint_calibrated = tpoint_calibrated_;
            status.tracking_error_ra = tracking_error_ra_;
            status.tracking_error_dec = tracking_error_dec_;
            status.timestamp = std::chrono::system_clock::now();
            status.error_message = error_message_;
            status.meridian_flip_pending = meridian_flip_pending_;
            status.meridian_flip_in_progress = meridian_flip_in_progress_;
            status.pier_side = pier_side_;
            status.time_to_meridian = time_to_meridian_;
            status.soft_limit_warning_active = soft_limit_warning_active_;
            status.soft_limit_deceleration_active = soft_limit_deceleration_active_;
            status.soft_limit_distance_axis1 = soft_limit_distance_axis1_;
            status.soft_limit_distance_axis2 = soft_limit_distance_axis2_;
            status.soft_limit_warning_message = soft_limit_warning_message_;
        }
        if (status_callback_) {
            status_callback_(status);
        }
        notify_in_progress_.store(false);
    }
    
    /**
     * @brief Invoke error_callback_ if set.
     *
     * @note Thread safety: This function performs a read + call on error_callback_
     * without holding a mutex. It relies on the callback being set once during
     * initialization (before any thread starts) and never modified afterward.
     * This is safe because:
     *   1. std::function assignment is NOT atomic (contrary to a common myth),
     *      but the callback is invariant after the first RPC arrives.
     *   2. All setErrorCallback() calls happen during Impl construction or
     *      before threads begin processing requests.
     *   3. If runtime callback changes are needed in the future, a std::mutex
     *      must be added to protect both the read and write of error_callback_.
     */
    void notifyError(const std::string& msg) {
        if (error_callback_) {
            error_callback_(msg);
        }
    }
    
    // ============================================
    // Meridian Flip API
    // ============================================
    
    /**
     * @brief Manually trigger an immediate meridian flip during tracking
     *
     * Forces the mount to flip to the other pier side by slewing
     * HA+180°, Dec→180°-Dec and resuming tracking.
     *
     * @return True if flip was initiated
     */
    bool executeMeridianFlip() {
        {
            std::lock_guard<std::shared_mutex> lock(*state_mutex_);
            
            if (state_ != MountStatus::State::TRACKING) {
                MOUNT_LOG_WARN("executeMeridianFlip rejected: state is not TRACKING");
                return false;
            }
            if (!config_.meridian_flip_enabled) {
                MOUNT_LOG_WARN("executeMeridianFlip rejected: meridian flip is disabled");
                return false;
            }
            if (config_.mount_type != MountType::EQUATORIAL) {
                MOUNT_LOG_WARN("executeMeridianFlip rejected: mount type is not EQUATORIAL"
                              " (CASUAL and ALT_AZ mounts do not support meridian flips)");
                return false;
            }
            if (meridian_flip_in_progress_) {
                MOUNT_LOG_WARN("executeMeridianFlip rejected: flip already in progress");
                return false;
            }
            
            // Initiate the flip immediately (no delay)
            state_ = MountStatus::State::MERIDIAN_FLIP;
            meridian_flip_in_progress_ = true;
            flip_start_time_ = std::chrono::steady_clock::now();
            
            // Compute flip targets
            double new_ha = axis1_target_ + 180.0;
            while (new_ha > 180.0) new_ha -= 360.0;
            while (new_ha < -180.0) new_ha += 360.0;
            
            flip_ha_target_ = new_ha;
            flip_dec_target_ = 180.0 - axis2_target_;
            
            double jd_flip = core::AstronomicalCalculations::getCurrentJulianDate();
            double lst_flip = core::AstronomicalCalculations::calculateLST(jd_flip, config_.longitude);
            double ha_hours_flip = axis1_position_ / 15.0;
            flip_original_ra_ = lst_flip - ha_hours_flip;
            while (flip_original_ra_ < 0.0) flip_original_ra_ += 24.0;
            while (flip_original_ra_ >= 24.0) flip_original_ra_ -= 24.0;
            flip_original_dec_ = axis2_position_;
            
            meridian_flip_pending_ = false;
            
            MOUNT_LOG_INFO("Manual meridian flip triggered: HA target={:.2f}°, Dec target={:.2f}°",
                     flip_ha_target_, flip_dec_target_);
        }  // state_mutex_ released
        
        // Notify status callback outside state_mutex_ lock
        notifyStatusChanged();  // TRACKING → MERIDIAN_FLIP
        
        return true;
    }
    
    /**
     * @brief Check if a meridian flip is pending (waiting for delay to expire)
     * @return True if a flip is pending
     */
    bool isMeridianFlipPending() const {
        std::shared_lock<std::shared_mutex> lock(*state_mutex_);
        return meridian_flip_pending_;
    }
    
    /**
     * @brief Get time to meridian crossing
     * @return Time until meridian crossing in hours (negative if past meridian)
     */
    double getTimeToMeridian() const {
        std::shared_lock<std::shared_mutex> lock(*state_mutex_);
        return time_to_meridian_;
    }
    
    /**
     * @brief Get current pier side
     * @return 1 = East pier (normal), -1 = West pier (flipped)
     */
    int getPierSide() const {
        std::shared_lock<std::shared_mutex> lock(*state_mutex_);
        return pier_side_;
    }
    
    /// Evaluate soft limit zones and return a rate scaling factor.
    /// Computes distance to nearest limit for each axis, sets warning/deceleration flags,
    /// and returns a rate multiplier (1.0 = normal, down to soft_limit_tracking_rate_factor at hard limit).
    /// Must be called with state_mutex_ held.
    double evaluateSoftLimits(double axis1_pos, double axis2_pos) {
        // Guard against NaN/Inf inputs. Non-finite positions would produce NaN distances
        // (NaN - limit = NaN), NaN rate_factor (std::min(NaN, ...) = NaN), and silent
        // propagation through the tracking loop. Return 1.0 (no deceleration) and let
        // the caller's rate_factor NaN guard catch it with a more informative error message.
        if (!std::isfinite(axis1_pos) || !std::isfinite(axis2_pos)) {
            soft_limit_warning_active_ = false;
            soft_limit_deceleration_active_ = false;
            soft_limit_distance_axis1_ = 0.0;
            soft_limit_distance_axis2_ = 0.0;
            soft_limit_warning_message_.clear();
            return 1.0;
        }
        
        if (!config_.soft_limits_enabled) {
            soft_limit_warning_active_ = false;
            soft_limit_deceleration_active_ = false;
            soft_limit_distance_axis1_ = 0.0;
            soft_limit_distance_axis2_ = 0.0;
            soft_limit_warning_message_.clear();
            return 1.0;
        }
        
        const double min1 = config_.soft_limit_axis1_min;
        const double max1 = config_.soft_limit_axis1_max;
        const double min2 = config_.soft_limit_axis2_min;
        const double max2 = config_.soft_limit_axis2_max;
        const double warning = config_.soft_limit_warning_degrees;
        const double decel = config_.soft_limit_deceleration_degrees;
        const double min_rate = config_.soft_limit_tracking_rate_factor;
        
        // Distance to nearest limit on each axis (positive = inside range)
        double d1_min = axis1_pos - min1;
        double d1_max = max1 - axis1_pos;
        double d2_min = axis2_pos - min2;
        double d2_max = max2 - axis2_pos;
        
        double dist1 = std::min(d1_min, d1_max);
        double dist2 = std::min(d2_min, d2_max);
        
        soft_limit_distance_axis1_ = dist1;
        soft_limit_distance_axis2_ = dist2;
        
        // Check for hard limit violation
        if (dist1 < 0.0 || dist2 < 0.0) {
            soft_limit_warning_active_ = true;
            soft_limit_deceleration_active_ = true;
            soft_limit_warning_message_ = "Hard limit exceeded";
            return min_rate; // Minimum rate - effectively stopped
        }
        
        // Determine if in deceleration zone (whichever axis is closer)
        bool in_decel = (dist1 < decel) || (dist2 < decel);
        bool in_warning = (dist1 < warning) || (dist2 < warning);
        
        soft_limit_warning_active_ = in_warning;
        soft_limit_deceleration_active_ = in_decel;
        
        // Build warning message
        soft_limit_warning_message_.clear();
        if (in_warning || in_decel) {
            std::string msg;
            if (dist1 < warning) {
                msg += "Axis1: " + std::to_string(dist1) + "° to limit; ";
            }
            if (dist2 < warning) {
                msg += "Axis2: " + std::to_string(dist2) + "° to limit; ";
            }
            if (in_decel) msg += "DECELERATING";
            else msg += "WARNING";
            soft_limit_warning_message_ = msg;
        }
        
        // Compute rate scaling factor based on closest axis to limit
        double min_dist = std::min(dist1, dist2);
        
        if (min_dist <= 0.0) {
            return min_rate;
        } else if (min_dist < decel) {
            // Linear interpolation: min_rate at hard limit, 1.0 at decel boundary
            return min_rate + (1.0 - min_rate) * (min_dist / decel);
        } else {
            return 1.0; // Full rate outside deceleration zone
        }
    }
    
private:
    struct Measurement {
        double observed_ra;
        double observed_dec;
        double expected_ra;
        double expected_dec;
        double mount_ha;
        double mount_dec;
        double temperature;
        double pressure;
        double humidity;
        double proper_motion_ra;
        double proper_motion_dec;
        double parallax;
        double epoch;
        std::chrono::system_clock::time_point timestamp;
    };
    
    MountStatus::State state_;
    double axis1_position_;             // Normalized servo position [0°, 360°)
    double axis2_position_;             // Normalized servo position [0°, 360°)
    double raw_servo_axis1_position_{0.0}; // Non-normalized (absolute) servo motor degrees
    double raw_servo_axis2_position_{0.0}; // Non-normalized (absolute) servo motor degrees
    double axis1_target_;
    double axis2_target_;
    double axis1_rate_;
    double axis2_rate_;
    bool encoders_active_;
    bool guider_active_;
    bool tpoint_calibrated_;
    bool bootstrap_calibrated_;
    BootstrapMode bootstrap_mode_{BootstrapMode::BOOTSTRAP_MANUAL};

    // Guider delta corrections — stored as POSITION OFFSETS in degrees.
    // Written by applyGuiderCorrection() under rate_mutex_ as arcsec→deg
    // conversions (RA: *15/3600, Dec: /3600), read and reset each tracking
    // loop iteration as direct position additions (not rate modulations).
    // This position-offset approach ensures correction magnitude is
    // independent of the loop iteration interval (dt).
    double guider_delta_axis1_{0.0};
    double guider_delta_axis2_{0.0};
    
    // Cached last-sent CANopen/HAL motor rates for rate-change detection.
    // The tracking loop sends setVelocity / setVelocityTarget every 100ms
    // iteration; if the rate hasn't changed significantly (>1e-12 deg/s),
    // we skip the CANopen bus write to reduce bus traffic and CPU overhead.
    // Initialized to NaN so the first iteration always triggers a write.
    double last_sent_rate_1_{std::numeric_limits<double>::quiet_NaN()};
    double last_sent_rate_2_{std::numeric_limits<double>::quiet_NaN()};
    
    // Bootstrap calibration computed results
    double bootstrap_rms_ra_arcsec_{0.0};
    double bootstrap_rms_dec_arcsec_{0.0};
    double bootstrap_ra_correction_arcsec_{0.0};
    double bootstrap_dec_correction_arcsec_{0.0};
    
    // CASUAL bootstrap: estimated orientation quaternion
    MountOrientation bootstrap_estimated_orientation_;
    double bootstrap_estimated_quaternion_error_arcsec_{0.0};
    
    // TPOINT calibration computed results
    double tpoint_residual_rms_arcsec_{0.0};
    double tpoint_residual_max_arcsec_{0.0};
    double tpoint_chi_squared_{0.0};
    double tpoint_mean_error_arcsec_{0.0};
    
    // Effective TPoint enabled terms bitmask (from config or DEFAULT_TERMS)
    uint32_t tpoint_enabled_terms_{models::TPointTerms::DEFAULT_TERMS};
    
    // Cached TPoint quality metrics and uncertainties
    std::map<std::string, double> tpoint_quality_metrics_;
    std::vector<double> tpoint_param_uncertainties_;
    Eigen::MatrixXd tpoint_covariance_matrix_;
    
    // Operation counters for metrics
    size_t slew_count_{0};
    size_t track_count_{0};
    size_t calibration_count_{0};
    
    // Tracking loop metrics
    size_t tracking_iteration_count_{0};
    double total_update_time_ms_{0.0};
    
    double tracking_error_ra_;
    double tracking_error_dec_;
    
    std::atomic<bool> tracking_active_{false};
    
    // Re-entrancy guard for notifyStatusChanged(). Prevents deadlock when the
    // status_callback_ itself triggers a state change that calls back into
    // notifyStatusChanged() — the recursive call is safely skipped since the
    // outer call will already have constructed the MountStatus snapshot.
    // Without this guard, the status callback's re-entrant notifyStatusChanged
    // call would block trying to re-acquire state_mutex_ (shared_lock on a
    // shared_mutex already held by the current thread is undefined behavior).
    std::atomic<bool> notify_in_progress_{false};
    
    std::unique_ptr<std::shared_mutex> state_mutex_;
    std::unique_ptr<std::shared_mutex> rate_mutex_;
    std::unique_ptr<std::mutex> env_mutex_;
    
    // Background thread handle for async operations (slew, track, park)
    // Joined on shutdown/destroy to prevent use-after-free
    std::thread work_thread_;
    
    // Protects work_thread_ from concurrent join + assign (data race fix)
    std::unique_ptr<std::mutex> thread_mutex_;
    
    // ── Gamepad integration ─────────────────────────────────────────
    std::unique_ptr<hal::EvdevGamepadInput> gamepad_input_;
    std::thread gamepad_thread_;
    std::atomic<bool> gamepad_running_{false};
    std::atomic<bool> gamepad_control_enabled_{false};  ///< explicit user consent
    double gamepad_deadzone_{0.15};
    double gamepad_sensitivity_{1.0};
    double gamepad_max_velocity_{5.0};
    bool gamepad_axis0_active_{false};  ///< true when stick was deflected last cycle
    bool gamepad_axis1_active_{false};
    int gamepad_mode_{0};  ///< 0=RAW, 1=CELESTIAL, 2=ALT_AZ, 3=PRECISION
    
    // Speed adjustment via gamepad buttons
    double gamepad_speed_step_{0.5};       ///< Velocity step per speed-up/down button press
    double gamepad_min_velocity_{0.1};     ///< Minimum allowed max velocity
    double gamepad_max_velocity_limit_{20.0}; ///< Maximum allowed max velocity
    
  public:
    void initGamepadInput() {
        if (gamepad_input_) return;
        const auto& gp_cfg = config_.hal_config.gamepad;
        gamepad_deadzone_ = gp_cfg.deadzone > 0.0 ? gp_cfg.deadzone : 0.15;
        gamepad_sensitivity_ = gp_cfg.sensitivity > 0.0 ? gp_cfg.sensitivity : 1.0;
        gamepad_max_velocity_ = gp_cfg.max_velocity_deg_s > 0.0 ? gp_cfg.max_velocity_deg_s : 5.0;
        
        gamepad_mode_ = gp_cfg.gamepad_mode;
        
        gamepad_input_ = std::make_unique<hal::EvdevGamepadInput>();
        std::string dev = gp_cfg.device_path.empty() ? "" : gp_cfg.device_path;
        if (!gamepad_input_->initialize(dev)) {
            MOUNT_LOG_WARN("Gamepad: failed to open device '{}'", dev.empty() ? "(auto)" : dev);
            gamepad_input_.reset();
            return;
        }
        MOUNT_LOG_INFO("Gamepad: connected '{}'", gamepad_input_->getDeviceName());
    }
    
    void startGamepadLoop() {
        if (gamepad_running_) return;
        initGamepadInput();  // ensure device is open (no-op if already)
        if (!gamepad_input_) {
            MOUNT_LOG_WARN("Gamepad: cannot start loop, input not initialised");
            return;
        }
        gamepad_control_enabled_ = true;
        gamepad_running_ = true;
        gamepad_thread_ = std::thread(&Impl::gamepadLoop, this);
    }
    
    void stopGamepad() {
        gamepad_control_enabled_ = false;
        gamepad_running_ = false;
        if (gamepad_thread_.joinable()) {
            gamepad_thread_.join();
        }
        if (gamepad_input_) {
            gamepad_input_->shutdown();
            gamepad_input_.reset();
        }
        MOUNT_LOG_INFO("Gamepad: stopped");
    }
    
    void setGamepadMode(int mode) {
        if (mode < 0 || mode > 3) {
            MOUNT_LOG_WARN("Gamepad: invalid mode {}, ignoring", mode);
            return;
        }
        if (mode == gamepad_mode_) return;
        
        gamepad_mode_ = mode;
        hal_config_.gamepad.gamepad_mode = mode;
        
        const char* mode_names[] = {"RAW", "CELESTIAL", "ALT_AZ", "PRECISION"};
        MOUNT_LOG_INFO("Gamepad: mode set to {}", mode_names[mode]);
    }
    
    void gamepadLoop() {
        MOUNT_LOG_INFO("Gamepad: polling started");
        while (gamepad_running_) {
            if (!gamepad_input_ || !gamepad_input_->isConnected()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
            auto state = gamepad_input_->readState();
            if (!state.connected) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }
            
            // If the user hasn't explicitly enabled manual control
            // via the UI button, do NOT send any CANopen commands.
            // This prevents race conditions with node state transitions
            // during startup.
            if (!gamepad_control_enabled_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }
            
            // ── Button actions ──────────────────────────────────
            if (state.button_emergency_stop) {
                static auto last_emergency_stop = std::chrono::steady_clock::now();
                auto now_es = std::chrono::steady_clock::now();
                // Debounce: only trigger once per 3 seconds to avoid
                // flooding the log and bouncing on noisy buttons.
                if (std::chrono::duration_cast<std::chrono::seconds>(now_es - last_emergency_stop).count() >= 3) {
                    MOUNT_LOG_WARN("Gamepad: EMERGENCY STOP");
                    last_emergency_stop = now_es;
                }
                if (canopen_interface_) {
                    canopen_interface_->emergencyStop(0);
                    canopen_interface_->emergencyStop(1);
                }
                state_ = MountStatus::State::ERROR;
                error_message_ = "Gamepad emergency stop";
                // Do NOT break — the loop continues running so that
                // after clearErrors() returns to IDLE, gamepad control
                // resumes without needing to call StartGamepad again.
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }
            if (state.button_stop) {
                if (canopen_interface_) {
                    canopen_interface_->stopAxis(0);
                    canopen_interface_->stopAxis(1);
                }
                MOUNT_LOG_INFO("Gamepad: STOP");
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }
            if (state.button_park) {
                MOUNT_LOG_INFO("Gamepad: PARK");
                // Trigger park asynchronously
                if (state_ == MountStatus::State::IDLE) {
                    park();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
            if (state.button_home) {
                MOUNT_LOG_INFO("Gamepad: HOME");
                // Slew to park position as "home"
                if (state_ == MountStatus::State::IDLE && canopen_interface_) {
                    canopen_interface_->setPositionTarget(0, config_.park_position_axis1,
                        gamepad_max_velocity_, config_.slew_acceleration);
                    canopen_interface_->setPositionTarget(1, config_.park_position_axis2,
                        gamepad_max_velocity_, config_.slew_acceleration);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
            if (state.button_speed_up || state.button_speed_down) {
                static auto last_speed_change = std::chrono::steady_clock::now();
                auto now_sc = std::chrono::steady_clock::now();
                // Debounce: 300ms between speed changes
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now_sc - last_speed_change).count() >= 300) {
                    double step = gamepad_speed_step_;
                    if (state.button_speed_down) step = -step;
                    double new_vel = gamepad_max_velocity_ + step;
                    new_vel = std::clamp(new_vel, gamepad_min_velocity_, gamepad_max_velocity_limit_);
                    if (std::abs(new_vel - gamepad_max_velocity_) > 0.001) {
                        gamepad_max_velocity_ = new_vel;
                        MOUNT_LOG_INFO("Gamepad: max velocity adjusted to {:.1f} deg/s", gamepad_max_velocity_);
                        last_speed_change = now_sc;
                    }
                }
                // Don't continue – allow simultaneous axis control
            }
            
            // Skip axis control when in ERROR state (emergency stop active).
            // The loop continues running so that after clearErrors() transitions
            // back to IDLE, gamepad control resumes automatically.
            if (state_ == MountStatus::State::ERROR) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            // ── Axis control (left stick → axis 0=HA/RA, axis 1=Dec) ──
            // Only send velocity commands when the stick is actively
            // deflected. When the stick returns to centre we issue a
            // single zero-velocity stop and then go silent so that
            // position-mode slewing / tracking is not disrupted.
            if (canopen_interface_ && canopen_interface_->isConnected()) {
                double dead = gamepad_deadzone_;
                
                auto applyAxis = [&](double raw, double& out) {
                    if (std::abs(raw) < dead) { out = 0.0; return; }
                    // Rescale: (raw - dead*sign) / (1 - dead)
                    double sign = (raw > 0) ? 1.0 : -1.0;
                    out = sign * (std::abs(raw) - dead) / (1.0 - dead);
                    out *= gamepad_sensitivity_;
                };
                
                double vel0 = 0.0, vel1 = 0.0;
                applyAxis(state.axis_lx, vel0);  // Left stick X → axis 0
                applyAxis(state.axis_ly, vel1);  // Left stick Y → axis 1
                
                bool axis0_deflected = (std::abs(vel0) > 0.001);
                bool axis1_deflected = (std::abs(vel1) > 0.001);
                
                if (gamepad_mode_ == 0 || !bootstrap_calibrated_ || !astro_calc_ || (gamepad_mode_ == 3 && !tpoint_calibrated_)) {
                    // ── RAW mode (or fallback when uncalibrated) ──
                    vel0 *= gamepad_max_velocity_;
                    vel1 *= gamepad_max_velocity_;
                    
                    if (canopen_interface_->isDriveEnabled(0)) {
                        if (axis0_deflected || gamepad_axis0_active_) {
                            canopen_interface_->setVelocityTarget(0, vel0, config_.slew_acceleration);
                        }
                    }
                    gamepad_axis0_active_ = axis0_deflected;
                    
                    if (canopen_interface_->isDriveEnabled(1)) {
                        if (axis1_deflected || gamepad_axis1_active_) {
                            canopen_interface_->setVelocityTarget(1, vel1, config_.slew_acceleration);
                        }
                    }
                    gamepad_axis1_active_ = axis1_deflected;
                } else if (axis0_deflected || axis1_deflected) {
                    // ── Coordinate-based navigation (CELESTIAL or ALT_AZ) ──
                    const double dt = 0.02;  // ~50 Hz poll rate
                    double jd = core::AstronomicalCalculations::getCurrentJulianDate();
                    double axis1_target = axis1_position_;
                    double axis2_target = axis2_position_;
                    
                    if (gamepad_mode_ == 1 || gamepad_mode_ == 3) {
                        // CELESTIAL / PRECISION: LX → dRA/dt, LY → dDec/dt
                        double speed_factor = (gamepad_mode_ == 3) ? 0.1 : 1.0;  // PRECISION = 1/10th speed
                        auto [current_ra, current_dec] = astro_calc_->mountOrientationToEquatorial(
                            axis1_position_, axis2_position_, jd, mount_orientation_.quaternion);
                        
                        double dRA = vel0 * gamepad_max_velocity_ * speed_factor * dt / 15.0;
                        double dDec = vel1 * gamepad_max_velocity_ * speed_factor * dt;
                        double new_ra = current_ra + dRA;
                        double new_dec = std::clamp(current_dec + dDec, -90.0, 90.0);
                        
                        auto [new_axis1, new_axis2] = astro_calc_->equatorialToMountOrientation(
                            new_ra, new_dec, jd, mount_orientation_.quaternion);
                        axis1_target = new_axis1;
                        axis2_target = new_axis2;
                    } else if (gamepad_mode_ == 2) {
                        // ALT_AZ: LY → dAlt/dt (deg/s), LX → dAz/dt (deg/s)
                        auto [current_ra, current_dec] = astro_calc_->mountOrientationToEquatorial(
                            axis1_position_, axis2_position_, jd, mount_orientation_.quaternion);
                        auto [current_alt, current_az] = astro_calc_->equatorialToHorizontal(
                            current_ra, current_dec, jd, false);
                        
                        double dAlt = vel1 * gamepad_max_velocity_ * dt;
                        double dAz = vel0 * gamepad_max_velocity_ * dt;
                        double new_alt = std::clamp(current_alt + dAlt, 0.0, 90.0);
                        double new_az = std::fmod(current_az + dAz + 360.0, 360.0);
                        
                        auto [new_ra, new_dec] = astro_calc_->horizontalToEquatorial(
                            new_alt, new_az, jd, false);
                        auto [new_axis1, new_axis2] = astro_calc_->equatorialToMountOrientation(
                            new_ra, new_dec, jd, mount_orientation_.quaternion);
                        axis1_target = new_axis1;
                        axis2_target = new_axis2;
                    }
                    
                    // Convert position delta to velocity and send
                    double mount_vel0 = (axis1_target - axis1_position_) / dt;
                    double mount_vel1 = (axis2_target - axis2_position_) / dt;
                    
                    // Clamp to max velocity (reduced for PRECISION mode)
                    double max_vel = (gamepad_mode_ == 3) ? gamepad_max_velocity_ * 0.1 : gamepad_max_velocity_;
                    mount_vel0 = std::clamp(mount_vel0, -max_vel, max_vel);
                    mount_vel1 = std::clamp(mount_vel1, -max_vel, max_vel);
                    
                    if (canopen_interface_->isDriveEnabled(0)) {
                        canopen_interface_->setVelocityTarget(0, mount_vel0, config_.slew_acceleration);
                    }
                    if (canopen_interface_->isDriveEnabled(1)) {
                        canopen_interface_->setVelocityTarget(1, mount_vel1, config_.slew_acceleration);
                    }
                    gamepad_axis0_active_ = true;
                    gamepad_axis1_active_ = true;
                } else {
                    // Stick released – stop coordinate-based motion
                    if (gamepad_axis0_active_) {
                        if (canopen_interface_->isDriveEnabled(0)) {
                            canopen_interface_->setVelocityTarget(0, 0.0, config_.slew_acceleration);
                        }
                        gamepad_axis0_active_ = false;
                    }
                    if (gamepad_axis1_active_) {
                        if (canopen_interface_->isDriveEnabled(1)) {
                            canopen_interface_->setVelocityTarget(1, 0.0, config_.slew_acceleration);
                        }
                        gamepad_axis1_active_ = false;
                    }
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(20)); // ~50 Hz
        }
        // Stop all motion on exit
        if (canopen_interface_) {
            canopen_interface_->stopAxis(0);
            canopen_interface_->stopAxis(1);
        }
        MOUNT_LOG_INFO("Gamepad: polling stopped");
    }
    
    // Internal version: caller must already hold thread_mutex_ lock
    void joinWorkThreadLocked() {
        if (work_thread_.joinable()) {
            work_thread_.join();
        }
    }
    
    // Public version: acquires thread_mutex_ internally
    void joinWorkThread() {
        std::lock_guard<std::mutex> lock(*thread_mutex_);
        joinWorkThreadLocked();
    }
    
    ControllerConfig config_;
    MountOrientation mount_orientation_;
    std::vector<Measurement> bootstrap_measurements_;
    std::vector<Measurement> tpoint_measurements_;
    std::unique_ptr<models::TPointModel> tpoint_model_;
    std::unique_ptr<core::AstronomicalCalculations> astro_calc_;
    std::unique_ptr<models::EphemerisTrackerManager> ephemeris_manager_;
    std::shared_ptr<ICanOpenInterface> canopen_interface_;
    std::unique_ptr<hal::HALInterface> hal_interface_;
    hal::HALConfig hal_config_;
    std::unique_ptr<PositionKalmanFilter> position_kf_;
    
    // HAL component instances (created from hal_interface_ during initialize())
    std::unique_ptr<hal::MotorControl> hal_axis1_motor_;
    std::unique_ptr<hal::MotorControl> hal_axis2_motor_;
    std::unique_ptr<hal::EncoderReader> hal_axis1_encoder_;
    std::unique_ptr<hal::EncoderReader> hal_axis2_encoder_;
    std::unique_ptr<hal::SafetyMonitor> hal_safety_monitor_;
    std::unique_ptr<hal::SensorInterface> hal_sensor_interface_;
    
    // DerotatorController instance (internal module with own mutex and thread)
    std::unique_ptr<DerotatorController> derotator_;
    
    // Encoder type storage
    bool encoder_absolute_;
    
    // Environmental parameters
    double env_temperature_;
    double env_pressure_;
    double env_humidity_;
    
    // Guider connection string
    std::string guider_connection_;
    
    // Field rotation time tracking (non-static, per-instance)
    mutable double last_field_rotation_time_;
    
    // Meridian flip tracking
    bool meridian_flip_pending_{false};
    std::chrono::steady_clock::time_point meridian_flip_pending_time_;
    bool meridian_flip_in_progress_{false};
    bool meridian_flipped_{false};
    int pier_side_{1};
    double time_to_meridian_{0.0};
    std::chrono::steady_clock::time_point flip_start_time_;
    double flip_ha_target_{0.0};
    double flip_dec_target_{0.0};
    double flip_original_ra_{0.0};
    double flip_original_dec_{0.0};
    
    // Soft limit tracking state
    double soft_limit_distance_axis1_{0.0};
    double soft_limit_distance_axis2_{0.0};
    bool soft_limit_warning_active_{false};
    bool soft_limit_deceleration_active_{false};
    std::string soft_limit_warning_message_;
    
};

MountController::MountController()
    : pimpl(std::make_unique<Impl>()) {}

MountController::MountController(std::unique_ptr<hal::HALInterface> hal_interface)
    : pimpl(std::make_unique<Impl>(std::move(hal_interface))) {}

MountController::~MountController() {
    // Ensure the background work thread is joined before destroying Impl members.
    // Without this, the compiler-generated ~Impl() would destroy canopen_interface_
    // and hal_interface before work_thread_, potentially leaving the work thread
    // accessing already-freed memory (segfault) or calling std::terminate() on a
    // joinable std::thread.
    if (pimpl) {
        pimpl->shutdown();
    }
}

bool MountController::initialize(const ControllerConfig& config) {
    return pimpl->initialize(config);
}

void MountController::shutdown() {
    pimpl->shutdown();
}

bool MountController::slewToEquatorial(double ra, double dec) {
    return pimpl->slewToEquatorial(ra, dec);
}

bool MountController::slewToHorizontal(double altitude, double azimuth) {
    return pimpl->slewToHorizontal(altitude, azimuth);
}

bool MountController::startTracking(double ra, double dec, TrackingMode mode) {
    return pimpl->startTracking(ra, dec, mode);
}

void MountController::stop() {
    pimpl->stop();
}

void MountController::park() {
    pimpl->park();
}

void MountController::unpark() {
    pimpl->unpark();
}

void MountController::clearErrors() {
    pimpl->clearErrors();
}

MountController::MountStatus MountController::getStatus() const {
    return pimpl->getStatus();
}

void MountController::refreshPositions() {
    pimpl->refreshPositionsFromCANopen();
}

bool MountController::addCalibrationMeasurement(double observed_ra, double observed_dec,
                                                double expected_ra, double expected_dec,
                                                double mount_ha, double mount_dec,
                                                double temperature, double pressure,
                                                double humidity,
                                                double proper_motion_ra, double proper_motion_dec,
                                                double parallax, double epoch) {
    return pimpl->addCalibrationMeasurement(observed_ra, observed_dec,
                                           expected_ra, expected_dec,
                                           mount_ha, mount_dec,
                                           temperature, pressure, humidity,
                                           proper_motion_ra, proper_motion_dec,
                                           parallax, epoch);
}

// Bootstrap calibration methods
bool MountController::addBootstrapMeasurement(double observed_ra, double observed_dec,
                                              double expected_ra, double expected_dec,
                                              double mount_ha, double mount_dec) {
    return pimpl->addBootstrapMeasurement(observed_ra, observed_dec,
                                         expected_ra, expected_dec,
                                         mount_ha, mount_dec);
}

bool MountController::runBootstrapCalibration() {
    return pimpl->runBootstrapCalibration();
}

bool MountController::isBootstrapCalibrated() const {
    return pimpl->isBootstrapCalibrated();
}

void MountController::clearBootstrapMeasurements() {
    pimpl->clearBootstrapMeasurements();
}

size_t MountController::getBootstrapMeasurementCount() const {
    return pimpl->getBootstrapMeasurementCount();
}

double MountController::getBootstrapRmsRaArcsec() const {
    return pimpl->getBootstrapRmsRaArcsec();
}

double MountController::getBootstrapRmsDecArcsec() const {
    return pimpl->getBootstrapRmsDecArcsec();
}

double MountController::getBootstrapRaCorrectionArcsec() const {
    return pimpl->getBootstrapRaCorrectionArcsec();
}

double MountController::getBootstrapDecCorrectionArcsec() const {
    return pimpl->getBootstrapDecCorrectionArcsec();
}

bool MountController::setBootstrapMode(BootstrapMode mode) {
    return pimpl->setBootstrapMode(mode);
}

MountController::BootstrapMode MountController::getBootstrapMode() const {
    return pimpl->getBootstrapMode();
}

// Metrics and counters accessors
size_t MountController::getSlewCount() const {
    return pimpl->getSlewCount();
}

size_t MountController::getTrackCount() const {
    return pimpl->getTrackCount();
}

size_t MountController::getCalibrationCount() const {
    return pimpl->getCalibrationCount();
}

size_t MountController::getTrackingIterationCount() const {
    return pimpl->getTrackingIterationCount();
}

double MountController::getTotalUpdateTimeMs() const {
    return pimpl->getTotalUpdateTimeMs();
}

// TPOINT calibration metrics accessors
size_t MountController::getTPointMeasurementCount() const {
    return pimpl->getTPointMeasurementCount();
}

double MountController::getTPointResidualRmsArcsec() const {
    return pimpl->getTPointResidualRmsArcsec();
}

double MountController::getTPointResidualMaxArcsec() const {
    return pimpl->getTPointResidualMaxArcsec();
}

double MountController::getTPointChiSquared() const {
    return pimpl->getTPointChiSquared();
}

// TPOINT calibration methods
bool MountController::addTPointMeasurement(double observed_ra, double observed_dec,
                                           double expected_ra, double expected_dec,
                                           double mount_ha, double mount_dec,
                                           double temperature, double pressure,
                                           double humidity,
                                           double proper_motion_ra, double proper_motion_dec,
                                           double parallax, double epoch) {
    return pimpl->addTPointMeasurement(observed_ra, observed_dec,
                                      expected_ra, expected_dec,
                                      mount_ha, mount_dec,
                                      temperature, pressure, humidity,
                                      proper_motion_ra, proper_motion_dec,
                                      parallax, epoch);
}

bool MountController::addTPointMeasurement(double observed_ra, double observed_dec,
                                           double expected_ra, double expected_dec,
                                           double temperature, double pressure,
                                           double humidity) {
    return pimpl->addTPointMeasurement(observed_ra, observed_dec,
                                      expected_ra, expected_dec,
                                      temperature, pressure, humidity);
}

void MountController::clearTPointMeasurements() {
    pimpl->clearTPointMeasurements();
}

bool MountController::runTPointCalibration() {
    return pimpl->runTPointCalibration();
}

std::string MountController::getTPointParameters() const {
    return pimpl->getTPointParameters();
}

std::vector<double> MountController::getRotationMatrix() const {
    return pimpl->getRotationMatrix();
}

void MountController::setEncodersEnabled(bool enable) {
    pimpl->setEncodersEnabled(enable);
}

void MountController::setEncoderType(bool absolute) {
    pimpl->setEncoderType(absolute);
}

bool MountController::connectGuider(const std::string& connection_string) {
    return pimpl->connectGuider(connection_string);
}

void MountController::disconnectGuider() {
    pimpl->disconnectGuider();
}

void MountController::applyGuiderCorrection(double ra_correction, double dec_correction) {
    pimpl->applyGuiderCorrection(ra_correction, dec_correction);
}

std::tuple<double, double, double> MountController::determinePolePosition(double duration_hours) {
    return pimpl->determinePolePosition(duration_hours);
}

bool MountController::saveState(const std::string& filename) const {
    return pimpl->saveState(filename);
}

bool MountController::loadState(const std::string& filename) {
    return pimpl->loadState(filename);
}

void MountController::setEnvironmentalParams(double temperature, double pressure, double humidity) {
    pimpl->setEnvironmentalParams(temperature, pressure, humidity);
}

void MountController::setStatusCallback(std::function<void(const MountStatus&)> callback) {
    // Store in a member variable (we defined it in Impl)
    // This callback would be invoked from status monitoring thread
    pimpl->status_callback_ = callback;
}

void MountController::setErrorCallback(std::function<void(const std::string&)> callback) {
    pimpl->error_callback_ = callback;
}

MountController::ControllerConfig MountController::getConfiguration() const {
    return pimpl->getConfiguration();
}

bool MountController::updateConfiguration(const ControllerConfig& config) {
    return pimpl->updateConfiguration(config);
}

void MountController::setConfigFilePath(const std::string& path) {
    pimpl->config_file_path_ = path;
}

bool MountController::setMountOrientation(const MountOrientation& orientation) {
    return pimpl->setMountOrientation(orientation);
}

MountController::MountOrientation MountController::getMountOrientation() const {
    return pimpl->getMountOrientation();
}

bool MountController::uploadEphemeris(const std::string& object_id,
                                     const std::string& object_name,
                                     const std::string& object_type,
                                     const std::vector<std::tuple<std::chrono::system_clock::time_point,
                                                                   double, double, double, double>>& points,
                                     int interpolation_order) {
    return pimpl->uploadEphemeris(object_id, object_name, object_type, points, interpolation_order);
}

std::string MountController::startEphemerisTracking(
    const std::string& object_id,
    const std::chrono::system_clock::time_point& start_time,
    double lead_time_seconds,
    bool wait_at_start,
    bool enable_prediction,
    double prediction_interval_hours,
    const std::string& tracking_mode,
    double custom_rate_ra,
    double custom_rate_dec) {
    return pimpl->startEphemerisTracking(object_id, start_time, lead_time_seconds,
                                        wait_at_start, enable_prediction,
                                        prediction_interval_hours, tracking_mode,
                                        custom_rate_ra, custom_rate_dec);
}

std::string MountController::startEphemerisTrackingWithData(
    const std::string& object_id,
    const std::string& object_name,
    const std::string& object_type,
    const std::vector<std::tuple<std::chrono::system_clock::time_point,
                                  double, double, double, double>>& points,
    const std::chrono::system_clock::time_point& start_time,
    double lead_time_seconds,
    int interpolation_order,
    const std::string& tracking_mode) {
    return pimpl->startEphemerisTrackingWithData(object_id, object_name, object_type,
                                                points, start_time, lead_time_seconds,
                                                interpolation_order, tracking_mode);
}

bool MountController::stopEphemerisTracking(const std::string& tracker_id) {
    return pimpl->stopEphemerisTracking(tracker_id);
}

::astro_mount::EphemerisTrackStatus MountController::getEphemerisTrackStatus(
    const std::string& tracker_id) const {
    return pimpl->getEphemerisTrackStatus(tracker_id);
}

std::vector<std::string> MountController::getActiveEphemerisTrackers() const {
    return pimpl->getActiveEphemerisTrackers();
}

void MountController::clearEphemerisCache() {
    pimpl->clearEphemerisCache();
}

::astro_mount::EphemerisMetrics MountController::getEphemerisMetrics() const {
    return pimpl->getEphemerisMetrics();
}

// ============================================
// FIELD ROTATION / DEROTATOR CONTROL
// ============================================

bool MountController::configureDerotator(const ::astro_mount::DerotatorConfig& config) {
    return pimpl->configureDerotator(config);
}

bool MountController::enableFieldRotation(const ::astro_mount::FieldRotationParams& params) {
    return pimpl->enableFieldRotation(params);
}

bool MountController::controlFieldRotation(const ::astro_mount::FieldRotationControlRequest& request) {
    return pimpl->controlFieldRotation(request);
}

::astro_mount::DerotatorStatus MountController::getDerotatorStatus() const {
    return pimpl->getDerotatorStatus();
}

bool MountController::homeDerotator(const ::astro_mount::DerotatorHomingRequest& request) {
    return pimpl->homeDerotator(request);
}

::astro_mount::FieldRotationParams MountController::getFieldRotationParams() const {
    return pimpl->getFieldRotationParams();
}

std::shared_ptr<ICanOpenInterface> MountController::getCanOpenInterface() {
    // Use helper method from Impl
    return pimpl->getCanOpenInterfaceShared();
}

// ============================================
// MERIDIAN FLIP API
// ============================================

bool MountController::executeMeridianFlip() {
    return pimpl->executeMeridianFlip();
}

bool MountController::isMeridianFlipPending() const {
    return pimpl->isMeridianFlipPending();
}

double MountController::getTimeToMeridian() const {
    return pimpl->getTimeToMeridian();
}

int MountController::getPierSide() const {
    return pimpl->getPierSide();
}

// ============================================
// HAL CONFIGURATION API
// ============================================

bool MountController::getHALConfig(::astro_mount::HALConfig& config) const {
    return pimpl->getHALConfig(config);
}

bool MountController::setHALConfig(const ::astro_mount::HALConfigRequest& request) {
    return pimpl->setHALConfig(request);
}

bool MountController::getHALStatus(::astro_mount::HALStatus& status) const {
    return pimpl->getHALStatus(status);
}

bool MountController::reinitializeHAL(const ::astro_mount::HALReinitRequest& request) {
    return pimpl->reinitializeHAL(request);
}

void MountController::startGamepadLoop() {
    pimpl->startGamepadLoop();
}

void MountController::stopGamepad() {
    pimpl->stopGamepad();
}

void MountController::setGamepadMode(GamepadMode mode) {
    pimpl->setGamepadMode(static_cast<int>(mode));
}

} // namespace controllers
} // namespace astro_mount

