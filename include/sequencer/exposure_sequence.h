#ifndef SEQUENCER_EXPOSURE_H
#define SEQUENCER_EXPOSURE_H

#include <string>
#include <vector>
#include <cstdint>

namespace astro_mount {
namespace sequencer {

struct ExposureStep {
    int32_t step_index{0};
    double exposure_time_s{60.0};
    int32_t gain{0};
    int32_t binning{1};
    int32_t filter_position{0};
    std::string filter_name;
    std::string image_type{"lights"};
    bool completed{false};
    std::string saved_path;
    double hfd{0.0};
};

} // namespace sequencer
} // namespace astro_mount

#endif
