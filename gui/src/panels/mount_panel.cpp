#include "panels/mount_panel.h"
#include "grpc_client.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QScrollArea>

namespace panels {

// Helper: create a card-like QGroupBox
static QGroupBox* makeCard(const QString& title, QWidget* parent) {
    auto* card = new QGroupBox(title, parent);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    return card;
}

// Helper: create a card body widget
static QWidget* makeCardBody(QWidget* parent) {
    auto* body = new QWidget(parent);
    body->setObjectName("cardBody");
    return body;
}

// Helper: status badge label
static QLabel* makeBadge(const QString& text, QWidget* parent) {
    auto* badge = new QLabel(text, parent);
    badge->setObjectName("statusBadge");
    return badge;
}

// Helper: short description label under card title
static QLabel* makeDesc(const QString& text, QWidget* parent) {
    auto* lbl = new QLabel(text, parent);
    lbl->setWordWrap(true);
    lbl->setStyleSheet(
        "color: #6c6c80; font-size: 11px; padding: 2px 0 6px 0; "
        "border-bottom: 1px solid rgba(255,255,255,0.04); margin-bottom: 2px;");
    return lbl;
}

MountPanel::MountPanel(QWidget* parent) : QWidget(parent) {
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* scrollContent = new QWidget(scrollArea);
    auto* mainLayout = new QVBoxLayout(scrollContent);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    // ════════════════════════════════════════════════════════════════
    // 1. Slew to Coordinates Card
    // ════════════════════════════════════════════════════════════════
    auto* slewCard = makeCard("Slew to Coordinates", scrollContent);
    auto* slewBody = makeCardBody(slewCard);
    auto* slewLayout = new QVBoxLayout(slewBody);
    slewLayout->setSpacing(10);
    slewLayout->addWidget(makeDesc("Enter target Right Ascension (hours) and Declination (degrees). "
                                   "Use Slew & Track to automatically begin tracking after arrival.", slewBody));

    auto* raLayout = new QFormLayout();
    ra_input_ = new QLineEdit(slewBody);
    ra_input_->setPlaceholderText("e.g. 10h 30m 00s");
    raLayout->addRow("Right Ascension (hours):", ra_input_);
    slewLayout->addLayout(raLayout);

    auto* decLayout = new QFormLayout();
    dec_input_ = new QLineEdit(slewBody);
    dec_input_->setPlaceholderText("e.g. +41° 16' 12.3\"");
    decLayout->addRow("Declination (degrees):", dec_input_);
    slewLayout->addLayout(decLayout);

    auto* slewBtnRow = new QHBoxLayout();
    slew_btn_ = new QPushButton("Slew", slewBody);
    slew_btn_->setProperty("primary", true);
    slew_track_btn_ = new QPushButton("Slew && Track", slewBody);
    slew_track_btn_->setProperty("warning", true);
    slewBtnRow->addWidget(slew_btn_);
    slewBtnRow->addWidget(slew_track_btn_);
    slewLayout->addLayout(slewBtnRow);

    slewCard->setLayout(new QVBoxLayout());
    slewCard->layout()->setContentsMargins(0, 0, 0, 0);
    slewCard->layout()->addWidget(slewBody);
    mainLayout->addWidget(slewCard);

    // ════════════════════════════════════════════════════════════════
    // 2. Axis Control Card (directional pad)
    // ════════════════════════════════════════════════════════════════
    auto* axisCard = makeCard("Axis Control", scrollContent);
    auto* axisBody = makeCardBody(axisCard);
    auto* axisLayout = new QVBoxLayout(axisBody);
    axisLayout->setSpacing(12);
    axisLayout->addWidget(makeDesc("Direct axis control pad for manual mount movement. Use velocity mode "
                                   "for continuous motion or step mode for incremental positioning.", axisBody));

    // Axis mode info
    axis_mode_label_ = new QLabel("Uncalibrated — using direct axis velocity control", axisBody);
    axis_mode_label_->setAlignment(Qt::AlignCenter);
    axis_mode_label_->setStyleSheet("color: #a0a0b0; font-size: 11px; padding: 4px; border-bottom: 1px solid #2a2a4a;");
    axisLayout->addWidget(axis_mode_label_);

    // Directional Pad (3x3 grid matching web interface)
    auto* padWidget = new QWidget(axisBody);
    auto* padGrid = new QGridLayout(padWidget);
    padGrid->setSpacing(6);
    padGrid->setContentsMargins(0, 0, 0, 0);

    btn_axis_up_ = new QPushButton("▲", padWidget);
    btn_axis_up_->setProperty("class", "axis-btn");
    btn_axis_up_->setFixedSize(96, 96);

    btn_axis_down_ = new QPushButton("▼", padWidget);
    btn_axis_down_->setProperty("class", "axis-btn");
    btn_axis_down_->setFixedSize(96, 96);

    btn_axis_left_ = new QPushButton("◄", padWidget);
    btn_axis_left_->setProperty("class", "axis-btn");
    btn_axis_left_->setFixedSize(96, 96);

    btn_axis_right_ = new QPushButton("►", padWidget);
    btn_axis_right_->setProperty("class", "axis-btn");
    btn_axis_right_->setFixedSize(96, 96);

    // Center indicator
    auto* padCenter = new QLabel(padWidget);
    padCenter->setFixedSize(96, 96);
    padCenter->setAlignment(Qt::AlignCenter);
    padCenter->setStyleSheet(
        "background: #0f0f1a; border: 1px dashed #2a2a4a; border-radius: 6px;"
    );
    padCenter->setText("◉");

    padGrid->addWidget(btn_axis_up_,    0, 1, Qt::AlignCenter);
    padGrid->addWidget(btn_axis_left_,  1, 0, Qt::AlignCenter);
    padGrid->addWidget(padCenter,       1, 1, Qt::AlignCenter);
    padGrid->addWidget(btn_axis_right_, 1, 2, Qt::AlignCenter);
    padGrid->addWidget(btn_axis_down_,  2, 1, Qt::AlignCenter);

    // Center the pad grid
    auto* padContainer = new QHBoxLayout();
    padContainer->addStretch();
    padContainer->addWidget(padWidget);
    padContainer->addStretch();
    axisLayout->addLayout(padContainer);

    // Axis mode selector
    auto* modeRow = new QHBoxLayout();
    modeRow->addWidget(new QLabel("Mode:", axisBody));
    axis_mode_toggle_ = new QPushButton("Velocity", axisBody);
    axis_mode_toggle_->setProperty("class", "btn-secondary");
    modeRow->addWidget(axis_mode_toggle_);
    modeRow->addWidget(new QLabel("Step:", axisBody));
    axis_step_size_ = new QDoubleSpinBox(axisBody);
    axis_step_size_->setRange(0.01, 90.0);
    axis_step_size_->setValue(1.0);
    axis_step_size_->setSuffix("°");
    axis_step_size_->setFixedWidth(90);
    modeRow->addWidget(axis_step_size_);
    modeRow->addStretch();
    axisLayout->addLayout(modeRow);

    // Telescope axis speed reference toggle
    axis_speed_ref_toggle_ = new QCheckBox("Telescope axis (°/s)", axisBody);
    axisLayout->addWidget(axis_speed_ref_toggle_);

    // Speed slider
    auto* speedRow = new QVBoxLayout();
    auto* speedHeader = new QHBoxLayout();
    speed_label_ = new QLabel("Speed: 1.0 °/s", axisBody);
    speed_label_->setAlignment(Qt::AlignCenter);
    speedHeader->addStretch();
    speedHeader->addWidget(speed_label_);
    speedHeader->addStretch();
    speedRow->addLayout(speedHeader);

    speed_slider_ = new QSlider(Qt::Horizontal, axisBody);
    speed_slider_->setRange(1, 500); // 0.01 to 5.0 mapped as int*100
    speed_slider_->setValue(100);
    speedRow->addWidget(speed_slider_);

    auto* speedLabelsRow = new QHBoxLayout();
    speedLabelsRow->addWidget(new QLabel("Slow", axisBody));
    speedLabelsRow->addStretch();
    speedLabelsRow->addWidget(new QLabel("Fast", axisBody));
    speedRow->addLayout(speedLabelsRow);
    axisLayout->addLayout(speedRow);

    // Acceleration slider
    auto* accelRow = new QVBoxLayout();
    accel_label_ = new QLabel("Acceleration: 50 °/s²", axisBody);
    accel_label_->setAlignment(Qt::AlignCenter);
    accelRow->addWidget(accel_label_);

    accel_slider_ = new QSlider(Qt::Horizontal, axisBody);
    accel_slider_->setRange(1, 5000); // 0.1 to 500 mapped as int*10
    accel_slider_->setValue(500);
    accelRow->addWidget(accel_slider_);

    auto* accelLabelsRow = new QHBoxLayout();
    accelLabelsRow->addWidget(new QLabel("Gentle", axisBody));
    accelLabelsRow->addStretch();
    accelLabelsRow->addWidget(new QLabel("Aggressive", axisBody));
    accelRow->addLayout(accelLabelsRow);
    axisLayout->addLayout(accelRow);

    // Deceleration slider
    auto* decelRow = new QVBoxLayout();
    decel_label_ = new QLabel("Deceleration: 50 °/s²", axisBody);
    decel_label_->setAlignment(Qt::AlignCenter);
    decelRow->addWidget(decel_label_);

    decel_slider_ = new QSlider(Qt::Horizontal, axisBody);
    decel_slider_->setRange(1, 5000); // 0.1 to 500 mapped as int*10
    decel_slider_->setValue(500);
    decelRow->addWidget(decel_slider_);

    auto* decelLabelsRow = new QHBoxLayout();
    decelLabelsRow->addWidget(new QLabel("Gentle", axisBody));
    decelLabelsRow->addStretch();
    decelLabelsRow->addWidget(new QLabel("Aggressive", axisBody));
    decelRow->addLayout(decelLabelsRow);
    axisLayout->addLayout(decelRow);

    // Emergency stop
    btn_emergency_stop_ = new QPushButton("EMERGENCY STOP", axisBody);
    btn_emergency_stop_->setObjectName("emergencyStop");
    axisLayout->addWidget(btn_emergency_stop_);

    // Connect slider value changes to label updates
    connect(speed_slider_, &QSlider::valueChanged, this, [this](int val) {
        speed_label_->setText(QString("Speed: %1 °/s").arg(val / 100.0, 0, 'f', 2));
    });
    connect(accel_slider_, &QSlider::valueChanged, this, [this](int val) {
        accel_label_->setText(QString("Acceleration: %1 °/s²").arg(val / 10.0, 0, 'f', 1));
    });
    connect(decel_slider_, &QSlider::valueChanged, this, [this](int val) {
        decel_label_->setText(QString("Deceleration: %1 °/s²").arg(val / 10.0, 0, 'f', 1));
    });

    axisCard->setLayout(new QVBoxLayout());
    axisCard->layout()->setContentsMargins(0, 0, 0, 0);
    axisCard->layout()->addWidget(axisBody);
    mainLayout->addWidget(axisCard);

    // ════════════════════════════════════════════════════════════════
    // 3. Home Card
    // ════════════════════════════════════════════════════════════════
    auto* homeCard = makeCard("🏠 Home", scrollContent);
    auto* homeBody = makeCardBody(homeCard);
    auto* homeLayout = new QVBoxLayout(homeBody);
    homeLayout->setSpacing(10);
    homeLayout->addWidget(makeDesc("Set the internal coordinate reference. Physically point the "
                                   "telescope at a known object first, then click Home.", homeBody));

    home_btn_ = new QPushButton("🏠 Home", homeBody);
    home_btn_->setProperty("primary", true);
    homeLayout->addWidget(home_btn_);

    homeCard->setLayout(new QVBoxLayout());
    homeCard->layout()->setContentsMargins(0, 0, 0, 0);
    homeCard->layout()->addWidget(homeBody);
    mainLayout->addWidget(homeCard);

    // ════════════════════════════════════════════════════════════════
    // 4. Quick Actions Card
    // ════════════════════════════════════════════════════════════════
    auto* actionsCard = makeCard("Quick Actions", scrollContent);
    auto* actionsBody = makeCardBody(actionsCard);
    auto* actionsWrap = new QVBoxLayout(actionsBody);
    actionsWrap->setSpacing(6);
    actionsWrap->addWidget(makeDesc("Frequently used mount operations for quick access.", actionsBody));

    auto* actionsGrid = new QGridLayout();
    actionsGrid->setSpacing(8);

    stop_btn_ = new QPushButton("Stop", actionsBody);
    stop_btn_->setProperty("danger", true);
    park_btn_ = new QPushButton("Park", actionsBody);
    park_btn_->setProperty("warning", true);
    unpark_btn_ = new QPushButton("Unpark", actionsBody);
    clear_errors_btn_ = new QPushButton("Clear Errors", actionsBody);

    actionsGrid->addWidget(stop_btn_, 0, 0);
    actionsGrid->addWidget(park_btn_, 0, 1);
    actionsGrid->addWidget(unpark_btn_, 1, 0);
    actionsGrid->addWidget(clear_errors_btn_, 1, 1);
    actionsWrap->addLayout(actionsGrid);

    actionsCard->setLayout(new QVBoxLayout());
    actionsCard->layout()->setContentsMargins(0, 0, 0, 0);
    actionsCard->layout()->addWidget(actionsBody);
    mainLayout->addWidget(actionsCard);

    // ════════════════════════════════════════════════════════════════
    // 5. Mount State Card
    // ════════════════════════════════════════════════════════════════
    auto* stateCard = makeCard("Mount State", scrollContent);
    auto* stateBody = makeCardBody(stateCard);
    auto* stateLayout = new QVBoxLayout(stateBody);
    stateLayout->setSpacing(6);
    stateLayout->addWidget(makeDesc("Current mount controller status and tracking state.", stateBody));

    mount_state_label_ = new QLabel("State: Idle", stateBody);
    mount_state_label_->setStyleSheet("font-size: 13px; color: #e0e0e0;");
    stateLayout->addWidget(mount_state_label_);

    tracking_label_ = new QLabel("Tracking: Off", stateBody);
    tracking_label_->setStyleSheet("font-size: 11px; color: #a0a0b0;");
    stateLayout->addWidget(tracking_label_);

    stateCard->setLayout(new QVBoxLayout());
    stateCard->layout()->setContentsMargins(0, 0, 0, 0);
    stateCard->layout()->addWidget(stateBody);
    mainLayout->addWidget(stateCard);

    mainLayout->addStretch();

    scrollArea->setWidget(scrollContent);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);
}

