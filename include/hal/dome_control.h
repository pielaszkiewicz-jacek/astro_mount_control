#ifndef DOME_CONTROL_H
#define DOME_CONTROL_H

#include <string>
#include <cstdint>

namespace astro_mount {
namespace hal {

enum class DomeType {
    UNKNOWN,
    ROTATING,
    ROLLOFF
};

enum class DomeState {
    CLOSED,
    OPENING,
    OPEN,
    CLOSING,
    ROTATING,
    PARKED,
    ERROR
};

struct DomeStatus {
    DomeType dome_type{DomeType::UNKNOWN};
    DomeState state{DomeState::CLOSED};
    double azimuth_deg{0.0};
    double target_azimuth_deg{0.0};
    bool can_rotate{false};
    bool can_open{true};
    bool shutter_open{false};
    double aperture_deg{180.0};
    bool parked{false};
    bool sync_enabled{false};
    double sync_offset_deg{0.0};
    std::string error_message;
    double home_azimuth_deg{0.0};
    double park_azimuth_deg{0.0};
};

class DomeControl {
public:
    virtual ~DomeControl() = default;
    virtual std::string name() const = 0;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual bool openShutter() = 0;
    virtual bool closeShutter() = 0;
    virtual bool rotateTo(double azimuth_deg, bool wait = false) = 0;
    virtual bool halt() = 0;
    virtual bool park() = 0;
    virtual bool unpark() = 0;
    virtual bool goHome() = 0;
    virtual DomeStatus getStatus() = 0;
    virtual bool isMoving() const = 0;
    virtual DomeType getDomeType() const = 0;
};

} // namespace hal
} // namespace astro_mount

#endif
