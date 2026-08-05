#include "panels/derotator_panel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QFormLayout>
#include <QLabel>

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

DerotatorPanel::DerotatorPanel(QWidget* parent) : QWidget(parent) {
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* scrollContent = new QWidget(scrollArea);
    auto* mainLayout = new QVBoxLayout(scrollContent);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    // Mode Selection Card
    auto* modeCard = makeCard("Mode", scrollContent);
    auto* modeBody = makeCardBody(modeCard);
    auto* modeLayout = new QVBoxLayout(modeBody);
    modeLayout->setSpacing(6);
    modeLayout->addWidget(makeDesc("Field derotator operating mode: Disabled, Automatic (sky-tracked), "
                                   "Fixed angle, or Manual control.", modeBody));

    disabled_ = new QRadioButton("Disabled", modeBody);
    auto_ = new QRadioButton("Automatic", modeBody);
    fixed_ = new QRadioButton("Fixed", modeBody);
    manual_ = new QRadioButton("Manual", modeBody);
    disabled_->setChecked(true);

    modeLayout->addWidget(disabled_);
    modeLayout->addWidget(auto_);
    modeLayout->addWidget(fixed_);
    modeLayout->addWidget(manual_);

    modeCard->setLayout(new QVBoxLayout());
    modeCard->layout()->setContentsMargins(0, 0, 0, 0);
    modeCard->layout()->addWidget(modeBody);
    mainLayout->addWidget(modeCard);

    // Status Card
    auto* statusCard = makeCard("Status", scrollContent);
    auto* statusBody = makeCardBody(statusCard);
    auto* statusLayout = new QVBoxLayout(statusBody);
    statusLayout->setSpacing(6);
    statusLayout->addWidget(makeDesc("Current derotator position, rotation rate, and homing status.", statusBody));

    pos_label_ = new QLabel("Position: --", statusBody);
    pos_label_->setStyleSheet("font-size: 13px; font-weight: 600;");
    rate_label_ = new QLabel("Rate: --", statusBody);
    homed_label_ = new QLabel("Homed: --", statusBody);

    statusLayout->addWidget(pos_label_);
    statusLayout->addWidget(rate_label_);
    statusLayout->addWidget(homed_label_);

    statusCard->setLayout(new QVBoxLayout());
    statusCard->layout()->setContentsMargins(0, 0, 0, 0);
    statusCard->layout()->addWidget(statusBody);
    mainLayout->addWidget(statusCard);

    // Manual Control Card
    auto* manualCard = makeCard("Manual Control", scrollContent);
    auto* manualBody = makeCardBody(manualCard);
    auto* manualCtrlLayout = new QVBoxLayout(manualBody);
    manualCtrlLayout->setSpacing(10);
    manualCtrlLayout->addWidget(makeDesc("Direct angle and rate control for the derotator.", manualBody));

    auto* angleForm = new QFormLayout();
    angle_spin_ = new QDoubleSpinBox(manualBody);
    angle_spin_->setRange(0.0, 360.0);
    angle_spin_->setValue(0.0);
    angle_spin_->setSuffix("°");
    angleForm->addRow("Angle:", angle_spin_);
    manualCtrlLayout->addLayout(angleForm);

    auto* rateForm = new QFormLayout();
    rate_spin_ = new QDoubleSpinBox(manualBody);
    rate_spin_->setRange(-10.0, 10.0);
    rate_spin_->setValue(0.0);
    rate_spin_->setSuffix("°/h");
    rateForm->addRow("Rate:", rate_spin_);
    manualCtrlLayout->addLayout(rateForm);

    auto* btnRow = new QHBoxLayout();
    set_angle_btn_ = new QPushButton("Set Angle", manualBody);
    set_angle_btn_->setProperty("primary", true);
    set_rate_btn_ = new QPushButton("Set Rate", manualBody);
    home_btn_ = new QPushButton("Home", manualBody);
    btnRow->addWidget(set_angle_btn_);
    btnRow->addWidget(set_rate_btn_);
    btnRow->addWidget(home_btn_);
    manualCtrlLayout->addLayout(btnRow);

    manualCard->setLayout(new QVBoxLayout());
    manualCard->layout()->setContentsMargins(0, 0, 0, 0);
    manualCard->layout()->addWidget(manualBody);
    mainLayout->addWidget(manualCard);

    mainLayout->addStretch();

    scrollArea->setWidget(scrollContent);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);
}

} // namespace panels
