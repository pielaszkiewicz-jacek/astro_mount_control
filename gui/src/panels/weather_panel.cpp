#include "panels/weather_panel.h"
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

static void addSensorRow(QGridLayout* grid, int row, const QString& label, QLabel* valueLabel) {
    auto* lbl = new QLabel(label, valueLabel->parentWidget());
    lbl->setStyleSheet("font-size: 10px; text-transform: uppercase; letter-spacing: 0.5px; color: #6c6c80; font-weight: 600;");
    valueLabel->setStyleSheet("font-size: 14px; font-weight: 600; color: #e0e0e0; font-family: monospace;");
    grid->addWidget(lbl, row, 0);
    grid->addWidget(valueLabel, row, 1);
}

WeatherPanel::WeatherPanel(QWidget* parent) : QWidget(parent) {
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* scrollContent = new QWidget(scrollArea);
    auto* mainLayout = new QVBoxLayout(scrollContent);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    // Weather Data Card (sensor grid)
    auto* dataCard = makeCard("Weather Data", scrollContent);
    auto* dataBody = makeCardBody(dataCard);
    auto* dataWrap = new QVBoxLayout(dataBody);
    dataWrap->setSpacing(6);
    dataWrap->addWidget(makeDesc("Live atmospheric conditions from the weather station or online source.", dataBody));

    auto* grid = new QGridLayout();
    grid->setSpacing(8);

    temp_ = new QLabel("--", dataBody);
    humidity_ = new QLabel("--", dataBody);
    pressure_ = new QLabel("--", dataBody);
    wind_ = new QLabel("--", dataBody);
    rain_ = new QLabel("--", dataBody);
    clouds_ = new QLabel("--", dataBody);

    addSensorRow(grid, 0, "Temperature", temp_);
    addSensorRow(grid, 1, "Humidity", humidity_);
    addSensorRow(grid, 2, "Pressure", pressure_);
    addSensorRow(grid, 3, "Wind", wind_);
    addSensorRow(grid, 4, "Rain", rain_);
    addSensorRow(grid, 5, "Clouds", clouds_);
    dataWrap->addLayout(grid);

    dataCard->setLayout(new QVBoxLayout());
    dataCard->layout()->setContentsMargins(0, 0, 0, 0);
    dataCard->layout()->addWidget(dataBody);
    mainLayout->addWidget(dataCard);

    // Safety Status Card
    auto* safetyCard = makeCard("Safety Status", scrollContent);
    auto* safetyBody = makeCardBody(safetyCard);
    auto* safetyLayout = new QVBoxLayout(safetyBody);
    safetyLayout->addWidget(makeDesc("Overall safety assessment based on weather rules. "
                                     "Triggers alerts when conditions are unsafe.", safetyBody));

    safety_label_ = new QLabel("Conditions: OK", safetyBody);
    safety_label_->setStyleSheet(
        "font-size: 14px; font-weight: 600; color: #66bb6a; "
        "background: rgba(102, 187, 106, 0.1); border: 1px solid #66bb6a; "
        "border-radius: 8px; padding: 10px;");
    safety_label_->setAlignment(Qt::AlignCenter);
    safetyLayout->addWidget(safety_label_);

    safetyCard->setLayout(new QVBoxLayout());
    safetyCard->layout()->setContentsMargins(0, 0, 0, 0);
    safetyCard->layout()->addWidget(safetyBody);
    mainLayout->addWidget(safetyCard);

    mainLayout->addStretch();

    scrollArea->setWidget(scrollContent);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);
}

} // namespace panels
