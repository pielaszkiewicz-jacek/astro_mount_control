#include "panels/settings_panel.h"
#include "grpc_client.h"
#include "mount_controller.pb.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QLabel>
#include <QMessageBox>

namespace panels {

// ── Helpers ──────────────────────────────────────────────────────────────

static QGroupBox* makeCard(const QString& title, QWidget* parent) {
    auto* card = new QGroupBox(title, parent);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    return card;
}

static QWidget* makeCardBody(QWidget* parent) {
    auto* body = new QWidget(parent);
    body->setObjectName("cardBody");
    return body;
}

static QLabel* makeDesc(const QString& text, QWidget* parent) {
    auto* lbl = new QLabel(text, parent);
    lbl->setWordWrap(true);
    lbl->setStyleSheet(
        "color: #6c6c80; font-size: 11px; padding: 2px 0 6px 0; "
        "border-bottom: 1px solid rgba(255,255,255,0.04); margin-bottom: 2px;");
    return lbl;
}

static QDoubleSpinBox* makeDoubleSpin(QWidget* parent, double min, double max,
                                       double val, int decimals = 4,
                                       const QString& suffix = "") {
    auto* sb = new QDoubleSpinBox(parent);
    sb->setRange(min, max);
    sb->setValue(val);
    sb->setDecimals(decimals);
    if (!suffix.isEmpty()) sb->setSuffix(suffix);
    return sb;
}

SettingsPanel::SettingsPanel(QWidget* parent) : QWidget(parent) {
    setupUi();
}

void SettingsPanel::setupUi() {
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* scrollContent = new QWidget(scrollArea);
    auto* mainLayout = new QVBoxLayout(scrollContent);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    // ── Action buttons at top ─────────────────────────────────────────
    auto* btnRow = new QHBoxLayout();
    refresh_btn_ = new QPushButton("↻ Refresh from Server", scrollContent);
    refresh_btn_->setProperty("primary", true);
    save_btn_ = new QPushButton("💾 Save to Server", scrollContent);
    save_btn_->setProperty("warning", true);
    btnRow->addWidget(refresh_btn_);
    btnRow->addWidget(save_btn_);
    btnRow->addStretch();
    mainLayout->addLayout(btnRow);

    // ════════════════════════════════════════════════════════════════
    // 📍 Location — geographic position of the observatory
    // ════════════════════════════════════════════════════════════════
    {
        auto* card = makeCard("📍 Location", scrollContent);
        auto* body = makeCardBody(card);
        auto* lay = new QVBoxLayout(body);
        lay->addWidget(makeDesc("Geographic coordinates of the telescope. Used for coordinate transformations, "
                                "sidereal time calculation, and atmospheric refraction correction.", body));
        auto* form = new QFormLayout();
        latitude_ = makeDoubleSpin(body, -90, 90, 52.0, 6, "°");
        longitude_ = makeDoubleSpin(body, -180, 180, 21.0, 6, "°");
        altitude_ = makeDoubleSpin(body, -500, 9000, 100, 1, " m");
        form->addRow("Latitude:", latitude_);
        form->addRow("Longitude:", longitude_);
        form->addRow("Altitude:", altitude_);
        lay->addLayout(form);
        card->setLayout(new QVBoxLayout());
        card->layout()->setContentsMargins(0, 0, 0, 0);
        card->layout()->addWidget(body);
        mainLayout->addWidget(card);
    }

    // ════════════════════════════════════════════════════════════════
    // 🔭 Telescope — optical parameters
    // ════════════════════════════════════════════════════════════════
    {
        auto* card = makeCard("🔭 Telescope", scrollContent);
        auto* body = makeCardBody(card);
        auto* lay = new QVBoxLayout(body);
        lay->addWidget(makeDesc("Optical parameters of the telescope. Focal length and aperture affect "
                                "field of view calculations and plate solving.", body));
        auto* form = new QFormLayout();
        focal_length_ = makeDoubleSpin(body, 0, 10000, 1000, 1, " mm");
        aperture_ = makeDoubleSpin(body, 0, 1000, 200, 1, " mm");
        form->addRow("Focal Length:", focal_length_);
        form->addRow("Aperture:", aperture_);
        lay->addLayout(form);
        card->setLayout(new QVBoxLayout());
        card->layout()->setContentsMargins(0, 0, 0, 0);
        card->layout()->addWidget(body);
        mainLayout->addWidget(card);
    }

    // ════════════════════════════════════════════════════════════════
    // 🌡️ Environment Defaults
    // ════════════════════════════════════════════════════════════════
    {
        auto* card = makeCard("🌡️ Environment Defaults", scrollContent);
        auto* body = makeCardBody(card);
        auto* lay = new QVBoxLayout(body);
        lay->addWidget(makeDesc("Default atmospheric conditions used for refraction correction when "
                                "no live weather data is available.", body));
        auto* form = new QFormLayout();
        default_temp_ = makeDoubleSpin(body, -50, 60, 15, 1, " °C");
        default_pressure_ = makeDoubleSpin(body, 500, 1100, 1013, 1, " hPa");
        default_humidity_ = makeDoubleSpin(body, 0, 100, 50, 1, " %");
        form->addRow("Temperature:", default_temp_);
        form->addRow("Pressure:", default_pressure_);
        form->addRow("Humidity:", default_humidity_);
        lay->addLayout(form);
        card->setLayout(new QVBoxLayout());
        card->layout()->setContentsMargins(0, 0, 0, 0);
        card->layout()->addWidget(body);
        mainLayout->addWidget(card);
    }

    // ════════════════════════════════════════════════════════════════
    // 🎮 Mount Control — movement & geometry
    // ════════════════════════════════════════════════════════════════
    {
        auto* card = makeCard("🎮 Mount Control", scrollContent);
        auto* body = makeCardBody(card);
        auto* lay = new QVBoxLayout(body);
        lay->addWidget(makeDesc("Mount geometry type, parking targets, slew/tracking rate limits, "
                                "acceleration profiles, and axis inversion. Changing mount type may "
                                "require recalibration.", body));
        auto* form = new QFormLayout();

        mount_type_ = new QComboBox(body);
        mount_type_->addItem("Equatorial (0)", 0);
        mount_type_->addItem("Alt-Az (1)", 1);
        mount_type_->addItem("Casual (3)", 3);
        form->addRow("Mount Type:", mount_type_);

        park_axis1_ = makeDoubleSpin(body, -360, 360, 0, 2, "°");
        park_axis2_ = makeDoubleSpin(body, -360, 360, 0, 2, "°");
        form->addRow("Park Axis 1:", park_axis1_);
        form->addRow("Park Axis 2:", park_axis2_);

        max_slew_rate_ = makeDoubleSpin(body, 0.01, 100, 5, 2, " °/s");
        max_tracking_rate_ = makeDoubleSpin(body, 0.001, 10, 0.5, 3, " °/s");
        form->addRow("Max Slew Rate:", max_slew_rate_);
        form->addRow("Max Tracking Rate:", max_tracking_rate_);

        slew_accel_ = makeDoubleSpin(body, 0.1, 1000, 50, 1, " °/s²");
        tracking_accel_ = makeDoubleSpin(body, 0.01, 100, 5, 2, " °/s²");
        form->addRow("Slew Acceleration:", slew_accel_);
        form->addRow("Tracking Acceleration:", tracking_accel_);

        position_tolerance_ = makeDoubleSpin(body, 0.0001, 10, 0.01, 4, "°");
        rate_tolerance_ = makeDoubleSpin(body, 0.0001, 10, 0.001, 4, "°/s");
        form->addRow("Position Tolerance:", position_tolerance_);
        form->addRow("Rate Tolerance:", rate_tolerance_);

        invert_axis1_ = new QCheckBox("Invert Axis 1 rotation direction", body);
        invert_axis2_ = new QCheckBox("Invert Axis 2 rotation direction", body);
        form->addRow("", invert_axis1_);
        form->addRow("", invert_axis2_);

        equatorial_velocity_mode_ = new QCheckBox(
            "Equatorial Tracking Velocity Mode (experimental)", body);
        form->addRow("", equatorial_velocity_mode_);

        lay->addLayout(form);
        card->setLayout(new QVBoxLayout());
        card->layout()->setContentsMargins(0, 0, 0, 0);
        card->layout()->addWidget(body);
        mainLayout->addWidget(card);
    }

    // ════════════════════════════════════════════════════════════════
    // 🧭 Mount Orientation (CASUAL mount only)
    // ════════════════════════════════════════════════════════════════
    {
        auto* card = makeCard("🧭 Mount Orientation (CASUAL)", scrollContent);
        auto* body = makeCardBody(card);
        auto* lay = new QVBoxLayout(body);
        lay->addWidget(makeDesc("Quaternion orientation of the mount in the local horizontal frame. "
                                "Only used when mount_type = CASUAL. Identity quaternion = (0,0,0,1).", body));
        auto* form = new QFormLayout();
        orient_qx_ = makeDoubleSpin(body, -1, 1, 0, 6);
        orient_qy_ = makeDoubleSpin(body, -1, 1, 0, 6);
        orient_qz_ = makeDoubleSpin(body, -1, 1, 0, 6);
        orient_qw_ = makeDoubleSpin(body, -1, 1, 1, 6);
        form->addRow("QX:", orient_qx_);
        form->addRow("QY:", orient_qy_);
        form->addRow("QZ:", orient_qz_);
        form->addRow("QW:", orient_qw_);
        lay->addLayout(form);
        card->setLayout(new QVBoxLayout());
        card->layout()->setContentsMargins(0, 0, 0, 0);
        card->layout()->addWidget(body);
        mainLayout->addWidget(card);
    }

    // ════════════════════════════════════════════════════════════════
    // 📐 Encoders — position feedback
    // ════════════════════════════════════════════════════════════════
    {
        auto* card = makeCard("📐 Encoders", scrollContent);
        auto* body = makeCardBody(card);
        auto* lay = new QVBoxLayout(body);
        lay->addWidget(makeDesc("Shaft encoder configuration. When enabled, encoder feedback is used "
                                "to correct mount position. Absolute encoders retain position across "
                                "power cycles.", body));
        auto* form = new QFormLayout();
        use_encoders_ = new QCheckBox("Enable encoder feedback", body);
        encoders_absolute_ = new QCheckBox("Absolute encoders", body);
        encoder_resolution_ = makeDoubleSpin(body, 1, 1e9, 10000, 1, " counts/rev");
        form->addRow("", use_encoders_);
        form->addRow("", encoders_absolute_);
        form->addRow("Resolution:", encoder_resolution_);
        lay->addLayout(form);
        card->setLayout(new QVBoxLayout());
        card->layout()->setContentsMargins(0, 0, 0, 0);
        card->layout()->addWidget(body);
        mainLayout->addWidget(card);
    }

    // ════════════════════════════════════════════════════════════════
    // 🎯 TPOINT — pointing model
    // ════════════════════════════════════════════════════════════════
    {
        auto* card = makeCard("🎯 TPOINT Calibration", scrollContent);
        auto* body = makeCardBody(card);
        auto* lay = new QVBoxLayout(body);
        lay->addWidget(makeDesc("TPOINT pointing model configuration. The enabled terms bitmask "
                                "selects which correction terms (IH, ID, NP, CH, MA, ME, etc.) "
                                "are included in the pointing model.", body));
        auto* form = new QFormLayout();
        tpoint_enabled_terms_ = new QSpinBox(body);
        tpoint_enabled_terms_->setRange(0, 0xFFFFFFFF);
        tpoint_enabled_terms_->setValue(0);
        tpoint_enabled_terms_->setPrefix("0x");
        tpoint_enabled_terms_->setDisplayIntegerBase(16);
        form->addRow("Enabled Terms (hex):", tpoint_enabled_terms_);
        lay->addLayout(form);
        card->setLayout(new QVBoxLayout());
        card->layout()->setContentsMargins(0, 0, 0, 0);
        card->layout()->addWidget(body);
        mainLayout->addWidget(card);
    }

    // ════════════════════════════════════════════════════════════════
    // 🎯 Guider — autoguiding
    // ════════════════════════════════════════════════════════════════
    {
        auto* card = makeCard("🎯 Guider", scrollContent);
        auto* body = makeCardBody(card);
        auto* lay = new QVBoxLayout(body);
        lay->addWidget(makeDesc("Autoguider integration. When enabled, the mount accepts pulse-guide "
                                "corrections from an external guider (e.g. PHD2) via ST-4 or ASCOM.", body));
        auto* form = new QFormLayout();
        enable_guider_ = new QCheckBox("Enable guider", body);
        guider_max_correction_ = makeDoubleSpin(body, 0.01, 100, 5, 2, " arcsec");
        guider_aggression_ = makeDoubleSpin(body, 0.01, 1.0, 0.7, 2);
        form->addRow("", enable_guider_);
        form->addRow("Max Correction:", guider_max_correction_);
        form->addRow("Aggression:", guider_aggression_);
        lay->addLayout(form);
        card->setLayout(new QVBoxLayout());
        card->layout()->setContentsMargins(0, 0, 0, 0);
        card->layout()->addWidget(body);
        mainLayout->addWidget(card);
    }

    // ════════════════════════════════════════════════════════════════
    // 📊 Kalman Filter
    // ════════════════════════════════════════════════════════════════
    {
        auto* card = makeCard("📊 Kalman Filter", scrollContent);
        auto* body = makeCardBody(card);
        auto* lay = new QVBoxLayout(body);
        lay->addWidget(makeDesc("Kalman filter parameters for smoothing mount position estimates. "
                                "Higher process noise = more responsive to changes. Higher measurement "
                                "noise = more smoothing.", body));
        auto* form = new QFormLayout();
        process_noise_ = makeDoubleSpin(body, 0.0001, 100, 0.1, 4);
        measurement_noise_ = makeDoubleSpin(body, 0.0001, 100, 1.0, 4);
        form->addRow("Process Noise:", process_noise_);
        form->addRow("Measurement Noise:", measurement_noise_);
        lay->addLayout(form);
        card->setLayout(new QVBoxLayout());
        card->layout()->setContentsMargins(0, 0, 0, 0);
        card->layout()->addWidget(body);
        mainLayout->addWidget(card);
    }

    // ════════════════════════════════════════════════════════════════
    // 📝 Logging
    // ════════════════════════════════════════════════════════════════
    {
        auto* card = makeCard("📝 Logging", scrollContent);
        auto* body = makeCardBody(card);
        auto* lay = new QVBoxLayout(body);
        lay->addWidget(makeDesc("Server-side logging configuration. Log level controls verbosity "
                                "(trace=most verbose, critical=least). Directory specifies where "
                                "log files are written on the server.", body));
        auto* form = new QFormLayout();
        log_level_ = new QComboBox(body);
        log_level_->addItems({"trace", "debug", "info", "warning", "error", "critical"});
        log_level_->setCurrentIndex(2);
        log_dir_ = new QLineEdit(body);
        log_dir_->setText("/var/log/astro_mount");
        log_console_ = new QCheckBox("Also output to console", body);
        form->addRow("Level:", log_level_);
        form->addRow("Directory:", log_dir_);
        form->addRow("", log_console_);
        lay->addLayout(form);
        card->setLayout(new QVBoxLayout());
        card->layout()->setContentsMargins(0, 0, 0, 0);
        card->layout()->addWidget(body);
        mainLayout->addWidget(card);
    }

    // ════════════════════════════════════════════════════════════════
    // 🌐 Network — gRPC server
    // ════════════════════════════════════════════════════════════════
    {
        auto* card = makeCard("🌐 Network", scrollContent);
        auto* body = makeCardBody(card);
        auto* lay = new QVBoxLayout(body);
        lay->addWidget(makeDesc("gRPC server network settings. The server binds to grpc_address:grpc_port. "
                                "SSL/TLS can be enabled for encrypted communication. "
                                "max_connections limits concurrent client connections.", body));
        auto* form = new QFormLayout();
        grpc_address_ = new QLineEdit(body);
        grpc_address_->setText("0.0.0.0");
        grpc_port_ = new QSpinBox(body);
        grpc_port_->setRange(1, 65535);
        grpc_port_->setValue(50051);
        network_max_connections_ = new QSpinBox(body);
        network_max_connections_->setRange(1, 1000);
        network_max_connections_->setValue(10);
        enable_ssl_ = new QCheckBox("Enable SSL/TLS", body);
        ssl_cert_ = new QLineEdit(body);
        ssl_key_ = new QLineEdit(body);
        form->addRow("gRPC Address:", grpc_address_);
        form->addRow("gRPC Port:", grpc_port_);
        form->addRow("Max Connections:", network_max_connections_);
        form->addRow("", enable_ssl_);
        form->addRow("SSL Cert:", ssl_cert_);
        form->addRow("SSL Key:", ssl_key_);
        lay->addLayout(form);
        card->setLayout(new QVBoxLayout());
        card->layout()->setContentsMargins(0, 0, 0, 0);
        card->layout()->addWidget(body);
        mainLayout->addWidget(card);
    }

    // ════════════════════════════════════════════════════════════════
    // 🔌 CANopen — CiA 402 drive interface
    // ════════════════════════════════════════════════════════════════
    {
        auto* card = makeCard("🔌 CANopen", scrollContent);
        auto* body = makeCardBody(card);
        auto* lay = new QVBoxLayout(body);
        lay->addWidget(makeDesc("CANopen/CiA 402 drive interface configuration. The controller "
                                "communicates with motor drives over CAN bus. SYNC messages provide "
                                "deterministic timing. PDO config auto-writes mapping objects.", body));
        auto* form = new QFormLayout();
        can_interface_ = new QLineEdit(body);
        can_interface_->setText("can0");
        can_node_id_ = new QSpinBox(body);
        can_node_id_->setRange(1, 127);
        can_node_id_->setValue(1);
        can_baud_rate_ = new QSpinBox(body);
        can_baud_rate_->setRange(10000, 1000000);
        can_baud_rate_->setSingleStep(10000);
        can_baud_rate_->setValue(1000000);
        can_enable_sync_ = new QCheckBox("Enable SYNC messages", body);
        can_sync_interval_ = new QSpinBox(body);
        can_sync_interval_->setRange(1, 1000);
        can_sync_interval_->setValue(50);
        can_sync_interval_->setSuffix(" ms");
        can_accel_mode_ = new QComboBox(body);
        can_accel_mode_->addItems({"time", "rate"});
        can_accel_mode_->setToolTip(
            "time = 0x6083 is ramp time, rate = 0x6083 is acceleration rate");
        can_pdo_config_ = new QCheckBox("Auto-configure PDO mappings", body);
        can_pos_rewind_enabled_ = new QCheckBox("Enable position counter rewind", body);
        can_pos_rewind_interval_ = makeDoubleSpin(body, 0, 3600, 0, 1, " s");
        can_pos_rewind_threshold_ = makeDoubleSpin(body, 0, 100, 80, 1, " %");

        form->addRow("Interface:", can_interface_);
        form->addRow("Node ID:", can_node_id_);
        form->addRow("Baud Rate:", can_baud_rate_);
        form->addRow("", can_enable_sync_);
        form->addRow("SYNC Interval:", can_sync_interval_);
        form->addRow("Accel Mode:", can_accel_mode_);
        form->addRow("", can_pdo_config_);
        form->addRow("", can_pos_rewind_enabled_);
        form->addRow("Rewind Interval:", can_pos_rewind_interval_);
        form->addRow("Rewind Threshold:", can_pos_rewind_threshold_);
        lay->addLayout(form);
        card->setLayout(new QVBoxLayout());
        card->layout()->setContentsMargins(0, 0, 0, 0);
        card->layout()->addWidget(body);
        mainLayout->addWidget(card);
    }

    // ════════════════════════════════════════════════════════════════
    // 🎯 Tracking / Refraction
    // ════════════════════════════════════════════════════════════════
    {
        auto* card = makeCard("🎯 Tracking", scrollContent);
        auto* body = makeCardBody(card);
        auto* lay = new QVBoxLayout(body);
        lay->addWidget(makeDesc("Tracking behavior settings. When refraction correction is enabled, "
                                "the tracking loop applies real-time atmospheric refraction compensation "
                                "based on temperature, pressure, and altitude above horizon.", body));
        auto* form = new QFormLayout();
        refraction_correction_ = new QCheckBox("Apply real-time refraction correction", body);
        form->addRow("", refraction_correction_);
        lay->addLayout(form);
        card->setLayout(new QVBoxLayout());
        card->layout()->setContentsMargins(0, 0, 0, 0);
        card->layout()->addWidget(body);
        mainLayout->addWidget(card);
    }

    // ════════════════════════════════════════════════════════════════
    // 🔄 Meridian Flip
    // ════════════════════════════════════════════════════════════════
    {
        auto* card = makeCard("🔄 Meridian Flip", scrollContent);
        auto* body = makeCardBody(card);
        auto* lay = new QVBoxLayout(body);
        lay->addWidget(makeDesc("Automatic meridian flip for equatorial mounts. When the telescope "
                                "crosses the meridian, the mount flips to the other pier side to "
                                "avoid tracking limits. Applies to EQUATORIAL mount type only.", body));
        auto* form = new QFormLayout();
        meridian_flip_enabled_ = new QCheckBox("Enable automatic meridian flip", body);
        meridian_flip_delay_ = makeDoubleSpin(body, 0, 60, 5, 1, " min");
        meridian_flip_hysteresis_ = makeDoubleSpin(body, 0, 10, 1, 2, "°");
        meridian_flip_timeout_ = makeDoubleSpin(body, 1, 600, 120, 1, " s");
        form->addRow("", meridian_flip_enabled_);
        form->addRow("Delay after crossing:", meridian_flip_delay_);
        form->addRow("Hysteresis:", meridian_flip_hysteresis_);
        form->addRow("Slew Timeout:", meridian_flip_timeout_);
        lay->addLayout(form);
        card->setLayout(new QVBoxLayout());
        card->layout()->setContentsMargins(0, 0, 0, 0);
        card->layout()->addWidget(body);
        mainLayout->addWidget(card);
    }

    // ════════════════════════════════════════════════════════════════
    // ⛔ Soft Limits
    // ════════════════════════════════════════════════════════════════
    {
        auto* card = makeCard("⛔ Soft Limits", scrollContent);
        auto* body = makeCardBody(card);
        auto* lay = new QVBoxLayout(body);
        lay->addWidget(makeDesc("Software-imposed axis limits to prevent the telescope from hitting "
                                "physical stops. The warning zone triggers a UI alert. The deceleration "
                                "zone begins slowing the mount before the hard limit.", body));
        auto* form = new QFormLayout();
        soft_limits_enabled_ = new QCheckBox("Enable soft limits", body);
        soft_limit_a1_min_ = makeDoubleSpin(body, -360, 360, -180, 2, "°");
        soft_limit_a1_max_ = makeDoubleSpin(body, -360, 360, 180, 2, "°");
        soft_limit_a2_min_ = makeDoubleSpin(body, -90, 90, -90, 2, "°");
        soft_limit_a2_max_ = makeDoubleSpin(body, -90, 90, 90, 2, "°");
        soft_limit_warning_ = makeDoubleSpin(body, 0, 90, 5, 2, "°");
        soft_limit_deceleration_ = makeDoubleSpin(body, 0, 90, 2, 2, "°");
        soft_limit_rate_factor_ = makeDoubleSpin(body, 0, 1, 0.5, 2);

        form->addRow("", soft_limits_enabled_);
        form->addRow("Axis1 Min:", soft_limit_a1_min_);
        form->addRow("Axis1 Max:", soft_limit_a1_max_);
        form->addRow("Axis2 Min:", soft_limit_a2_min_);
        form->addRow("Axis2 Max:", soft_limit_a2_max_);
        form->addRow("Warning Zone:", soft_limit_warning_);
        form->addRow("Deceleration Zone:", soft_limit_deceleration_);
        form->addRow("Rate Factor at Limit:", soft_limit_rate_factor_);
        lay->addLayout(form);
        card->setLayout(new QVBoxLayout());
        card->layout()->setContentsMargins(0, 0, 0, 0);
        card->layout()->addWidget(body);
        mainLayout->addWidget(card);
    }

    // ════════════════════════════════════════════════════════════════
    // ⚙️ Servo Init — custom SDO sequence
    // ════════════════════════════════════════════════════════════════
    {
        auto* card = makeCard("⚙️ Servo Init", scrollContent);
        auto* body = makeCardBody(card);
        auto* lay = new QVBoxLayout(body);
        lay->addWidget(makeDesc("Custom CANopen SDO write sequence executed during servo initialization. "
                                "Format: JSON array of objects with {axis, index, subindex, value, "
                                "description} fields.", body));
        auto* form = new QFormLayout();
        servo_init_enabled_ = new QCheckBox("Enable custom SDO sequence on init", body);
        servo_init_sequence_ = new QLineEdit(body);
        servo_init_sequence_->setPlaceholderText(
            "[{\"axis\":0,\"index\":\"0x6060\",\"subindex\":0,\"value\":8,\"description\":\"CSP mode\"}]");
        form->addRow("", servo_init_enabled_);
        form->addRow("Sequence (JSON):", servo_init_sequence_);
        lay->addLayout(form);
        card->setLayout(new QVBoxLayout());
        card->layout()->setContentsMargins(0, 0, 0, 0);
        card->layout()->addWidget(body);
        mainLayout->addWidget(card);
    }

    // ════════════════════════════════════════════════════════════════
    // ⏱️ Controller Timing
    // ════════════════════════════════════════════════════════════════
    {
        auto* card = makeCard("⏱️ Controller Timing", scrollContent);
        auto* body = makeCardBody(card);
        auto* lay = new QVBoxLayout(body);
        lay->addWidget(makeDesc("Real-time loop timing parameters. Poll interval controls how often "
                                "the main control loop runs. Tracking update interval controls how "
                                "often target positions are recomputed.", body));
        auto* form = new QFormLayout();
        controller_poll_ms_ = new QSpinBox(body);
        controller_poll_ms_->setRange(10, 1000);
        controller_poll_ms_->setValue(50);
        controller_poll_ms_->setSuffix(" ms");
        tracking_update_ms_ = new QSpinBox(body);
        tracking_update_ms_->setRange(10, 1000);
        tracking_update_ms_->setValue(20);
        tracking_update_ms_->setSuffix(" ms");
        form->addRow("Main Loop Poll:", controller_poll_ms_);
        form->addRow("Tracking Update:", tracking_update_ms_);
        lay->addLayout(form);
        card->setLayout(new QVBoxLayout());
        card->layout()->setContentsMargins(0, 0, 0, 0);
        card->layout()->addWidget(body);
        mainLayout->addWidget(card);
    }

    // ════════════════════════════════════════════════════════════════
    // 🔧 HA Axis Physical Parameters
    // ════════════════════════════════════════════════════════════════
    {
        auto* card = makeCard("🔧 HA/RA Axis Physical Parameters", scrollContent);
        auto* body = makeCardBody(card);
        auto* lay = new QVBoxLayout(body);
        lay->addWidget(makeDesc("Physical parameters for the primary axis (HA for equatorial, "
                                "Altitude for alt-az). Gear ratios, encoder resolution, backlash "
                                "compensation, and CANopen scaling factors.", body));
        auto* form = new QFormLayout();
        ha_gear_ratio_ = makeDoubleSpin(body, 0.01, 1e6, 100, 2);
        ha_worm_ratio_ = makeDoubleSpin(body, 0, 10000, 0, 2);
        ha_worm_teeth_ = new QSpinBox(body);
        ha_worm_teeth_->setRange(0, 10000);
        ha_worm_teeth_->setValue(0);
        ha_encoder_resolution_ = makeDoubleSpin(body, 1, 1e9, 10000, 1, " counts/rev");
        ha_encoder_counts_per_arcsec_ = makeDoubleSpin(body, 0.0001, 1000, 1, 4, " cts/arcsec");
        ha_backlash_ = makeDoubleSpin(body, 0, 3600, 0, 2, " arcsec");
        ha_pos_counts_per_deg_ = makeDoubleSpin(body, 0.01, 1e9, 10000, 2, " cts/°");
        ha_vel_counts_per_degs_ = makeDoubleSpin(body, 0.01, 1e9, 100, 2, " cts/°/s");

        form->addRow("Gear Ratio:", ha_gear_ratio_);
        form->addRow("Worm Ratio:", ha_worm_ratio_);
        form->addRow("Worm Teeth:", ha_worm_teeth_);
        form->addRow("Encoder Resolution:", ha_encoder_resolution_);
        form->addRow("Encoder cts/arcsec:", ha_encoder_counts_per_arcsec_);
        form->addRow("Backlash:", ha_backlash_);
        form->addRow("CAN Pos cts/°:", ha_pos_counts_per_deg_);
        form->addRow("CAN Vel cts/°/s:", ha_vel_counts_per_degs_);
        lay->addLayout(form);
        card->setLayout(new QVBoxLayout());
        card->layout()->setContentsMargins(0, 0, 0, 0);
        card->layout()->addWidget(body);
        mainLayout->addWidget(card);
    }

    // ════════════════════════════════════════════════════════════════
    // 🔧 Dec Axis Physical Parameters
    // ════════════════════════════════════════════════════════════════
    {
        auto* card = makeCard("🔧 Dec/Alt Axis Physical Parameters", scrollContent);
        auto* body = makeCardBody(card);
        auto* lay = new QVBoxLayout(body);
        lay->addWidget(makeDesc("Physical parameters for the secondary axis (Dec for equatorial, "
                                "Azimuth for alt-az). Same parameters as the primary axis.", body));
        auto* form = new QFormLayout();
        dec_gear_ratio_ = makeDoubleSpin(body, 0.01, 1e6, 100, 2);
        dec_worm_ratio_ = makeDoubleSpin(body, 0, 10000, 0, 2);
        dec_worm_teeth_ = new QSpinBox(body);
        dec_worm_teeth_->setRange(0, 10000);
        dec_worm_teeth_->setValue(0);
        dec_encoder_resolution_ = makeDoubleSpin(body, 1, 1e9, 10000, 1, " counts/rev");
        dec_encoder_counts_per_arcsec_ = makeDoubleSpin(body, 0.0001, 1000, 1, 4, " cts/arcsec");
        dec_backlash_ = makeDoubleSpin(body, 0, 3600, 0, 2, " arcsec");
        dec_pos_counts_per_deg_ = makeDoubleSpin(body, 0.01, 1e9, 10000, 2, " cts/°");
        dec_vel_counts_per_degs_ = makeDoubleSpin(body, 0.01, 1e9, 100, 2, " cts/°/s");

        form->addRow("Gear Ratio:", dec_gear_ratio_);
        form->addRow("Worm Ratio:", dec_worm_ratio_);
        form->addRow("Worm Teeth:", dec_worm_teeth_);
        form->addRow("Encoder Resolution:", dec_encoder_resolution_);
        form->addRow("Encoder cts/arcsec:", dec_encoder_counts_per_arcsec_);
        form->addRow("Backlash:", dec_backlash_);
        form->addRow("CAN Pos cts/°:", dec_pos_counts_per_deg_);
        form->addRow("CAN Vel cts/°/s:", dec_vel_counts_per_degs_);
        lay->addLayout(form);
        card->setLayout(new QVBoxLayout());
        card->layout()->setContentsMargins(0, 0, 0, 0);
        card->layout()->addWidget(body);
        mainLayout->addWidget(card);
    }

    // ── Bottom save button ──────────────────────────────────────────
    auto* bottomBtnRow = new QHBoxLayout();
    auto* saveBtn2 = new QPushButton("💾 Save Configuration", scrollContent);
    saveBtn2->setProperty("primary", true);
    bottomBtnRow->addStretch();
    bottomBtnRow->addWidget(saveBtn2);
    mainLayout->addLayout(bottomBtnRow);

    mainLayout->addStretch();

    scrollArea->setWidget(scrollContent);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);

