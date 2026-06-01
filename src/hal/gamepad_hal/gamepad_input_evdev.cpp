#include "gamepad_input_evdev.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <dirent.h>
#include "logging/logger.h"
#include <sys/stat.h>
#include <linux/joystick.h>
#include <linux/input.h>
#include <sys/ioctl.h>

namespace astro_mount {
namespace hal {

// ============================================================================
// Construction / Destruction
// ============================================================================

EvdevGamepadInput::EvdevGamepadInput() {
    setupDefaultMappings();
}

EvdevGamepadInput::~EvdevGamepadInput() {
    shutdown();
}

// ============================================================================
// Public interface
// ============================================================================

bool EvdevGamepadInput::initialize(const std::string& device_path) {
    if (running_) {
        logging::Logger::get("gamepad")->error("[EvdevGamepadInput] Already initialized");
        return false;
    }
    
    std::string path = device_path;
    if (path.empty()) {
        path = autoDetect();
        if (path.empty()) {
            logging::Logger::get("gamepad")->error("[EvdevGamepadInput] No gamepad device found");
            return false;
        }
    }
    
    if (!openDevice(path)) {
        return false;
    }
    
    device_path_ = path;
    running_ = true;
    poll_thread_ = std::thread(&EvdevGamepadInput::pollLoop, this);
    
    logging::Logger::get("gamepad")->info("[EvdevGamepadInput] Opened: {} ({}) axes={} buttons={}",
              device_name_, path, axis_count_, button_count_);
    return true;
}

void EvdevGamepadInput::shutdown() {
    if (running_) {
        running_ = false;
        if (poll_thread_.joinable()) {
            poll_thread_.join();
        }
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.connected = false;
    }
}

bool EvdevGamepadInput::isConnected() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return state_.connected;
}

GamepadState EvdevGamepadInput::readState() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.timestamp = std::chrono::steady_clock::now();
    return state_;
}

std::string EvdevGamepadInput::getDeviceName() const {
    return device_name_;
}

int EvdevGamepadInput::getAxisCount() const {
    return axis_count_;
}

int EvdevGamepadInput::getButtonCount() const {
    return button_count_;
}

void EvdevGamepadInput::applyButtonMapping(const std::map<int, std::string>& mapping) {
    for (const auto& [idx, action_str] : mapping) {
        if (action_str == "home")           button_map_[idx] = ButtonAction::HOME;
        else if (action_str == "stop")      button_map_[idx] = ButtonAction::STOP;
        else if (action_str == "emergency_stop") button_map_[idx] = ButtonAction::EMERGENCY_STOP;
        else if (action_str == "park")      button_map_[idx] = ButtonAction::PARK;
        else if (action_str == "speed_up")  button_map_[idx] = ButtonAction::SPEED_UP;
        else if (action_str == "speed_down") button_map_[idx] = ButtonAction::SPEED_DOWN;
        else if (action_str == "manual_toggle") button_map_[idx] = ButtonAction::MANUAL_TOGGLE;
        else if (action_str == "none")      button_map_[idx] = ButtonAction::NONE;
        else {
            logging::Logger::get("gamepad")->warn("[EvdevGamepadInput] Unknown button action '{}' for index {}", action_str, idx);
        }
    }
}

void EvdevGamepadInput::applyAxisMapping(const std::map<int, std::string>& mapping) {
    for (const auto& [idx, axis_str] : mapping) {
        if (axis_str == "lx")         axis_map_[idx] = AxisAction::LX;
        else if (axis_str == "ly")    axis_map_[idx] = AxisAction::LY;
        else if (axis_str == "rx")    axis_map_[idx] = AxisAction::RX;
        else if (axis_str == "ry")    axis_map_[idx] = AxisAction::RY;
        else if (axis_str == "trigger_l") axis_map_[idx] = AxisAction::TRIGGER_L;
        else if (axis_str == "trigger_r") axis_map_[idx] = AxisAction::TRIGGER_R;
        else if (axis_str == "pov_x")  axis_map_[idx] = AxisAction::POV_X;
        else if (axis_str == "pov_y")  axis_map_[idx] = AxisAction::POV_Y;
        else if (axis_str == "none")   axis_map_[idx] = AxisAction::NONE;
        else {
            logging::Logger::get("gamepad")->warn("[EvdevGamepadInput] Unknown axis action '{}' for index {}", axis_str, idx);
        }
    }
}

// ============================================================================
// Device opening
// ============================================================================

