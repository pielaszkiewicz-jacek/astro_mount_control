#include "panels/dome_panel.h"
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

DomePanel::DomePanel(QWidget* parent) : QWidget(parent) {
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* scrollContent = new QWidget(scrollArea);
    auto* mainLayout = new QVBoxLayout(scrollContent);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    // Dome Status Card
    auto* statusCard = makeCard("Dome Status", scrollContent);
    auto* statusBody = makeCardBody(statusCard);
    auto* statusLayout = new QVBoxLayout(statusBody);
    statusLayout->setSpacing(6);
    statusLayout->addWidget(makeDesc("Current dome state, shutter position, and azimuth.", statusBody));

    state_label_ = new QLabel("State: --", statusBody);
    state_label_->setStyleSheet("font-size: 13px; font-weight: 600;");
    shutter_label_ = new QLabel("Shutter: --", statusBody);
    azimuth_label_ = new QLabel("Azimuth: --", statusBody);

    statusLayout->addWidget(state_label_);
    statusLayout->addWidget(shutter_label_);
    statusLayout->addWidget(azimuth_label_);

    statusCard->setLayout(new QVBoxLayout());
    statusCard->layout()->setContentsMargins(0, 0, 0, 0);
    statusCard->layout()->addWidget(statusBody);
    mainLayout->addWidget(statusCard);

    // Dome Controls Card
    auto* ctrlCard = makeCard("Controls", scrollContent);
    auto* ctrlBody = makeCardBody(ctrlCard);
    auto* ctrlLayout = new QVBoxLayout(ctrlBody);
    ctrlLayout->setSpacing(10);
    ctrlLayout->addWidget(makeDesc("Dome shutter and rotation controls.", ctrlBody));

    auto* btnRow1 = new QHBoxLayout();
    open_btn_ = new QPushButton("Open Shutter", ctrlBody);
    open_btn_->setProperty("primary", true);
    close_btn_ = new QPushButton("Close Shutter", ctrlBody);
    close_btn_->setProperty("warning", true);
    btnRow1->addWidget(open_btn_);
    btnRow1->addWidget(close_btn_);
    ctrlLayout->addLayout(btnRow1);

    auto* rotForm = new QFormLayout();
    azimuth_spin_ = new QDoubleSpinBox(ctrlBody);
    azimuth_spin_->setRange(0.0, 360.0);
    azimuth_spin_->setValue(0.0);
    azimuth_spin_->setSuffix("°");
    rotForm->addRow("Azimuth:", azimuth_spin_);
    ctrlLayout->addLayout(rotForm);

    auto* btnRow2 = new QHBoxLayout();
    rotate_btn_ = new QPushButton("Rotate", ctrlBody);
    rotate_btn_->setProperty("primary", true);
    park_btn_ = new QPushButton("Park Dome", ctrlBody);
    btnRow2->addWidget(rotate_btn_);
    btnRow2->addWidget(park_btn_);
    ctrlLayout->addLayout(btnRow2);

    ctrlCard->setLayout(new QVBoxLayout());
    ctrlCard->layout()->setContentsMargins(0, 0, 0, 0);
    ctrlCard->layout()->addWidget(ctrlBody);
    mainLayout->addWidget(ctrlCard);

    mainLayout->addStretch();

    scrollArea->setWidget(scrollContent);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);
}

} // namespace panels
