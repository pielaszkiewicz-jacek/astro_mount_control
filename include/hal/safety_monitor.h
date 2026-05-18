#pragma once
#include <functional>
#include <memory>
#include <string>
#include <chrono>

namespace astro_mount {
namespace hal {

// Forward declaration
struct SafetyConfig;

// Callback types
using LimitCallback = std::function<void(int axis_id, const std::string& limit_name, double current_value)>;
using ErrorCallback = std::function<void(int axis_id, const std::string& error_message)>;

// Status structure
struct SafetyStatus {
    enum class State {
        NORMAL,
        WARNING,
        LIMIT_EXCEEDED,
        EMERGENCY_STOP,
        ERROR
    };
    
    State overall_state{State::NORMAL};
    std::chrono::system_clock::time_point timestamp;
    
    // Per-axis status
    struct AxisStatus {
        bool limits_ok{true};
        bool temperature_ok{true};
        bool current_ok{true};
        bool voltage_ok{true};
        bool communication_ok{true};
        std::string error_message;
    };
    
    std::array<AxisStatus, 3> axes_status;
    
    // System-wide status
    double system_temperature{0.0};
    double system_current{0.0};
    double system_voltage{0.0};
    bool emergency_stop_active{false};
    bool safety_circuit_ok{true};
    
    std::string getStateString() const {
        switch (overall_state) {
            case State::NORMAL: return "NORMAL";
            case State::WARNING: return "WARNING";
            case State::LIMIT_EXCEEDED: return "LIMIT_EXCEEDED";
            case State::EMERGENCY_STOP: return "EMERGENCY_STOP";
            case State::ERROR: return "ERROR";
            default: return "UNKNOWN";
        }
    }
};

// Safety monitor interface
class SafetyMonitor {
public:
    virtual ~SafetyMonitor() = default;
    
    // Inicjalizacja i zarządzanie
    virtual bool initialize(const SafetyConfig& config) = 0;
    virtual void shutdown() = 0;
    virtual bool isInitialized() const = 0;
    
    // Sprawdzanie statusu
    virtual SafetyStatus getStatus() const = 0;
    virtual bool checkLimits(int axis_id) = 0;
    virtual bool emergencyStop(int axis_id) = 0;
    virtual bool clearErrors(int axis_id) = 0;
    
    // Callbacks
    virtual void setLimitCallback(LimitCallback callback) = 0;
    virtual void setErrorCallback(ErrorCallback callback) = 0;
    
    // Diagnostyka
    virtual std::string getDiagnostics() const = 0;
};

// Konfiguracja bezpieczeństwa
struct SafetyConfig {
    // Limity dla każdej osi
    struct AxisLimits {
        double min_position_deg{-270.0};
        double max_position_deg{270.0};
        double max_velocity_deg_s{5.0};
        double max_acceleration_deg_s2{2.0};
        double max_current_a{10.0};
        double max_temperature_c{80.0};
        double min_voltage_v{20.0};
        double max_voltage_v{30.0};
    };
    
    std::array<AxisLimits, 3> axes_limits;
    
    // Parametry monitorowania
    uint32_t monitoring_rate_hz{10};      // Częstotliwość monitorowania
    double warning_threshold{0.9};       // Próg ostrzeżenia (90% limitu)
    uint32_t emergency_stop_delay_ms{100}; // Opóźnienie zatrzymania awaryjnego
    
    // System-wide limits
    double max_system_current_a{30.0};
    double max_system_temperature_c{70.0};
    double min_system_voltage_v{22.0};
    double max_system_voltage_v{28.0};
    
    // Komunikacja
    bool enable_communication_monitoring{true};
    uint32_t communication_timeout_ms{1000};
    
    // Logging
    bool enable_logging{true};
    std::string log_directory{"/var/log/astro_mount/safety"};
    uint32_t log_rotation_days{7};
};

} // namespace hal
} // namespace astro_mount