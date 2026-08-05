#include "panels/camera_panel.h"
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

CameraPanel::CameraPanel(QWidget* parent) : QWidget(parent) {
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* scrollContent = new QWidget(scrollArea);
    auto* mainLayout = new QVBoxLayout(scrollContent);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    // Camera Info Card
    auto* infoCard = makeCard("Camera Info", scrollContent);
    auto* infoBody = makeCardBody(infoCard);
    auto* infoLayout = new QVBoxLayout(infoBody);
    infoLayout->setSpacing(6);
    infoLayout->addWidget(makeDesc("Connected camera model, sensor temperature, and cooler status.", infoBody));

    info_label_ = new QLabel("Camera: --", infoBody);
    info_label_->setStyleSheet("font-size: 13px; font-weight: 600;");
    temp_label_ = new QLabel("Temperature: --", infoBody);
    cooler_label_ = new QLabel("Cooler: --", infoBody);
    infoLayout->addWidget(info_label_);
    infoLayout->addWidget(temp_label_);
    infoLayout->addWidget(cooler_label_);

    infoCard->setLayout(new QVBoxLayout());
    infoCard->layout()->setContentsMargins(0, 0, 0, 0);
    infoCard->layout()->addWidget(infoBody);
    mainLayout->addWidget(infoCard);

    // Exposure Control Card
    auto* expCard = makeCard("Exposure Control", scrollContent);
    auto* expBody = makeCardBody(expCard);
    auto* expLayout = new QVBoxLayout(expBody);
    expLayout->setSpacing(10);
    expLayout->addWidget(makeDesc("Capture settings: exposure time, binning mode, and filter selection.", expBody));

    auto* expForm = new QFormLayout();
    exposure_spin_ = new QDoubleSpinBox(expBody);
    exposure_spin_->setRange(0.001, 3600.0);
    exposure_spin_->setValue(1.0);
    exposure_spin_->setSuffix(" s");
    exposure_spin_->setDecimals(3);
    expForm->addRow("Exposure Time:", exposure_spin_);

    binning_combo_ = new QComboBox(expBody);
    binning_combo_->addItems({"1x1", "2x2", "3x3", "4x4"});
    binning_combo_->setCurrentIndex(0);
    expForm->addRow("Binning:", binning_combo_);

    filter_combo_ = new QComboBox(expBody);
    filter_combo_->addItems({"L", "R", "G", "B", "Ha", "OIII", "SII"});
    filter_combo_->setCurrentIndex(0);
    expForm->addRow("Filter:", filter_combo_);
    expLayout->addLayout(expForm);

    exposure_progress_ = new QProgressBar(expBody);
    exposure_progress_->setRange(0, 100);
    expLayout->addWidget(exposure_progress_);

    auto* expBtnRow = new QHBoxLayout();
    expose_btn_ = new QPushButton("Start Exposure", expBody);
    expose_btn_->setProperty("primary", true);
    abort_btn_ = new QPushButton("Abort", expBody);
    abort_btn_->setProperty("danger", true);
    expBtnRow->addWidget(expose_btn_);
    expBtnRow->addWidget(abort_btn_);
    expLayout->addLayout(expBtnRow);

    expCard->setLayout(new QVBoxLayout());
    expCard->layout()->setContentsMargins(0, 0, 0, 0);
    expCard->layout()->addWidget(expBody);
    mainLayout->addWidget(expCard);

    mainLayout->addStretch();

    scrollArea->setWidget(scrollContent);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);
}

} // namespace panels
