#include "panels/status_panel.h"
#include "grpc_client.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QGridLayout>

namespace panels {

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

static void addStatRow(QVBoxLayout* layout, const QString& label, QLabel* valueLabel) {
    auto* row = new QHBoxLayout();
    auto* lbl = new QLabel(label, valueLabel->parentWidget());
    lbl->setStyleSheet("font-size: 11px; color: #a0a0b0; font-weight: 500;");
    valueLabel->setStyleSheet("font-size: 12px; color: #e0e0e0; font-family: monospace; font-weight: 500;");
    row->addWidget(lbl);
    row->addStretch();
    row->addWidget(valueLabel);
    layout->addLayout(row);
}

StatusPanel::StatusPanel(QWidget* parent) : QWidget(parent) {
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* scrollContent = new QWidget(scrollArea);
    auto* mainLayout = new QVBoxLayout(scrollContent);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    // ════════════════════════════════════════════════════════════════
    // 1. Mount Status Card
    // ════════════════════════════════════════════════════════════════
    auto* mountCard = makeCard("Mount Status", scrollContent);
    auto* mountBody = makeCardBody(mountCard);
    auto* mountLayout = new QVBoxLayout(mountBody);
    mountLayout->setSpacing(6);
    mountLayout->addWidget(makeDesc("Real-time axis positions, rates, pier side, and meridian crossing information.", mountBody));

    axis1_pos_ = new QLabel("--", mountBody);
    axis2_pos_ = new QLabel("--", mountBody);
    axis1_rate_ = new QLabel("--", mountBody);
    axis2_rate_ = new QLabel("--", mountBody);
    pier_side_ = new QLabel("--", mountBody);
    meridian_time_ = new QLabel("--", mountBody);
    flip_pending_ = new QLabel("--", mountBody);

    addStatRow(mountLayout, "Axis 1 Position:", axis1_pos_);
    addStatRow(mountLayout, "Axis 2 Position:", axis2_pos_);
    addStatRow(mountLayout, "Axis 1 Rate:", axis1_rate_);
    addStatRow(mountLayout, "Axis 2 Rate:", axis2_rate_);
    addStatRow(mountLayout, "Pier Side:", pier_side_);
    addStatRow(mountLayout, "Meridian Time:", meridian_time_);
    addStatRow(mountLayout, "Flip Pending:", flip_pending_);

    mountCard->setLayout(new QVBoxLayout());
    mountCard->layout()->setContentsMargins(0, 0, 0, 0);
    mountCard->layout()->addWidget(mountBody);
    mainLayout->addWidget(mountCard);

    // ════════════════════════════════════════════════════════════════
    // 2. Position Card
    // ════════════════════════════════════════════════════════════════
    auto* posCard = makeCard("Position", scrollContent);
    auto* posBody = makeCardBody(posCard);
    auto* posLayout = new QVBoxLayout(posBody);
    posLayout->setSpacing(6);
    posLayout->addWidget(makeDesc("Current celestial coordinates and tracking errors.", posBody));

    tracking_ra_ = new QLabel("--", posBody);
    tracking_dec_ = new QLabel("--", posBody);
    error_ra_ = new QLabel("--", posBody);
    error_dec_ = new QLabel("--", posBody);

    addStatRow(posLayout, "RA:", tracking_ra_);
    addStatRow(posLayout, "Dec:", tracking_dec_);
    addStatRow(posLayout, "Err RA:", error_ra_);
    addStatRow(posLayout, "Err Dec:", error_dec_);

    posCard->setLayout(new QVBoxLayout());
    posCard->layout()->setContentsMargins(0, 0, 0, 0);
    posCard->layout()->addWidget(posBody);
    mainLayout->addWidget(posCard);

    // ════════════════════════════════════════════════════════════════
    // 3. Tracking Card
    // ════════════════════════════════════════════════════════════════
    auto* trackCard = makeCard("Tracking", scrollContent);
    auto* trackBody = makeCardBody(trackCard);
    auto* trackLayout = new QVBoxLayout(trackBody);
    trackLayout->setSpacing(6);
    trackLayout->addWidget(makeDesc("Encoder, guider, and TPOINT calibration status.", trackBody));

    encoders_ = new QLabel("--", trackBody);
    guider_ = new QLabel("--", trackBody);
    tpoint_ = new QLabel("--", trackBody);

    addStatRow(trackLayout, "Encoders:", encoders_);
    addStatRow(trackLayout, "Guider:", guider_);
    addStatRow(trackLayout, "TPoint:", tpoint_);

    trackCard->setLayout(new QVBoxLayout());
    trackCard->layout()->setContentsMargins(0, 0, 0, 0);
    trackCard->layout()->addWidget(trackBody);
    mainLayout->addWidget(trackCard);

    // ════════════════════════════════════════════════════════════════
    // 4. Velocity Chart placeholder
    // ════════════════════════════════════════════════════════════════
    auto* velCard = makeCard("Axis Velocity", scrollContent);
    auto* velBody = makeCardBody(velCard);
    auto* velLayout = new QVBoxLayout(velBody);
    velLayout->addWidget(makeDesc("Real-time velocity chart for both axes (requires active connection).", velBody));
    auto* velPlaceholder = new QLabel("Velocity chart — coming soon", velBody);
    velPlaceholder->setAlignment(Qt::AlignCenter);
    velPlaceholder->setStyleSheet("color: #6c6c80; padding: 30px;");
    velLayout->addWidget(velPlaceholder);

    velCard->setLayout(new QVBoxLayout());
    velCard->layout()->setContentsMargins(0, 0, 0, 0);
    velCard->layout()->addWidget(velBody);
    mainLayout->addWidget(velCard);

    mainLayout->addStretch();

    scrollArea->setWidget(scrollContent);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);
}

