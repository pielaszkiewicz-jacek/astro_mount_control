#pragma once
#include <functional>
#include <chrono>
#include <string>

namespace astro_mount {
namespace hal {

enum class MotorType {
    STEPPER,        // Silnik krokowy
    SERVO,          // Serwonapęd
    BRUSHED_DC,     // Silnik DC z komutatorem
    BRUSHLESS_DC,   // Silnik bezszczotkowy
    CANOPEN_SERVO,  // Serwonapęd CANopen
    VIRTUAL         // Wirtualny (do testów)
};

enum class ControlMode {
    POSITION,       // Sterowanie pozycją
    VELOCITY,       // Sterowanie prędkością
    TORQUE,         // Sterowanie momentem
    TRAJECTORY,     // Sterowanie trajektorią
    OPEN_LOOP       // Sterowanie otwartej pętli
};

struct MotorConfig {
    MotorType type{MotorType::STEPPER};
    ControlMode default_mode{ControlMode::POSITION};
    double max_velocity{2.0};          // deg/s
    double max_acceleration{0.5};      // deg/s²
    double max_torque{100.0};          // %
    double encoder_counts_per_degree{10000.0};
    double gear_ratio{1.0};
    bool enable_current_limit{true};
    double current_limit{5.0};         // A
    bool enable_temperature_protection{true};
    double max_temperature{80.0};      // °C
    
    // Metody pomocnicze
    double countsToDegrees(uint32_t counts) const {
        return static_cast<double>(counts) / encoder_counts_per_degree;
    }
    
    uint32_t degreesToCounts(double degrees) const {
        return static_cast<uint32_t>(degrees * encoder_counts_per_degree);
    }
};

class MotorControl {
public:
    virtual ~MotorControl() = default;
    
    // Podstawowe operacje
    virtual bool enable() = 0;
    virtual bool disable() = 0;
    virtual bool isEnabled() const = 0;
    
    // Sterowanie
    virtual bool setPosition(double position_deg, 
                            double velocity_deg_s = 0.0, 
                            double acceleration_deg_s2 = 0.0) = 0;
    virtual bool setVelocity(double velocity_deg_s, 
                            double acceleration_deg_s2 = 0.0) = 0;
    virtual bool setTorque(double torque_percent) = 0;
    virtual bool stop() = 0;
    virtual bool emergencyStop() = 0;
    
    // Informacje o stanie
    virtual double getActualPosition() const = 0;
    virtual double getActualVelocity() const = 0;
    virtual double getActualTorque() const = 0;
    virtual bool isMoving() const = 0;
    virtual bool targetReached() const = 0;
    virtual bool inErrorState() const = 0;
    virtual std::string getErrorString() const = 0;
    
    // Konfiguracja
    virtual bool configure(const MotorConfig& config) = 0;
    virtual MotorConfig getConfiguration() const = 0;
    
    // Callbacki
    using PositionCallback = std::function<void(double position, double velocity, double torque)>;
    using ErrorCallback = std::function<void(const std::string& error, int error_code)>;
    using StateChangeCallback = std::function<void(bool enabled, bool moving)>;
    
    virtual void setPositionCallback(PositionCallback callback) = 0;
    virtual void setErrorCallback(ErrorCallback callback) = 0;
    virtual void setStateChangeCallback(StateChangeCallback callback) = 0;
    
    // Diagnostyka
    virtual double getTemperature() const = 0;
    virtual double getCurrent() const = 0;
    virtual double getVoltage() const = 0;
    virtual uint32_t getOperationTime() const = 0; // w sekundach
};

} // namespace hal
} // namespace astro_mount