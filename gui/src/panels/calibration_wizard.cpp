#include "panels/calibration_wizard.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QFormLayout>
#include <QHeaderView>
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

CalibrationWizard::CalibrationWizard(QWidget* parent) : QWidget(parent) {
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* scrollContent = new QWidget(scrollArea);
    auto* mainLayout = new QVBoxLayout(scrollContent);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    // 1. Bootstrap Calibration Card
    auto* bootstrapCard = makeCard("Initial Calibration (Bootstrap)", scrollContent);
    auto* bootstrapBody = makeCardBody(bootstrapCard);
    auto* bsLayout = new QVBoxLayout(bootstrapBody);
    bsLayout->setSpacing(10);
    bsLayout->addWidget(makeDesc("First-stage calibration that establishes the initial mount orientation. "
                                 "Requires manual or automatic star measurements.", bootstrapBody));

    bootstrap_status_ = new QLabel("Bootstrap: Not Calibrated", bootstrapBody);
    bootstrap_status_->setStyleSheet("font-size: 12px; color: #a0a0b0; padding: 4px 0;");
    bsLayout->addWidget(bootstrap_status_);

    auto* bsBtnRow = new QHBoxLayout();
    run_bootstrap_btn_ = new QPushButton("Run Initial Calibration", bootstrapBody);
    run_bootstrap_btn_->setProperty("primary", true);
    clear_btn_ = new QPushButton("Clear Measurements", bootstrapBody);
    clear_btn_->setProperty("danger", true);
    bsBtnRow->addWidget(run_bootstrap_btn_);
    bsBtnRow->addWidget(clear_btn_);
    bsLayout->addLayout(bsBtnRow);

    bootstrapCard->setLayout(new QVBoxLayout());
    bootstrapCard->layout()->setContentsMargins(0, 0, 0, 0);
    bootstrapCard->layout()->addWidget(bootstrapBody);
    mainLayout->addWidget(bootstrapCard);

    // 2. TPOINT Calibration Card
    auto* tpointCard = makeCard("Precise Calibration (TPOINT)", scrollContent);
    auto* tpointBody = makeCardBody(tpointCard);
    auto* tpLayout = new QVBoxLayout(tpointBody);
    tpLayout->setSpacing(10);
    tpLayout->addWidget(makeDesc("Second-stage precise pointing model calibration. "
                                 "Refines the mount model after bootstrap is complete.", tpointBody));

    tpoint_status_ = new QLabel("TPoint: Inactive", tpointBody);
    tpoint_status_->setStyleSheet("font-size: 12px; color: #a0a0b0; padding: 4px 0;");
    tpLayout->addWidget(tpoint_status_);

    auto* tpBtnRow = new QHBoxLayout();
    run_tpoint_btn_ = new QPushButton("Run TPOINT Calibration", tpointBody);
    run_tpoint_btn_->setProperty("primary", true);
    tpBtnRow->addWidget(run_tpoint_btn_);
    tpLayout->addLayout(tpBtnRow);

    tpointCard->setLayout(new QVBoxLayout());
    tpointCard->layout()->setContentsMargins(0, 0, 0, 0);
    tpointCard->layout()->addWidget(tpointBody);
    mainLayout->addWidget(tpointCard);

    // 3. Measurements Table Card
    auto* tableCard = makeCard("Measurements", scrollContent);
    auto* tableBody = makeCardBody(tableCard);
    auto* tableLayout = new QVBoxLayout(tableBody);
    tableLayout->addWidget(makeDesc("Calibration measurement records with observed and expected coordinates.", tableBody));

    measurements_table_ = new QTableWidget(0, 4, tableBody);
    measurements_table_->setHorizontalHeaderLabels({"RA", "Dec", "Expected RA", "Expected Dec"});
    measurements_table_->horizontalHeader()->setStretchLastSection(true);
    measurements_table_->setMinimumHeight(200);
    tableLayout->addWidget(measurements_table_);

    add_meas_btn_ = new QPushButton("Add Measurement", tableBody);
    add_meas_btn_->setProperty("primary", true);
    tableLayout->addWidget(add_meas_btn_);

    tableCard->setLayout(new QVBoxLayout());
    tableCard->layout()->setContentsMargins(0, 0, 0, 0);
    tableCard->layout()->addWidget(tableBody);
    mainLayout->addWidget(tableCard);

    mainLayout->addStretch();

    scrollArea->setWidget(scrollContent);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);
}

} // namespace panels
