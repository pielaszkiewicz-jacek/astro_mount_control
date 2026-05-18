#pragma once
#include "hal/gamepad_input.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <map>

// Linux input headers for evdev / legacy joystick API types
#include <linux/joystick.h>
#include <linux/input.h>

namespace astro_mount {
namespace hal {

/**
 * @brief Linux evdev / joystick API implementation of GamepadInput.
 *
 * Supports both:
 *   - Legacy joystick API  (/dev/input/jsX)
 *   - Newer evdev API      (/dev/input/eventX)
 *
 * Auto-detection tries /dev/input/js0 … js3 and /dev/input/event*
 * devices with EV_ABS capability.
 */
class EvdevGamepadInput : public GamepadInput {
public:
    EvdevGamepadInput();
    ~EvdevGamepadInput() override;

    // GamepadInput interface
    bool initialize(const std::string& device_path = "") override;
    void shutdown() override;
    bool isConnected() const override;
    GamepadState readState() override;
    std::string getDeviceName() const override;
    int getAxisCount() const override;
    int getButtonCount() const override;
    void applyButtonMapping(const std::map<int, std::string>& mapping) override;
    void applyAxisMapping(const std::map<int, std::string>& mapping) override;

private:
    // Internal state
    int fd_{-1};
    std::string device_path_;
    std::string device_name_;
    int axis_count_{0};
    int button_count_{0};
    bool use_evdev_{false};

    // Thread that reads events in the background
    std::thread poll_thread_;
    std::atomic<bool> running_{false};

    // Protects state_ from concurrent access
    mutable std::mutex state_mutex_;

    // Latest known state
    GamepadState state_;

    // Raw axis values (before mapping) — index → raw value
    // For evdev: these are [0, 255] or [-32768, 32767] depending on device
    std::vector<int> raw_axes_;

    // Button mapping: physical button index → semantic meaning
    // Default mapping (Xbox-like):
    //   0=A(south), 1=B(east), 2=X(north), 3=Y(west)
    //   4=LB, 5=RB, 6=Back, 7=Start, 8=Guide, 9=LeftStick, 10=RightStick
    enum class ButtonAction {
        NONE,
        STOP,
        EMERGENCY_STOP,
        PARK,
        SPEED_UP,
        SPEED_DOWN,
        MANUAL_TOGGLE,
        HOME
    };

    std::map<int, ButtonAction> button_map_;

    // Axis mapping: physical axis index → semantic axis
    // Default: 0=LX, 1=LY, 2=RX, 3=RY, 4=TriggerL, 5=TriggerR
    enum class AxisAction {
        NONE,
        LX,
        LY,
        RX,
        RY,
        TRIGGER_L,
        TRIGGER_R,
        POV_X,
        POV_Y
    };

    std::map<int, AxisAction> axis_map_;

    // Calibration
    double deadzone_{0.15};      ///< Joystick deadzone [0..1]
    double sensitivity_{1.0};    ///< Global sensitivity curve factor

    // Helpers
    bool openDevice(const std::string& path);
    bool openJoystickDevice(const std::string& path);
    bool openEvdevDevice(const std::string& path);
    std::string autoDetect();
    void pollLoop();
    void processJoystickEvent(const js_event& ev);
    void processEvdevEvent(const input_event& ev);
    double normalizeAxis(int raw, int axis_min, int axis_max) const;
    double applyDeadzone(double value) const;
    void setupDefaultMappings();
};

} // namespace hal
} // namespace astro_mount
