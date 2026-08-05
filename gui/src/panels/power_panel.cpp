#include "panels/power_panel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QGridLayout>
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

PowerPanel::PowerPanel(QWidget* parent) : QWidget(parent) {
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* scrollContent = new QWidget(scrollArea);
    auto* mainLayout = new QVBoxLayout(scrollContent);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    // Power Readings Card
    auto* readingsCard = makeCard("Power Readings", scrollContent);
    auto* readingsBody = makeCardBody(readingsCard);
    auto* readingsLayout = new QVBoxLayout(readingsBody);
    readingsLayout->setSpacing(6);
    readingsLayout->addWidget(makeDesc("Real-time voltage, current, power consumption, and temperature.", readingsBody));

    voltage_ = new QLabel("--", readingsBody);
    current_ = new QLabel("--", readingsBody);
    power_ = new QLabel("--", readingsBody);
    temp_ = new QLabel("--", readingsBody);

    addStatRow(readingsLayout, "Voltage:", voltage_);
    addStatRow(readingsLayout, "Current:", current_);
    addStatRow(readingsLayout, "Power:", power_);
    addStatRow(readingsLayout, "Temperature:", temp_);

    readingsCard->setLayout(new QVBoxLayout());
    readingsCard->layout()->setContentsMargins(0, 0, 0, 0);
    readingsCard->layout()->addWidget(readingsBody);
    mainLayout->addWidget(readingsCard);

    // Battery Status Card
    auto* batteryCard = makeCard("Battery Status", scrollContent);
    auto* batteryBody = makeCardBody(batteryCard);
    auto* batteryLayout = new QVBoxLayout(batteryBody);
    batteryLayout->setSpacing(6);
    batteryLayout->addWidget(makeDesc("Battery charge level, estimated runtime, power source, and charging state.", batteryBody));

    battery_ = new QLabel("--", batteryBody);
    runtime_ = new QLabel("--", batteryBody);
    source_ = new QLabel("--", batteryBody);
    charging_ = new QLabel("--", batteryBody);

    addStatRow(batteryLayout, "Battery Level:", battery_);
    addStatRow(batteryLayout, "Runtime:", runtime_);
    addStatRow(batteryLayout, "Power Source:", source_);
    addStatRow(batteryLayout, "Charging:", charging_);

    batteryCard->setLayout(new QVBoxLayout());
    batteryCard->layout()->setContentsMargins(0, 0, 0, 0);
    batteryCard->layout()->addWidget(batteryBody);
    mainLayout->addWidget(batteryCard);

    mainLayout->addStretch();

    scrollArea->setWidget(scrollContent);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);
}

} // namespace panels
