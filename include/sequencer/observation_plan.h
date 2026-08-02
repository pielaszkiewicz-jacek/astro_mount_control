#ifndef OBSERVATION_PLAN_H
#define OBSERVATION_PLAN_H

#include <string>
#include <vector>
#include "sequencer/target.h"
#include "sequencer/exposure_sequence.h"

namespace astro_mount {
namespace sequencer {

enum class PlanState {
    IDLE,
    LOADING,
    SLEWING,
    TRACKING,
    FOCUSING,
    EXPOSING,
    PARKING,
    PARKED,
    ERROR,
    COMPLETED
};

struct ObservationPlan {
    std::string name;
    std::string observer;
    std::string date;
    std::string notes;
    std::vector<Target> targets;

    // Session settings
    bool auto_focus{true};
    bool auto_guide{true};
    bool dither{true};
    int focus_interval{5};

    double getTotalExposureTime() const {
        double total = 0;
        for (const auto& t : targets) {
            total += t.getTotalExposureTime();
        }
        return total;
    }

    int getTotalExposures() const {
        int total = 0;
        for (const auto& t : targets) {
            total += t.getTotalExposures();
        }
        return total;
    }
};

} // namespace sequencer
} // namespace astro_mount

#endif
