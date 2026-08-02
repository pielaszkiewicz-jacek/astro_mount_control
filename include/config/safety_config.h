#ifndef SAFETY_CONFIG_H
#define SAFETY_CONFIG_H

#include <string>

namespace astro_mount {
namespace config {

/**
 * @brief Safety-related configuration — soft limits, meridian flip, park, refraction
 *
 * Contains all parameters that define the mount's safety behaviour,
 * including motion limits, automatic meridian flip logic, park position,
 * and atmospheric correction settings.
 */
struct SafetyConfig {
    // Soft limits
    bool soft_limits_enabled{true};
    double soft_limit_axis1_min{-270.0};
    double soft_limit_axis1_max{270.0};
    double soft_limit_axis2_min{-5.0};
    double soft_limit_axis2_max{185.0};
    double soft_limit_warning_degrees{10.0};       ///< Distance from hard limit to start warning [degrees]
    double soft_limit_deceleration_degrees{5.0};   ///< Distance from hard limit to start deceleration [degrees]
    double soft_limit_tracking_rate_factor{0.1};    ///< Minimum tracking rate factor at hard limit (0..1)
    
    // Meridian flip configuration
    bool meridian_flip_enabled{true};
    double meridian_flip_delay_minutes{5.0};
    double meridian_flip_hysteresis_degrees{0.5};
    double meridian_flip_timeout_seconds{120.0};  ///< Max time for flip slew before ERROR state
    
    // Park position (configurable, e.g. NCP for equatorial: HA=0°, Dec=90°)
    double park_position_axis1{0.0};   ///< Park target for axis 1 (HA) [degrees]
    double park_position_axis2{90.0};  ///< Park target for axis 2 (Dec) [degrees] — NCP default
    
    // Atmospheric refraction correction
    bool enable_refraction_correction{true};  ///< Apply real-time refraction correction in tracking loop
};

} // namespace config
} // namespace astro_mount

#endif // SAFETY_CONFIG_H