    // Button connections are handled externally by MainWindow
}

// ── Populate from proto ──────────────────────────────────────────────────

void SettingsPanel::populateFromConfig(const astro_mount::Configuration& cfg) {
    // Location
    latitude_->setValue(cfg.latitude());
    longitude_->setValue(cfg.longitude());
    altitude_->setValue(cfg.altitude());

    // Telescope
    focal_length_->setValue(cfg.focal_length());
    aperture_->setValue(cfg.aperture());

    // Environment
    default_temp_->setValue(cfg.default_temperature());
    default_pressure_->setValue(cfg.default_pressure());
    default_humidity_->setValue(cfg.default_humidity());

    // Mount type
    int mtIdx = mount_type_->findData(static_cast<int>(cfg.mount_type()));
    if (mtIdx >= 0) mount_type_->setCurrentIndex(mtIdx);

    // Mount Control
    park_axis1_->setValue(cfg.park_position_axis1());
    park_axis2_->setValue(cfg.park_position_axis2());
    max_slew_rate_->setValue(cfg.max_slew_rate());
    max_tracking_rate_->setValue(cfg.max_tracking_rate());
    slew_accel_->setValue(cfg.slew_acceleration());
    tracking_accel_->setValue(cfg.tracking_acceleration());
    position_tolerance_->setValue(cfg.position_tolerance());
    rate_tolerance_->setValue(cfg.rate_tolerance());
    invert_axis1_->setChecked(cfg.invert_axis1());
    invert_axis2_->setChecked(cfg.invert_axis2());
    equatorial_velocity_mode_->setChecked(cfg.equatorial_tracking_velocity_mode());

    // Mount Orientation (CASUAL)
    if (cfg.has_mount_orientation()) {
        orient_qx_->setValue(cfg.mount_orientation().qx());
        orient_qy_->setValue(cfg.mount_orientation().qy());
        orient_qz_->setValue(cfg.mount_orientation().qz());
        orient_qw_->setValue(cfg.mount_orientation().qw());
    }

    // Encoders
    use_encoders_->setChecked(cfg.use_encoders());
    encoders_absolute_->setChecked(cfg.encoders_absolute());
    encoder_resolution_->setValue(cfg.encoder_resolution_config());

    // TPOINT
    tpoint_enabled_terms_->setValue(static_cast<int>(cfg.tpoint_enabled_terms()));

    // Guider
    enable_guider_->setChecked(cfg.enable_guider());
    guider_max_correction_->setValue(cfg.guider_max_correction());
    guider_aggression_->setValue(cfg.guider_aggression());

    // Kalman
    process_noise_->setValue(cfg.process_noise());
    measurement_noise_->setValue(cfg.measurement_noise());

    // Logging
    int llIdx = log_level_->findText(QString::fromStdString(cfg.log_level()));
    if (llIdx >= 0) log_level_->setCurrentIndex(llIdx);
    log_dir_->setText(QString::fromStdString(cfg.log_directory()));
    log_console_->setChecked(cfg.log_console_output());

    // Network
    grpc_address_->setText(QString::fromStdString(cfg.grpc_address()));
    grpc_port_->setValue(cfg.grpc_port());
    network_max_connections_->setValue(cfg.network_max_connections());
    enable_ssl_->setChecked(cfg.network_enable_ssl());
    ssl_cert_->setText(QString::fromStdString(cfg.network_ssl_cert_path()));
    ssl_key_->setText(QString::fromStdString(cfg.network_ssl_key_path()));

    // CANopen
    can_interface_->setText(QString::fromStdString(cfg.canopen_interface()));
    can_node_id_->setValue(cfg.canopen_node_id());
    can_baud_rate_->setValue(cfg.canopen_baud_rate());
    can_enable_sync_->setChecked(cfg.canopen_enable_sync());
    can_sync_interval_->setValue(cfg.canopen_sync_interval_ms());
    int amIdx = can_accel_mode_->findText(QString::fromStdString(cfg.canopen_accel_mode()));
    if (amIdx >= 0) can_accel_mode_->setCurrentIndex(amIdx);
    can_pdo_config_->setChecked(cfg.canopen_pdo_config_enabled());
    can_pos_rewind_enabled_->setChecked(cfg.canopen_position_rewind_enabled());
    can_pos_rewind_interval_->setValue(cfg.canopen_position_rewind_interval_seconds());
    can_pos_rewind_threshold_->setValue(cfg.canopen_position_rewind_threshold_percent());

    // Tracking
    refraction_correction_->setChecked(cfg.enable_refraction_correction());

    // Meridian Flip
    meridian_flip_enabled_->setChecked(cfg.meridian_flip_enabled());
    meridian_flip_delay_->setValue(cfg.meridian_flip_delay_minutes());
    meridian_flip_hysteresis_->setValue(cfg.meridian_flip_hysteresis_degrees());
    meridian_flip_timeout_->setValue(cfg.meridian_flip_timeout_seconds());

    // Soft Limits
    soft_limits_enabled_->setChecked(cfg.soft_limits_enabled());
    soft_limit_a1_min_->setValue(cfg.soft_limit_axis1_min());
    soft_limit_a1_max_->setValue(cfg.soft_limit_axis1_max());
    soft_limit_a2_min_->setValue(cfg.soft_limit_axis2_min());
    soft_limit_a2_max_->setValue(cfg.soft_limit_axis2_max());
    soft_limit_warning_->setValue(cfg.soft_limit_warning_degrees());
    soft_limit_deceleration_->setValue(cfg.soft_limit_deceleration_degrees());
    soft_limit_rate_factor_->setValue(cfg.soft_limit_tracking_rate_factor());

    // Servo Init
    servo_init_enabled_->setChecked(cfg.servo_init_enabled());
    servo_init_sequence_->setText(QString::fromStdString(cfg.servo_init_sequence()));

    // Controller Timing
    controller_poll_ms_->setValue(cfg.controller_poll_ms());
    tracking_update_ms_->setValue(cfg.tracking_update_ms());

    // HA Axis Physical Params
    if (cfg.has_ha_axis_params()) {
        const auto& ha = cfg.ha_axis_params();
        ha_gear_ratio_->setValue(ha.gear_ratio());
        ha_worm_ratio_->setValue(ha.worm_ratio());
        ha_worm_teeth_->setValue(ha.worm_teeth());
        ha_encoder_resolution_->setValue(ha.encoder_resolution());
        ha_encoder_counts_per_arcsec_->setValue(ha.encoder_counts_per_arcsec());
        ha_backlash_->setValue(ha.backlash());
        ha_pos_counts_per_deg_->setValue(ha.position_counts_per_degree());
        ha_vel_counts_per_degs_->setValue(ha.velocity_counts_per_deg_s());
    }

    // Dec Axis Physical Params
    if (cfg.has_dec_axis_params()) {
        const auto& dec = cfg.dec_axis_params();
        dec_gear_ratio_->setValue(dec.gear_ratio());
        dec_worm_ratio_->setValue(dec.worm_ratio());
        dec_worm_teeth_->setValue(dec.worm_teeth());
        dec_encoder_resolution_->setValue(dec.encoder_resolution());
        dec_encoder_counts_per_arcsec_->setValue(dec.encoder_counts_per_arcsec());
        dec_backlash_->setValue(dec.backlash());
        dec_pos_counts_per_deg_->setValue(dec.position_counts_per_degree());
        dec_vel_counts_per_degs_->setValue(dec.velocity_counts_per_deg_s());
    }
}

