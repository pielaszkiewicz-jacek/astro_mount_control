#pragma once
#include <map>
#include <string>
#include <chrono>
#include <functional>

namespace astro_mount {
namespace hal {

/**
 * @brief Represents the current state of a gamepad/joystick.
 *
 * All axis values are normalized to [-1.0, 1.0] where:
 *   - Left/Up/Down on joystick → negative values
 *   - Right/Down on joystick  → positive values
 *   - Center / idle           → 0.0 (± deadzone)
 *
 * Button and POV hat positions are mapped to semantic mount-control actions.
 */
struct GamepadState {
    /// Primary analog axes (joystick sticks).
    /// axis_lx, axis_ly: left stick X and Y
    /// axis_rx, axis_ry: right stick X and Y
    double axis_lx{0.0};
    double axis_ly{0.0};
    double axis_rx{0.0};
    double axis_ry{0.0};

    /// Analog triggers (range [-1.0, 1.0]; 0.0 = released).
    double axis_trigger_l{0.0};
    double axis_trigger_r{0.0};

    /// D-Pad / POV hat (in degrees, -1.0 = neutral).
    double pov_hat{-1.0};

    // ---- Semantic buttons (mapped from physical buttons) ----

    /// Stop all motion (typically START button).
    bool button_stop{false};
    /// Emergency stop (typically BACK/SELECT button).
    bool button_emergency_stop{false};
    /// Park mount (typically GUIDE button / PS button / Xbox button).
    bool button_park{false};
    /// Increase manual slew speed (right bumper).
    bool button_speed_up{false};
    /// Decrease manual slew speed (left bumper).
    bool button_speed_down{false};
    /// Enable/disable manual control mode (right thumb button).
    bool button_manual_toggle{false};
    /// Return to home / center (Y / triangle).
    bool button_home{false};

    /// Whether the device is physically connected.
    bool connected{false};

    /// Timestamp of the last state read.
    std::chrono::steady_clock::time_point timestamp{std::chrono::steady_clock::now()};
};

/**
 * @brief Abstract interface for reading gamepad / joystick input.
 *
 * This is part of the Hardware Abstraction Layer — different backends
 * (Linux evdev, SDL2, serial, mock) implement this interface.
 */
class GamepadInput {
public:
    virtual ~GamepadInput() = default;

    /**
     * @brief Open and initialise the gamepad device.
     * @param device_path  Path to the input device (e.g. "/dev/input/js0").
     *                     Empty string = auto-detect first available gamepad.
     * @return true on success.
     */
    virtual bool initialize(const std::string& device_path = "") = 0;

    /// Close the device and release resources.
    virtual void shutdown() = 0;

    /// Returns true if the device is open and connected.
    virtual bool isConnected() const = 0;

    /**
     * @brief Read the latest gamepad state.
     *
     * Non-blocking — returns the most recently buffered state.
     * If no new data is available, returns the last known state.
     */
    virtual GamepadState readState() = 0;

    /// Human-readable device name (e.g. "Xbox Wireless Controller").
    virtual std::string getDeviceName() const = 0;

    /// Number of axes the device exposes.
    virtual int getAxisCount() const = 0;

    /// Number of buttons the device exposes.
    virtual int getButtonCount() const = 0;

    /**
     * @brief Apply button mapping override from configuration.
     *
     * Map of physical button index → action name string.
     * Supported actions: "home", "stop", "emergency_stop", "park",
     *                    "speed_up", "speed_down", "manual_toggle", "none"
     *
     * Only the indices present in the map are overridden; existing
     * default mappings for other indices are preserved.
     */
    virtual void applyButtonMapping(const std::map<int, std::string>& mapping) = 0;

    /**
     * @brief Apply axis mapping override from configuration.
     *
     * Map of physical axis index → axis name string.
     * Supported axes: "lx", "ly", "rx", "ry", "trigger_l", "trigger_r",
     *                 "pov_x", "pov_y", "none"
     *
     * Only the indices present in the map are overridden.
     */
    virtual void applyAxisMapping(const std::map<int, std::string>& mapping) = 0;
};

} // namespace hal
} // namespace astro_mount