bool EvdevGamepadInput::openDevice(const std::string& path) {
    // Detect API type from path
    if (path.find("/dev/input/js") != std::string::npos) {
        return openJoystickDevice(path);
    } else {
        return openEvdevDevice(path);
    }
}

bool EvdevGamepadInput::openJoystickDevice(const std::string& path) {
    fd_ = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd_ < 0) {
        logging::Logger::get("gamepad")->error("[EvdevGamepadInput] Cannot open {}: {}", path, strerror(errno));
        return false;
    }

    use_evdev_ = false;

    // Get device name
    char name[128] = {0};
    if (ioctl(fd_, JSIOCGNAME(sizeof(name)), name) >= 0) {
        device_name_ = name;
    } else {
        device_name_ = "Unknown Joystick";
    }

    // Get axis / button counts
    uint8_t axes = 0, buttons = 0;
    ioctl(fd_, JSIOCGAXES, &axes);
    ioctl(fd_, JSIOCGBUTTONS, &buttons);
    axis_count_ = axes;
    button_count_ = buttons;

    raw_axes_.assign(axis_count_, 0);

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.connected = true;
    }

    return true;
}

bool EvdevGamepadInput::openEvdevDevice(const std::string& path) {
    fd_ = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd_ < 0) {
        logging::Logger::get("gamepad")->error("[EvdevGamepadInput] Cannot open {}: {}", path, strerror(errno));
        return false;
    }

    use_evdev_ = true;

    // Get device name
    char name[256] = {0};
    if (ioctl(fd_, EVIOCGNAME(sizeof(name)), name) >= 0) {
        device_name_ = name;
    } else {
        device_name_ = "Unknown Evdev Device";
    }

    // Get axis and button counts
    uint8_t abs_bits[ABS_MAX / 8 + 1] = {0};
    uint8_t key_bits[KEY_MAX / 8 + 1] = {0};

    ioctl(fd_, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits);
    ioctl(fd_, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits);

    // Count absolute axes
    for (int i = 0; i < ABS_MAX; i++) {
        if (abs_bits[i / 8] & (1 << (i % 8))) {
            axis_count_++;
        }
    }

    // Count buttons / keys
    for (int i = BTN_JOYSTICK; i < KEY_MAX && i < BTN_JOYSTICK + 64; i++) {
        if (key_bits[i / 8] & (1 << (i % 8))) {
            button_count_++;
        }
    }
    // Also count BTN_GAMEPAD range
    for (int i = BTN_GAMEPAD; i < BTN_GAMEPAD + 16 && i < (int)sizeof(key_bits)*8; i++) {
        if (i >= (int)sizeof(key_bits)*8) break;
        if (key_bits[i / 8] & (1 << (i % 8))) {
            button_count_++;
        }
    }

    raw_axes_.assign(axis_count_, 0);

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.connected = true;
    }

    return true;
}

// ============================================================================
// Auto-detection
// ============================================================================

std::string EvdevGamepadInput::autoDetect() {
    // Try legacy joystick API first
    for (int i = 0; i < 4; ++i) {
        std::string path = "/dev/input/js" + std::to_string(i);
        struct stat st;
        if (stat(path.c_str(), &st) == 0 && S_ISCHR(st.st_mode)) {
            // Try to open to confirm it's a joystick
            int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
            if (fd >= 0) {
                uint8_t axes = 0;
                if (ioctl(fd, JSIOCGAXES, &axes) >= 0 && axes >= 2) {
                    ::close(fd);
                    return path;
                }
                ::close(fd);
            }
        }
    }

    // Try evdev API — scan /dev/input/event* for devices with ABS axes
    DIR* dir = opendir("/dev/input");
    if (!dir) return "";

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "event", 5) != 0) continue;

        std::string path = std::string("/dev/input/") + entry->d_name;
        int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        // Check if it has EV_ABS capability (analog axes)
        uint8_t abs_bits[ABS_MAX / 8 + 1] = {0};
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits);

        int axis_count = 0;
        for (int i = 0; i < ABS_MAX; i++) {
            if (abs_bits[i / 8] & (1 << (i % 8))) {
                axis_count++;
            }
        }

        if (axis_count >= 2) {
            // Has at least 2 analog axes — likely a gamepad
            ::close(fd);
            closedir(dir);
            return path;
        }

        ::close(fd);
    }

    closedir(dir);
    return "";
}

// ============================================================================
// Polling loop
// ============================================================================

