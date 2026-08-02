#include "hal/focuser_control.h"
#include <thread>
#include <chrono>

namespace astro_mount {
namespace hal {

// Default implementation for setHfdCallback
void FocuserControl::setHfdCallback(std::function<void(double hfd)> callback) {
    hfd_callback_ = std::move(callback);
}

} // namespace hal
} // namespace astro_mount
