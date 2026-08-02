#ifndef CALIBRATION_CONFIG_H
#define CALIBRATION_CONFIG_H

#include <cstdint>

namespace astro_mount {
namespace config {

/**
 * @brief Bootstrap calibration mode
 *
 * Determines how bootstrap measurements are collected:
 * - MANUAL: User manually points at stars (gamepad), adds measurements
 * - HYBRID: First 3 measurements manual, then automatic slews
 * - AUTOMATIC: Fully automatic with plate solver orchestrator
 */
enum class BootstrapMode {
    BOOTSTRAP_MANUAL = 0,       ///< Fully manual pointing
    BOOTSTRAP_HYBRID = 1,       ///< Manual + automatic after 3 measurements
    BOOTSTRAP_AUTOMATIC = 2     ///< Fully automatic with plate solver
};

/**
 * @brief Calibration-specific configuration — TPOINT, bootstrap
 *
 * Contains parameters for the mount's calibration subsystems:
 * TPOINT telescope pointing model and bootstrap initial alignment.
 */
struct CalibrationConfig {
    // TPOINT parameters
    uint32_t tpoint_enabled_terms{0};   ///< Bitmask of enabled TPOINT correction terms
    
    // Bootstrap mode
    BootstrapMode bootstrap_mode{BootstrapMode::BOOTSTRAP_MANUAL};
};

} // namespace config
} // namespace astro_mount

#endif // CALIBRATION_CONFIG_H