void EvdevGamepadInput::pollLoop() {
    if (use_evdev_) {
        struct input_event ev;
        while (running_) {
            ssize_t n = ::read(fd_, &ev, sizeof(ev));
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }
                // Device disconnected
                std::lock_guard<std::mutex> lock(state_mutex_);
                state_.connected = false;
                break;
            }
            if (n == (ssize_t)sizeof(ev)) {
                processEvdevEvent(ev);
            }
        }
    } else {
        struct js_event ev;
        while (running_) {
            ssize_t n = ::read(fd_, &ev, sizeof(ev));
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }
                // Device disconnected
                std::lock_guard<std::mutex> lock(state_mutex_);
                state_.connected = false;
                break;
            }
            if (n == (ssize_t)sizeof(ev)) {
                processJoystickEvent(ev);
            }
        }
    }
}

// ============================================================================
// Event processing — legacy joystick API
// ============================================================================

void EvdevGamepadInput::processJoystickEvent(const js_event& ev) {
    std::lock_guard<std::mutex> lock(state_mutex_);

    if (ev.type & JS_EVENT_INIT) {
        // Ignore initial calibration events
        return;
    }

    state_.timestamp = std::chrono::steady_clock::now();
    state_.connected = true;

    switch (ev.type) {
    case JS_EVENT_AXIS: {
        // Raw value is [-32768, 32767]
        int idx = ev.number;
        if (idx >= 0 && idx < (int)raw_axes_.size()) {
            raw_axes_[idx] = ev.value;
        }

        double normalized = normalizeAxis(ev.value, -32768, 32767);
        normalized = applyDeadzone(normalized);

        auto it = axis_map_.find(idx);
        AxisAction action = (it != axis_map_.end()) ? it->second : AxisAction::NONE;

        switch (action) {
        case AxisAction::LX: state_.axis_lx = normalized; break;
        case AxisAction::LY: state_.axis_ly = normalized; break;
        case AxisAction::RX: state_.axis_rx = normalized; break;
        case AxisAction::RY: state_.axis_ry = normalized; break;
        case AxisAction::TRIGGER_L: state_.axis_trigger_l = normalized; break;
        case AxisAction::TRIGGER_R: state_.axis_trigger_r = normalized; break;
        case AxisAction::POV_X:
            if (normalized != 0.0) {
                // POV hat position: 0°, 45°, 90°, ...
                // Map continuous axis to discrete angles
                if (normalized < 0) state_.pov_hat = 270.0;   // left
                else state_.pov_hat = 90.0;                    // right
            } else {
                state_.pov_hat = -1.0;  // neutral
            }
            break;
        case AxisAction::POV_Y:
            if (normalized != 0.0) {
                if (normalized < 0) state_.pov_hat = 0.0;     // up
                else state_.pov_hat = 180.0;                   // down
            } else {
                state_.pov_hat = -1.0;  // neutral
            }
            break;
        default: break;
        }
        break;
    }

    case JS_EVENT_BUTTON: {
        bool pressed = (ev.value != 0);
        auto it = button_map_.find(ev.number);
        ButtonAction action = (it != button_map_.end()) ? it->second : ButtonAction::NONE;

        switch (action) {
        case ButtonAction::STOP:           state_.button_stop = pressed; break;
        case ButtonAction::EMERGENCY_STOP: state_.button_emergency_stop = pressed; break;
        case ButtonAction::PARK:           state_.button_park = pressed; break;
        case ButtonAction::SPEED_UP:       state_.button_speed_up = pressed; break;
        case ButtonAction::SPEED_DOWN:     state_.button_speed_down = pressed; break;
        case ButtonAction::MANUAL_TOGGLE:  state_.button_manual_toggle = pressed; break;
        case ButtonAction::HOME:           state_.button_home = pressed; break;
        default: break;
        }
        break;
    }
    }
}

// ============================================================================
// Event processing — evdev API
// ============================================================================

