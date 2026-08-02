#ifndef DOME_CONTROLLER_H
#define DOME_CONTROLLER_H

#include <memory>
#include <functional>
#include <atomic>
#include <thread>
#include "hal/dome_control.h"

namespace astro_mount {
namespace controllers {

class DomeController {
public:
    explicit DomeController(std::unique_ptr<hal::DomeControl> dome_hal);
    ~DomeController();

    bool open();
    bool close();
    bool rotateTo(double azimuth_deg);
    bool park();
    bool unpark();
    hal::DomeStatus getStatus() const;

    void setMountAzimuthCallback(std::function<double()> callback);
    void setAutoSync(bool enabled);
    bool isAutoSyncEnabled() const;

private:
    void syncLoop();
    double calculateDomeAzimuth(double mount_az) const;
    std::unique_ptr<hal::DomeControl> dome_hal_;
    std::function<double()> mount_azimuth_callback_;
    std::unique_ptr<std::thread> sync_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> auto_sync_{false};
};

} // namespace controllers
} // namespace astro_mount

#endif
