#ifndef MOUNT_EXCEPTIONS_H
#define MOUNT_EXCEPTIONS_H

#include <stdexcept>
#include <string>
#include <map>

namespace astro_mount {
namespace exceptions {

/**
 * @brief Error codes for mount controller system
 */
enum class ErrorCode {
    // General errors
    UNKNOWN_ERROR = 1000,
    INVALID_STATE,
    INVALID_CONFIGURATION,
    
    // Communication errors
    COMMUNICATION_FAILURE = 2000,
    CANOPEN_ERROR,
    ENCODER_ERROR,
    GUIDER_CONNECTION_FAILED,
    NETWORK_ERROR,
    
    // Calibration errors
    CALIBRATION_FAILED = 3000,
    INSUFFICIENT_MEASUREMENTS,
    CALIBRATION_DIVERGENCE,
    TPOINT_FIT_ERROR,
    
    // Safety errors
    SAFETY_VIOLATION = 4000,
    EMERGENCY_STOP_TRIGGERED,
    POSITION_LIMIT_EXCEEDED,
    VELOCITY_LIMIT_EXCEEDED,
    TEMPERATURE_LIMIT_EXCEEDED,
    
    // Operational errors
    OPERATION_FAILED = 5000,
    SLEW_FAILED,
    TRACKING_FAILED,
    PARK_FAILED,
    UNPARK_FAILED,
    
    // Configuration errors
    CONFIGURATION_ERROR = 6000,
    INVALID_PARAMETER,
    MISSING_REQUIRED_FIELD,
    VALIDATION_FAILED
};

/**
 * @brief Base exception class for mount controller system
 */
class MountException : public std::runtime_error {
public:
    MountException(ErrorCode code, const std::string& message, 
                   const std::string& component = "")
        : std::runtime_error(message), 
          code_(code), 
          component_(component) {}
    
    MountException(ErrorCode code, const std::string& message,
                   const std::string& component,
                   const std::map<std::string, std::string>& context)
        : std::runtime_error(message),
          code_(code),
          component_(component),
          context_(context) {}
    
    ErrorCode getErrorCode() const { return code_; }
    const std::string& getComponent() const { return component_; }
    const std::map<std::string, std::string>& getContext() const { return context_; }
    
    void addContext(const std::string& key, const std::string& value) {
        context_[key] = value;
    }
    
    std::string toString() const {
        std::string result = "MountException[code=" + std::to_string(static_cast<int>(code_));
        if (!component_.empty()) {
            result += ", component=" + component_;
        }
        result += "]: " + what();
        
        if (!context_.empty()) {
            result += "\nContext:";
            for (const auto& [key, value] : context_) {
                result += "\n  " + key + ": " + value;
            }
        }
        
        return result;
    }
    
private:
    ErrorCode code_;
    std::string component_;
    std::map<std::string, std::string> context_;
};

/**
 * @brief Communication-related exceptions
 */
class CommunicationException : public MountException {
public:
    CommunicationException(ErrorCode code, const std::string& message,
                          const std::string& component = "",
                          const std::map<std::string, std::string>& context = {})
        : MountException(code, message, component, context) {}
};

/**
 * @brief CANOpen specific communication errors
 */
class CanOpenException : public CommunicationException {
public:
    CanOpenException(const std::string& message, int node_id = -1,
                     const std::string& additional_info = "")
        : CommunicationException(ErrorCode::CANOPEN_ERROR, message, "CanOpenInterface") {
        if (node_id >= 0) {
            addContext("node_id", std::to_string(node_id));
        }
        if (!additional_info.empty()) {
            addContext("additional_info", additional_info);
        }
    }
};

/**
 * @brief Encoder-related exceptions
 */
class EncoderException : public CommunicationException {
public:
    EncoderException(const std::string& message, const std::string& encoder_type = "",
                     int axis = -1)
        : CommunicationException(ErrorCode::ENCODER_ERROR, message, "EncoderHandler") {
        if (!encoder_type.empty()) {
            addContext("encoder_type", encoder_type);
        }
        if (axis >= 0) {
            addContext("axis", std::to_string(axis));
        }
    }
};

/**
 * @brief Calibration-related exceptions
 */
class CalibrationException : public MountException {
public:
    CalibrationException(ErrorCode code, const std::string& message,
                        const std::map<std::string, std::string>& context = {})
        : MountException(code, message, "Calibration", context) {}
};

/**
 * @brief TPOINT calibration specific errors
 */
class TPointException : public CalibrationException {
public:
    TPointException(const std::string& message, int measurement_count = 0,
                    double max_residual = 0.0)
        : CalibrationException(ErrorCode::TPOINT_FIT_ERROR, message) {
        if (measurement_count > 0) {
            addContext("measurement_count", std::to_string(measurement_count));
        }
        if (max_residual > 0.0) {
            addContext("max_residual", std::to_string(max_residual));
        }
    }
};

/**
 * @brief Configuration-related exceptions
 */
class ConfigurationException : public MountException {
public:
    ConfigurationException(ErrorCode code, const std::string& message,
                          const std::string& config_path = "",
                          const std::map<std::string, std::string>& context = {})
        : MountException(code, message, "Configuration", context) {
        if (!config_path.empty()) {
            addContext("config_path", config_path);
        }
    }
};

/**
 * @brief Safety-related exceptions (emergency conditions)
 */
class SafetyException : public MountException {
public:
    SafetyException(ErrorCode code, const std::string& message,
                   const std::map<std::string, std::string>& context = {})
        : MountException(code, message, "SafetyController", context) {}
};

/**
 * @brief Emergency stop triggered
 */
class EmergencyStopException : public SafetyException {
public:
    EmergencyStopException(const std::string& reason, 
                          const std::map<std::string, std::string>& context = {})
        : SafetyException(ErrorCode::EMERGENCY_STOP_TRIGGERED, 
                         "EMERGENCY STOP: " + reason, context) {}
};

/**
 * @brief Operational exceptions during mount operations
 */
class OperationException : public MountException {
public:
    OperationException(ErrorCode code, const std::string& message,
                      const std::string& operation = "",
                      const std::map<std::string, std::string>& context = {})
        : MountException(code, message, "MountController", context) {
        if (!operation.empty()) {
            addContext("operation", operation);
        }
    }
};

/**
 * @brief Slew operation specific errors
 */
class SlewException : public OperationException {
public:
    SlewException(const std::string& message, double target_ra = 0.0,
                  double target_dec = 0.0, const std::string& reason = "")
        : OperationException(ErrorCode::SLEW_FAILED, message, "slewToEquatorial") {
        if (target_ra != 0.0 || target_dec != 0.0) {
            addContext("target_ra", std::to_string(target_ra));
            addContext("target_dec", std::to_string(target_dec));
        }
        if (!reason.empty()) {
            addContext("reason", reason);
        }
    }
};

/**
 * @brief Tracking operation specific errors
 */
class TrackingException : public OperationException {
public:
    TrackingException(const std::string& message, double tracking_error = 0.0,
                     const std::string& mode = "")
        : OperationException(ErrorCode::TRACKING_FAILED, message, "startTracking") {
        if (tracking_error > 0.0) {
            addContext("tracking_error_arcsec", std::to_string(tracking_error));
        }
        if (!mode.empty()) {
            addContext("mode", mode);
        }
    }
};

} // namespace exceptions
} // namespace astro_mount

#endif // MOUNT_EXCEPTIONS_H