void EvdevGamepadInput::processEvdevEvent(const input_event& ev) {
    std::lock_guard<std::mutex> lock(state_mutex_);

    state_.timestamp = std::chrono::steady_clock::now();
    state_.connected = true;

    switch (ev.type) {
    case EV_ABS: {
        // Query axis info for min/max
        struct input_absinfo absinfo;
        int idx = -1;

        // Map ev.code to logical axis index
        // For gamepads: ABS_X=0, ABS_Y=1, ABS_Z=2, ABS_RX=3, ABS_RY=4, ABS_RZ=5,
        //               ABS_HAT0X=16, ABS_HAT0Y=17, ABS_GAS=9, ABS_BRAKE=10
        switch (ev.code) {
        case ABS_X:        idx = 0; break;
        case ABS_Y:        idx = 1; break;
        case ABS_Z:        idx = 2; break;
        case ABS_RX:       idx = 3; break;
        case ABS_RY:       idx = 4; break;
        case ABS_RZ:       idx = 5; break;
        case ABS_THROTTLE: idx = 6; break;
        case ABS_RUDDER:   idx = 7; break;
        case ABS_WHEEL:    idx = 8; break;
        case ABS_GAS:      idx = 9; break;
        case ABS_BRAKE:    idx = 10; break;
        case ABS_HAT0X:    idx = 16; break;
        case ABS_HAT0Y:    idx = 17; break;
        default:
            // Unknown axis — map to available raw_axes_ slot if possible
            if (ev.code < (int)raw_axes_.size()) idx = ev.code;
            break;
        }

        if (idx < 0) break;

        // Get min/max for this axis
        int axis_min = -32768, axis_max = 32767;
        if (ioctl(fd_, EVIOCGABS(ev.code), &absinfo) >= 0) {
            axis_min = absinfo.minimum;
            axis_max = absinfo.maximum;
        }

        if (idx < (int)raw_axes_.size()) {
            raw_axes_[idx] = ev.value;
        }

        double normalized = normalizeAxis(ev.value, axis_min, axis_max);
        normalized = applyDeadzone(normalized);

        auto it = axis_map_.find(idx);
        AxisAction action = (it != axis_map_.end()) ? it->second : AxisAction::NONE;

        // Auto-detect mapping for unknown axes
        if (action == AxisAction::NONE) {
            // Try to infer axis mapping
            if (idx == 0) action = AxisAction::LX;
            else if (idx == 1) action = AxisAction::LY;
            else if (idx == 3) action = AxisAction::RX;
            else if (idx == 4) action = AxisAction::RY;
            else if (idx == 2) action = AxisAction::TRIGGER_L;
            else if (idx == 5) action = AxisAction::TRIGGER_R;
            else if (idx == 16) action = AxisAction::POV_X;
            else if (idx == 17) action = AxisAction::POV_Y;
        }

        switch (action) {
        case AxisAction::LX: state_.axis_lx = normalized; break;
        case AxisAction::LY: state_.axis_ly = normalized; break;
        case AxisAction::RX: state_.axis_rx = normalized; break;
        case AxisAction::RY: state_.axis_ry = normalized; break;
        case AxisAction::TRIGGER_L: state_.axis_trigger_l = normalized; break;
        case AxisAction::TRIGGER_R: state_.axis_trigger_r = normalized; break;
        case AxisAction::POV_X:
            if (normalized != 0.0) {
                state_.pov_hat = (normalized < 0) ? 270.0 : 90.0;
            } else {
                state_.pov_hat = -1.0;
            }
            break;
        case AxisAction::POV_Y:
            if (normalized != 0.0) {
                state_.pov_hat = (normalized < 0) ? 0.0 : 180.0;
            } else {
                state_.pov_hat = -1.0;
            }
            break;
        default: break;
        }
        break;
    }

    case EV_KEY: {
        bool pressed = (ev.value != 0);

        // Map ev.code to button index for our mapping
        int btn_idx = -1;
        if (ev.code >= BTN_JOYSTICK && ev.code <= BTN_JOYSTICK + 15) {
            btn_idx = ev.code - BTN_JOYSTICK;
        } else if (ev.code >= BTN_GAMEPAD && ev.code <= BTN_GAMEPAD + 15) {
            btn_idx = (ev.code - BTN_GAMEPAD) + 16;
        } else if (ev.code >= BTN_TRIGGER && ev.code <= BTN_TRIGGER + 15) {
            btn_idx = (ev.code - BTN_TRIGGER) + 32;
        } else if (ev.code >= BTN_TOP && ev.code <= BTN_TOP + 15) {
            btn_idx = (ev.code - BTN_TOP) + 48;
        } else if (ev.code >= BTN_THUMB && ev.code <= BTN_THUMB + 15) {
            btn_idx = (ev.code - BTN_THUMB) + 64;
        } else if (ev.code >= BTN_THUMB2 && ev.code <= BTN_THUMB2 + 15) {
            btn_idx = (ev.code - BTN_THUMB2) + 80;
        } else if (ev.code >= BTN_TOP2 && ev.code <= BTN_TOP2 + 15) {
            btn_idx = (ev.code - BTN_TOP2) + 96;
        } else if (ev.code == BTN_MODE) {
            btn_idx = 8; // Guide/PS button often maps here
        } else if (ev.code == BTN_SELECT) {
            btn_idx = 6; // Back/Select
        } else if (ev.code == BTN_START) {
            btn_idx = 7; // Start
        } else if (ev.code == BTN_BASE) {
            btn_idx = 8; // Guide button (legacy mapping)
        } else {
            break;
        }

        auto it = button_map_.find(btn_idx);
        ButtonAction action = (it != button_map_.end()) ? it->second : ButtonAction::NONE;

        switch (action) {
        case ButtonAction::STOP:           state_.button_stop = pressed; break;
        case ButtonAction::EMERGENCY_STOP: state_.button_emergency_stop = pressed; break;
        case ButtonAction::PARK:           state_.button_park = pressed; break;
        case ButtonAction::SPEED_UP:       state_.button_speed_up = pressed; break;
        case ButtonAction::SPEED_DOWN:     state_.button_speed_down = pressed; break;
        case ButtonAction::MANUAL_TOGGLE:  state_.button_manual_toggle = pressed; break;
        case ButtonAction::HOME:           state_.button_home = pressed; break;
        default: break;
        }
        break;
    }

    default:
        break;
    }
}