void MountPanel::refresh(GrpcClient* client) {
    try {
        auto state = client->getState();
        auto status = state.status();

        QString stateStr;
        switch (status) {
            case astro_mount::ControllerState::IDLE:     stateStr = "Idle"; break;
            case astro_mount::ControllerState::SLEWING:  stateStr = "Slewing"; break;
            case astro_mount::ControllerState::TRACKING: stateStr = "Tracking"; break;
            case astro_mount::ControllerState::PARKED:   stateStr = "Parked"; break;
            case astro_mount::ControllerState::ERROR:    stateStr = "Error"; break;
            default: stateStr = "Unknown"; break;
        }
        mount_state_label_->setText(QString("State: %1").arg(stateStr));
        tracking_label_->setText(
            QString("Tracking: %1").arg(status == astro_mount::ControllerState::TRACKING ? "On" : "Off"));
    } catch (...) {
        mount_state_label_->setText("State: --");
        tracking_label_->setText("Tracking: --");
    }
}

void MountPanel::setGrpcClient(GrpcClient* client) {
    grpc_client_ = client;
    connectAxisButtons();
}

void MountPanel::connectAxisButtons() {
    if (!grpc_client_) return;

    // Axis 0 (HA/RA or Azimuth): Left = negative, Right = positive
    connect(btn_axis_left_, &QPushButton::pressed, this, [this]() {
        onAxisButtonPressed(0, -1);
    });
    connect(btn_axis_left_, &QPushButton::released, this, [this]() {
        onAxisButtonReleased(0);
    });
    connect(btn_axis_right_, &QPushButton::pressed, this, [this]() {
        onAxisButtonPressed(0, 1);
    });
    connect(btn_axis_right_, &QPushButton::released, this, [this]() {
        onAxisButtonReleased(0);
    });

    // Axis 1 (Dec or Altitude): Down = negative, Up = positive
    connect(btn_axis_down_, &QPushButton::pressed, this, [this]() {
        onAxisButtonPressed(1, -1);
    });
    connect(btn_axis_down_, &QPushButton::released, this, [this]() {
        onAxisButtonReleased(1);
    });
    connect(btn_axis_up_, &QPushButton::pressed, this, [this]() {
        onAxisButtonPressed(1, 1);
    });
    connect(btn_axis_up_, &QPushButton::released, this, [this]() {
        onAxisButtonReleased(1);
    });

    // Emergency stop button
    connect(btn_emergency_stop_, &QPushButton::clicked, this, [this]() {
        if (grpc_client_) {
            grpc_client_->emergencyStop(-1, false);
        }
    });

    // Quick action buttons
    connect(stop_btn_, &QPushButton::clicked, this, [this]() {
        if (grpc_client_) grpc_client_->stop();
    });
    connect(park_btn_, &QPushButton::clicked, this, [this]() {
        if (grpc_client_) grpc_client_->park();
    });
    connect(unpark_btn_, &QPushButton::clicked, this, [this]() {
        if (grpc_client_) grpc_client_->unpark();
    });
    connect(clear_errors_btn_, &QPushButton::clicked, this, [this]() {
        if (grpc_client_) grpc_client_->clearErrors();
    });

    // Axis mode toggle (Velocity ↔ Step)
    connect(axis_mode_toggle_, &QPushButton::clicked, this, [this]() {
        bool isVelocity = axis_mode_toggle_->text() == "Velocity";
        if (isVelocity) {
            axis_mode_toggle_->setText("Step");
            axis_mode_label_->setText("Step mode — click axis buttons for incremental moves");
        } else {
            axis_mode_toggle_->setText("Velocity");
            axis_mode_label_->setText("Velocity mode — hold axis buttons for continuous motion");
        }
    });
}

void MountPanel::onAxisButtonPressed(int axis_id, int direction) {
    if (!grpc_client_) return;

    // Read speed from slider (0.01 to 5.0 deg/s mapped as int*100)
    double speed = speed_slider_->value() / 100.0;
    double acceleration = accel_slider_->value() / 10.0;

    // Apply direction sign
    double velocity = speed * direction;

    // Use VELOCITY_CONTROL mode (mode=1)
    grpc_client_->controlAxis(axis_id, 1, 0.0, velocity, acceleration, false);
}

void MountPanel::onAxisButtonReleased(int axis_id) {
    if (!grpc_client_) return;

    double deceleration = decel_slider_->value() / 10.0;
    grpc_client_->stopAxis(axis_id, true, deceleration);
}

} // namespace panels
