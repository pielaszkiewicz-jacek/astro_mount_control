#ifndef ST4_GUIDER_H
#define ST4_GUIDER_H
#include <memory>
#include <functional>
#include "hal/st4_control.h"

namespace astro_mount { namespace controllers {

class St4Guider {
public:
    explicit St4Guider(std::unique_ptr<hal::St4Control> hal);
    ~St4Guider();

    bool startPHD2(const std::string& host, int port);
    void stopPHD2();
    bool pulse(hal::St4Direction dir, int duration_ms);
    bool calibrate();

    struct Stats {
        int pulses_sent{0}, pulses_failed{0};
        double ra_correction{0}, dec_correction{0};
        double rms_ra{0}, rms_dec{0};
        bool calibrated{false};
    };
    Stats getStats() const;

private:
    std::unique_ptr<hal::St4Control> hal_;
    bool phd2_connected_{false};
    Stats stats_;
};

}} // namespace astro_mount::controllers
#endif
