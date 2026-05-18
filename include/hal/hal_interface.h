#pragma once
#include <memory>
#include <string>
#include <functional>
#include <chrono>
#include <vector>
#include "hal/motor_control.h"
#include "hal/encoder_reader.h"

namespace astro_mount {
namespace hal {

// Forward declarations
class SafetyMonitor;
class SensorInterface;
struct HALConfig;

enum class HALFeature {
    CANOPEN_SUPPORT,      // Wsparcie CANopen/CiA 402
    SERIAL_SUPPORT,       // Wsparcie portów szeregowych
    ETHERNET_SUPPORT,     // Wsparcie Ethernet (EtherCAT, Modbus TCP)
    PID_CONTROL,          // Wsparcie kontrolerów PID
    TRAJECTORY_CONTROL,   // Wsparcie sterowania trajektorią
    ENCODER_FEEDBACK,     // Odczyt enkoderów
    SAFETY_MONITORING,    // Monitorowanie bezpieczeństwa
    SENSOR_MONITORING,    // Monitorowanie czujników
    REAL_TIME_CONTROL,    // Sterowanie w czasie rzeczywistym
    DEROTATOR_SUPPORT,    // Wsparcie derotatora (pole obserwacyjne)
    MANUAL_CONTROL        // Ręczne sterowanie (gamepad/joystick)
};


class HALInterface {
public:
    virtual ~HALInterface() = default;
    
    // Inicjalizacja i zarządzanie
    virtual bool initialize(const HALConfig& config) = 0;
    virtual void shutdown() = 0;
    virtual bool isInitialized() const = 0;
    
    // Fabryka komponentów
    virtual std::unique_ptr<MotorControl> createMotorControl(int axis_id) = 0;
    virtual std::unique_ptr<EncoderReader> createEncoderReader(int axis_id) = 0;
    virtual std::unique_ptr<SafetyMonitor> createSafetyMonitor() = 0;
    virtual std::unique_ptr<SensorInterface> createSensorInterface() = 0;
    
    // Fabryka derotatora
    virtual std::unique_ptr<MotorControl> createDerotatorMotor() { return nullptr; }
    virtual std::unique_ptr<EncoderReader> createDerotatorEncoder() { return nullptr; }
    
    // Konfiguracja derotatora (parametry specyficzne dla sterownika CANopen/step/servo)
    virtual bool configureDerotator(const struct DerotatorConfig& config) { return false; }

    // Informacje o platformie

    virtual std::string getPlatformName() const = 0;
    virtual std::string getHardwareVersion() const = 0;
    virtual std::vector<HALFeature> getSupportedFeatures() const = 0;
    virtual bool supportsFeature(HALFeature feature) const = 0;
    
    // Zarządzanie zasobami
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual bool isRunning() const = 0;
    
    // Diagnostyka
    virtual std::string getStatus() const = 0;
    virtual std::string getErrorMessages() const = 0;
    virtual void clearErrors() = 0;
};

} // namespace hal
} // namespace astro_mount