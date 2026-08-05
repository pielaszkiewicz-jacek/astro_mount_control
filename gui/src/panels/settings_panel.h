#ifndef SETTINGS_PANEL_H
#define SETTINGS_PANEL_H
#include <QWidget>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QComboBox>

namespace astro_mount { class Configuration; }
class GrpcClient;

namespace panels {

class SettingsPanel : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPanel(QWidget* parent = nullptr);
    void loadConfig(GrpcClient* client);
    void saveConfig(GrpcClient* client);

private:
    void setupUi();
    void populateFromConfig(const astro_mount::Configuration& cfg);
    void populateToConfig(astro_mount::Configuration& cfg);

    // ── Location ──
    QDoubleSpinBox *latitude_, *longitude_, *altitude_;

    // ── Telescope ──
    QDoubleSpinBox *focal_length_, *aperture_;

    // ── Environment defaults ──
    QDoubleSpinBox *default_temp_, *default_pressure_, *default_humidity_;

    // ── Mount Control ──
    QComboBox *mount_type_;
    QDoubleSpinBox *park_axis1_, *park_axis2_;
    QDoubleSpinBox *max_slew_rate_, *max_tracking_rate_;
    QDoubleSpinBox *slew_accel_, *tracking_accel_;
    QDoubleSpinBox *position_tolerance_, *rate_tolerance_;
    QCheckBox *invert_axis1_, *invert_axis2_;
    QCheckBox *equatorial_velocity_mode_;

    // ── Mount Orientation (CASUAL) ──
    QDoubleSpinBox *orient_qx_, *orient_qy_, *orient_qz_, *orient_qw_;

    // ── Encoders ──
    QCheckBox *use_encoders_, *encoders_absolute_;
    QDoubleSpinBox *encoder_resolution_;

    // ── TPOINT ──
    QSpinBox *tpoint_enabled_terms_;

    // ── Guider ──
    QCheckBox *enable_guider_;
    QDoubleSpinBox *guider_max_correction_, *guider_aggression_;

    // ── Kalman ──
    QDoubleSpinBox *process_noise_, *measurement_noise_;

    // ── Logging ──
    QComboBox *log_level_;
    QLineEdit *log_dir_;
    QCheckBox *log_console_;

    // ── Network ──
    QLineEdit *grpc_address_;
    QSpinBox *grpc_port_;
    QSpinBox *network_max_connections_;
    QCheckBox *enable_ssl_;
    QLineEdit *ssl_cert_, *ssl_key_;

    // ── CANopen ──
    QLineEdit *can_interface_;
    QSpinBox *can_node_id_, *can_baud_rate_;
    QCheckBox *can_enable_sync_;
    QSpinBox *can_sync_interval_;
    QComboBox *can_accel_mode_;
    QCheckBox *can_pdo_config_;
    QCheckBox *can_pos_rewind_enabled_;
    QDoubleSpinBox *can_pos_rewind_interval_, *can_pos_rewind_threshold_;

    // ── Tracking / Refraction ──
    QCheckBox *refraction_correction_;

    // ── Meridian Flip ──
    QCheckBox *meridian_flip_enabled_;
    QDoubleSpinBox *meridian_flip_delay_, *meridian_flip_hysteresis_, *meridian_flip_timeout_;

    // ── Soft Limits ──
    QCheckBox *soft_limits_enabled_;
    QDoubleSpinBox *soft_limit_a1_min_, *soft_limit_a1_max_;
    QDoubleSpinBox *soft_limit_a2_min_, *soft_limit_a2_max_;
    QDoubleSpinBox *soft_limit_warning_, *soft_limit_deceleration_;
    QDoubleSpinBox *soft_limit_rate_factor_;

    // ── Servo Init ──
    QCheckBox *servo_init_enabled_;
    QLineEdit *servo_init_sequence_;

    // ── Controller timing ──
    QSpinBox *controller_poll_ms_, *tracking_update_ms_;

    // ── Axis Physical Parameters (HA/Dec) ──
    // Gear ratios
    QDoubleSpinBox *ha_gear_ratio_, *dec_gear_ratio_;
    QDoubleSpinBox *ha_worm_ratio_, *dec_worm_ratio_;
    QSpinBox *ha_worm_teeth_, *dec_worm_teeth_;
    // Encoder params
    QDoubleSpinBox *ha_encoder_resolution_, *dec_encoder_resolution_;
    QDoubleSpinBox *ha_encoder_counts_per_arcsec_, *dec_encoder_counts_per_arcsec_;
    // Backlash
    QDoubleSpinBox *ha_backlash_, *dec_backlash_;
    // CANopen scaling
    QDoubleSpinBox *ha_pos_counts_per_deg_, *dec_pos_counts_per_deg_;
    QDoubleSpinBox *ha_vel_counts_per_degs_, *dec_vel_counts_per_degs_;

    // ── Buttons ──
    QPushButton *refresh_btn_, *save_btn_;
};

} // namespace panels
#endif