// ============================================================================
// Helpers
// ============================================================================

double EvdevGamepadInput::normalizeAxis(int raw, int axis_min, int axis_max) const {
    if (axis_max <= axis_min) return 0.0;

    // Center at midpoint
    double mid = (axis_min + axis_max) / 2.0;
    double range = (axis_max - axis_min) / 2.0;

    if (range < 1.0) return 0.0;

    double normalized = (raw - mid) / range;

    // Clamp to [-1, 1]
    if (normalized < -1.0) normalized = -1.0;
    if (normalized > 1.0) normalized = 1.0;

    return normalized;
}

double EvdevGamepadInput::applyDeadzone(double value) const {
    if (std::abs(value) < deadzone_) {
        return 0.0;
    }
    // Re-scale so that deadzone edge maps to 0 and 1.0 maps to 1.0
    double sign = (value > 0.0) ? 1.0 : -1.0;
    double scaled = (std::abs(value) - deadzone_) / (1.0 - deadzone_);
    return sign * std::min(scaled, 1.0);
}

void EvdevGamepadInput::setupDefaultMappings() {
    // Default button mapping (Xbox 360/One / PS4 layout)
    //  0=A(south/front), 1=B(east), 2=X(north), 3=Y(west)
    //  4=LB(L1), 5=RB(R1), 6=Back(Select), 7=Start
    //  8=Guide(PS/Xbox), 9=LeftStick, 10=RightStick
    button_map_[0] = ButtonAction::HOME;           // A → home
    button_map_[1] = ButtonAction::STOP;            // B → stop
    button_map_[2] = ButtonAction::NONE;            // X → unassigned
    button_map_[3] = ButtonAction::PARK;            // Y → park
    button_map_[4] = ButtonAction::SPEED_DOWN;      // LB → speed down
    button_map_[5] = ButtonAction::SPEED_UP;        // RB → speed up
    button_map_[6] = ButtonAction::EMERGENCY_STOP;  // Back → emergency stop
    button_map_[7] = ButtonAction::STOP;            // Start → stop
    button_map_[8] = ButtonAction::MANUAL_TOGGLE;   // Guide → toggle manual
    button_map_[9] = ButtonAction::NONE;            // Left stick press
    button_map_[10] = ButtonAction::NONE;           // Right stick press

    // Default axis mapping (standard gamepad)
    //  0=LX, 1=LY, 2=Triggers(LTRT as single axis), 3=RX, 4=RY
    //  For two-trigger layout: 2=LT, 5=RT
    axis_map_[0] = AxisAction::LX;                  // Left stick X → axis1 (RA/HA/Az)
    axis_map_[1] = AxisAction::LY;                  // Left stick Y → axis2 (Dec/Alt)
    axis_map_[2] = AxisAction::TRIGGER_L;           // Z / LT
    axis_map_[3] = AxisAction::RX;                  // Right stick X
    axis_map_[4] = AxisAction::RY;                  // Right stick Y
    axis_map_[5] = AxisAction::TRIGGER_R;           // RZ / RT
    axis_map_[16] = AxisAction::POV_X;              // POV X
    axis_map_[17] = AxisAction::POV_Y;              // POV Y
}

} // namespace hal
} // namespace astro_mount
