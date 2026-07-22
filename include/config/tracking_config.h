#ifndef TRACKING_CONFIG_H
#define TRACKING_CONFIG_H

#include <string>

namespace astro_mount {
namespace config {

/**
 * @brief Tracking mode enumeration
 */
enum class TrackingMode {
    SIDEREAL,
    SOLAR,
    LUNAR,
    CUSTOM,
    OFF
};

/**
 * @brief Tracking-specific configuration — loop timing, guider, field rotation
 *
 * Contains only parameters related to the tracking loop, autoguider
 * integration, and field rotation compensation.
 */
struct TrackingConfig {
    // Loop timing
    int controller_poll_ms{50};     // Main loop poll interval (ms), default 20Hz
    int tracking_update_ms{20};     // Tracking update interval (ms), default 50Hz
    
    // Guider configuration
    bool enable_guider{false};
    double guider_max_correction{5.0};
    double guider_aggression{0.8};
    
    // Field rotation parameters
    bool field_rotation_enabled{false};
    double field_rotation_latitude{52.0};
    double field_rotation_altitude{0.0};
    double field_rotation_azimuth{0.0};
    double field_rotation_computed_rate{0.0};
    double field_rotation_applied_correction{0.0};
    double field_rotation_temperature{15.0};
    double field_rotation_flexure_correction{0.0};
};

} // namespace config
} // namespace astro_mount

#endif // TRACKING_CONFIG_H
