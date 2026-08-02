#ifndef SEQUENCER_TARGET_H
#define SEQUENCER_TARGET_H

#include <string>
#include <vector>
#include "models/exposure_planner.h"

namespace astro_mount {
namespace sequencer {

struct Target {
    std::string name;
    std::string catalog_name;
    double ra{0.0};
    double dec{0.0};
    double magnitude{0.0};
    std::string type;

    // Exposure plan for this target
    models::ExposurePlan exposure_plan;

    double getTotalExposureTime() const {
        return exposure_plan.totalExposureTime();
    }

    int getTotalExposures() const {
        return exposure_plan.totalExposures();
    }
};

} // namespace sequencer
} // namespace astro_mount

#endif
