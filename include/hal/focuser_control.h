#ifndef FOCUSER_CONTROL_H
#define FOCUSER_CONTROL_H

#include <string>
#include <memory>
#include <functional>
#include <chrono>

namespace astro_mount {
namespace hal {

/**
 * @brief Focuser position and status data
 */
struct FocuserStatus {
    int32_t position{0};            // Current absolute position [steps]
    int32_t max_position{100000};   // Maximum position [steps]
    bool moving{false};             // Focuser is moving
    bool connected{false};          // Hardware connected
    double temperature_c{20.0};     // Internal temperature [°C]
    double hfd{0.0};                // Last measured HFD [pixels]
    std::string model_name;         // Model name
    std::string error_message;      // Error details
    int32_t step_size_microns{10};  // Step size in microns
};

/**
 * @brief Abstract focuser HAL interface
 *
 * Supports multiple focuser hardware types:
 * - ZWO EAF (I²C via USB)
 * - MoonLite (serial RS-232)
 * - Pegasus FocusCube (USB HID)
 * - Simulated for testing
 */
class FocuserControl {
public:
    virtual ~FocuserControl() = default;

    /// @brief Unique name/identifier for this focuser type
    virtual std::string name() const = 0;

    /// @brief Connect to focuser hardware
    virtual bool connect() = 0;

    /// @brief Disconnect from focuser hardware
    virtual void disconnect() = 0;

    /// @brief Check if focuser is connected and operational
    virtual bool isConnected() const = 0;

    /// @brief Move to absolute position
    /// @param position Target position in steps
    /// @param speed Movement speed (0=silent..100=max)
    /// @param synchronous Wait for move to complete
    /// @return true if move command accepted
    virtual bool moveTo(int32_t position, int32_t speed = 50, bool synchronous = false) = 0;

    /// @brief Halt any current motion
    virtual void halt() = 0;

    /// @brief Read current focuser status
    virtual FocuserStatus getStatus() = 0;

    /// @brief Read current temperature from focuser
    virtual double readTemperature() = 0;

    /// @brief Initialize focuser (set max position, home if needed)
    virtual bool initialize() = 0;

    /// @brief Check if focuser is ready for commands
    virtual bool isReady() const = 0;

    /// @brief Get maximum position supported by hardware
    virtual int32_t getMaxPosition() const = 0;

    /**
     * @brief Set callback for auto-focus HFD measurements
     * Called when new HFD data is available from camera
     */
    virtual void setHfdCallback(std::function<void(double hfd)> callback);

protected:
    std::function<void(double hfd)> hfd_callback_;
};

} // namespace hal
} // namespace astro_mount

#endif // FOCUSER_CONTROL_H
