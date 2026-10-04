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

    // ── Split tracking loop + PDO fast path ───────────────────────────────
    // See docs/pl/propozycja_podzialu_watku_sledzenia.md (§3–§10).
    int  calc_update_hz{50};         // computational thread frequency [Hz]
    int  io_update_hz{200};          // communication (command ticker) frequency [Hz]
    int  telemetry_decimation{5};    // io iterations between sensor reads
    int  safety_check_decimation{1}; // io iterations between safety-monitor reads
    bool split_tracking_loop{true};  // true: calc + io threads; false: single legacy loop
    bool io_realtime{true};          // promote io thread to SCHED_FIFO (best effort)
    int  io_realtime_priority{40};   // SCHED_FIFO priority for the io thread
    bool pdo_fast_path{true};        // use TPDO1 reads + RPDO2 writes on the hot path
    bool pdo_velocity_mapping{false};// map RPDO2 to 6040h + 60FFh (target velocity)
    bool sdo_slow_control{true};     // keep SDO for mode switch / config / homing
    
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