// ── Populate to proto ────────────────────────────────────────────────────

void SettingsPanel::populateToConfig(astro_mount::Configuration& cfg) {
    cfg.set_latitude(latitude_->value());
    cfg.set_longitude(longitude_->value());
    cfg.set_altitude(altitude_->value());

    cfg.set_focal_length(focal_length_->value());
    cfg.set_aperture(aperture_->value());

    cfg.set_default_temperature(default_temp_->value());
    cfg.set_default_pressure(default_pressure_->value());
    cfg.set_default_humidity(default_humidity_->value());

    cfg.set_mount_type(static_cast<astro_mount::MountType>(
        mount_type_->currentData().toInt()));

    cfg.set_park_position_axis1(park_axis1_->value());
    cfg.set_park_position_axis2(park_axis2_->value());
    cfg.set_max_slew_rate(max_slew_rate_->value());
    cfg.set_max_tracking_rate(max_tracking_rate_->value());
    cfg.set_slew_acceleration(slew_accel_->value());
    cfg.set_tracking_acceleration(tracking_accel_->value());
    cfg.set_position_tolerance(position_tolerance_->value());
    cfg.set_rate_tolerance(rate_tolerance_->value());
    cfg.set_invert_axis1(invert_axis1_->isChecked());
    cfg.set_invert_axis2(invert_axis2_->isChecked());
    cfg.set_equatorial_tracking_velocity_mode(equatorial_velocity_mode_->isChecked());

    // Mount Orientation
    auto* orient = cfg.mutable_mount_orientation();
    orient->set_qx(orient_qx_->value());
    orient->set_qy(orient_qy_->value());
    orient->set_qz(orient_qz_->value());
    orient->set_qw(orient_qw_->value());

    cfg.set_use_encoders(use_encoders_->isChecked());
    cfg.set_encoders_absolute(encoders_absolute_->isChecked());
    cfg.set_encoder_resolution_config(encoder_resolution_->value());

    cfg.set_tpoint_enabled_terms(static_cast<uint32_t>(tpoint_enabled_terms_->value()));

    cfg.set_enable_guider(enable_guider_->isChecked());
    cfg.set_guider_max_correction(guider_max_correction_->value());
    cfg.set_guider_aggression(guider_aggression_->value());

    cfg.set_process_noise(process_noise_->value());
    cfg.set_measurement_noise(measurement_noise_->value());

    cfg.set_log_level(log_level_->currentText().toStdString());
    cfg.set_log_directory(log_dir_->text().toStdString());
    cfg.set_log_console_output(log_console_->isChecked());

    cfg.set_grpc_address(grpc_address_->text().toStdString());
    cfg.set_grpc_port(grpc_port_->value());
    cfg.set_network_max_connections(network_max_connections_->value());
    cfg.set_network_enable_ssl(enable_ssl_->isChecked());
    cfg.set_network_ssl_cert_path(ssl_cert_->text().toStdString());
    cfg.set_network_ssl_key_path(ssl_key_->text().toStdString());

    cfg.set_canopen_interface(can_interface_->text().toStdString());
    cfg.set_canopen_node_id(can_node_id_->value());
    cfg.set_canopen_baud_rate(can_baud_rate_->value());
    cfg.set_canopen_enable_sync(can_enable_sync_->isChecked());
    cfg.set_canopen_sync_interval_ms(can_sync_interval_->value());
    cfg.set_canopen_accel_mode(can_accel_mode_->currentText().toStdString());
    cfg.set_canopen_pdo_config_enabled(can_pdo_config_->isChecked());
    cfg.set_canopen_position_rewind_enabled(can_pos_rewind_enabled_->isChecked());
    cfg.set_canopen_position_rewind_interval_seconds(can_pos_rewind_interval_->value());
    cfg.set_canopen_position_rewind_threshold_percent(can_pos_rewind_threshold_->value());

    cfg.set_enable_refraction_correction(refraction_correction_->isChecked());

    cfg.set_meridian_flip_enabled(meridian_flip_enabled_->isChecked());
    cfg.set_meridian_flip_delay_minutes(meridian_flip_delay_->value());
    cfg.set_meridian_flip_hysteresis_degrees(meridian_flip_hysteresis_->value());
    cfg.set_meridian_flip_timeout_seconds(meridian_flip_timeout_->value());

    cfg.set_soft_limits_enabled(soft_limits_enabled_->isChecked());
    cfg.set_soft_limit_axis1_min(soft_limit_a1_min_->value());
    cfg.set_soft_limit_axis1_max(soft_limit_a1_max_->value());
    cfg.set_soft_limit_axis2_min(soft_limit_a2_min_->value());
    cfg.set_soft_limit_axis2_max(soft_limit_a2_max_->value());
    cfg.set_soft_limit_warning_degrees(soft_limit_warning_->value());
    cfg.set_soft_limit_deceleration_degrees(soft_limit_deceleration_->value());
    cfg.set_soft_limit_tracking_rate_factor(soft_limit_rate_factor_->value());

    cfg.set_servo_init_enabled(servo_init_enabled_->isChecked());
    cfg.set_servo_init_sequence(servo_init_sequence_->text().toStdString());

    cfg.set_controller_poll_ms(controller_poll_ms_->value());
    cfg.set_tracking_update_ms(tracking_update_ms_->value());

    // HA Axis Physical Params
    auto* ha = cfg.mutable_ha_axis_params();
    ha->set_gear_ratio(ha_gear_ratio_->value());
    ha->set_worm_ratio(ha_worm_ratio_->value());
    ha->set_worm_teeth(ha_worm_teeth_->value());
    ha->set_encoder_resolution(ha_encoder_resolution_->value());
    ha->set_encoder_counts_per_arcsec(ha_encoder_counts_per_arcsec_->value());
    ha->set_backlash(ha_backlash_->value());
    ha->set_position_counts_per_degree(ha_pos_counts_per_deg_->value());
    ha->set_velocity_counts_per_deg_s(ha_vel_counts_per_degs_->value());

    // Dec Axis Physical Params
    auto* dec = cfg.mutable_dec_axis_params();
    dec->set_gear_ratio(dec_gear_ratio_->value());
    dec->set_worm_ratio(dec_worm_ratio_->value());
    dec->set_worm_teeth(dec_worm_teeth_->value());
    dec->set_encoder_resolution(dec_encoder_resolution_->value());
    dec->set_encoder_counts_per_arcsec(dec_encoder_counts_per_arcsec_->value());
    dec->set_backlash(dec_backlash_->value());
    dec->set_position_counts_per_degree(dec_pos_counts_per_deg_->value());
    dec->set_velocity_counts_per_deg_s(dec_vel_counts_per_degs_->value());
}

void SettingsPanel::loadConfig(GrpcClient* client) {
    try {
        auto cfg = client->getConfig();
        populateFromConfig(cfg);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Load Error",
            QString("Failed to load configuration:\n%1").arg(e.what()));
    }
}

void SettingsPanel::saveConfig(GrpcClient* client) {
    try {
        astro_mount::Configuration cfg;
        populateToConfig(cfg);
        if (client->updateConfig(cfg)) {
            QMessageBox::information(this, "Success",
                "Configuration saved successfully.");
        } else {
            QMessageBox::warning(this, "Save Error",
                "Server rejected the configuration update.");
        }
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Save Error",
            QString("Failed to save configuration:\n%1").arg(e.what()));
    }
}

} // namespace panels
