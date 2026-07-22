#include "hal/camera_control.h"

namespace astro_mount {
namespace hal {

void CameraControl::setFrameCallback(std::function<void(const ImageData&)> callback) {
    frame_callback_ = std::move(callback);
}

} // namespace hal
} // namespace astro_mount