void StatusPanel::refresh(GrpcClient* client) {
    try {
        auto state = client->getState();

        // Axis positions (from MountPosition and telescope fields)
        axis1_pos_->setText(QString::number(state.current_position().axis1(), 'f', 4) + "°");
        axis2_pos_->setText(QString::number(state.current_position().axis2(), 'f', 4) + "°");
        axis1_rate_->setText(QString::number(state.actual_rate_axis1(), 'f', 4) + "°/s");
        axis2_rate_->setText(QString::number(state.actual_rate_axis2(), 'f', 4) + "°/s");

        // Celestial coordinates from tracked object
        const auto& coords = state.tracked_object().coordinates();
        tracking_ra_->setText(QString::number(coords.ra(), 'f', 6) + "h");
        tracking_dec_->setText(QString::number(coords.dec(), 'f', 6) + "°");

        // Tracking errors
        const auto& tracked = state.tracked_object();
        error_ra_->setText(QString::number(tracked.tracking_error_ra(), 'f', 2) + "\"");
        error_dec_->setText(QString::number(tracked.tracking_error_dec(), 'f', 2) + "\"");

        // Pier side: double (1=East, -1=West)
        double ps = state.pier_side();
        pier_side_->setText(ps > 0 ? "East" : (ps < 0 ? "West" : "Unknown"));

        // Time to meridian in hours
        meridian_time_->setText(QString::number(state.time_to_meridian(), 'f', 2) + " h");

        // Meridian flip state
        flip_pending_->setText(state.meridian_flipped() ? "Flipped" : "Normal");

        encoders_->setText(state.encoders_enabled() ? "Active" : "Inactive");
        guider_->setText(state.guider_active() ? "Active" : "Inactive");
        tpoint_->setText(state.tpoint_params().calibrated() ? "Calibrated" : "Not Calibrated");
    } catch (...) {
        axis1_pos_->setText("--");
        axis2_pos_->setText("--");
        axis1_rate_->setText("--");
        axis2_rate_->setText("--");
        tracking_ra_->setText("--");
        tracking_dec_->setText("--");
        error_ra_->setText("--");
        error_dec_->setText("--");
        pier_side_->setText("--");
        meridian_time_->setText("--");
        flip_pending_->setText("--");
        encoders_->setText("--");
        guider_->setText("--");
        tpoint_->setText("--");
    }
}

} // namespace panels
