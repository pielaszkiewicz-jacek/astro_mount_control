#include "panels/pec_panel.h"
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

PecPanel::PecPanel(QWidget* parent) : QWidget(parent) {
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* scrollContent = new QWidget(scrollArea);
    auto* mainLayout = new QVBoxLayout(scrollContent);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    // Enable / Status Card
    auto* statusCard = makeCard("PEC Status", scrollContent);
    auto* statusBody = makeCardBody(statusCard);
    auto* statusLayout = new QVBoxLayout(statusBody);
    statusLayout->setSpacing(8);
    statusLayout->addWidget(makeDesc("Periodic Error Correction status, peak error, and RMS error.", statusBody));

    enable_ = new QCheckBox("Enable PEC", statusBody);
    statusLayout->addWidget(enable_);

    status_label_ = new QLabel("Status: Disabled", statusBody);
    status_label_->setStyleSheet("font-size: 13px; font-weight: 600;");
    peak_label_ = new QLabel("Peak Error: --", statusBody);
    rms_label_ = new QLabel("RMS Error: --", statusBody);
    statusLayout->addWidget(status_label_);
    statusLayout->addWidget(peak_label_);
    statusLayout->addWidget(rms_label_);

    statusCard->setLayout(new QVBoxLayout());
    statusCard->layout()->setContentsMargins(0, 0, 0, 0);
    statusCard->layout()->addWidget(statusBody);
    mainLayout->addWidget(statusCard);

    // Training Card
    auto* trainCard = makeCard("Training", scrollContent);
    auto* trainBody = makeCardBody(trainCard);
    auto* trainLayout = new QVBoxLayout(trainBody);
    trainLayout->setSpacing(10);
    trainLayout->addWidget(makeDesc("Train the PEC model by recording worm cycle errors over multiple harmonics.", trainBody));

    auto* trainForm = new QFormLayout();
    worm_cycle_ = new QSpinBox(trainBody);
    worm_cycle_->setRange(1, 10000);
    worm_cycle_->setValue(479);
    worm_cycle_->setSuffix(" s");
    trainForm->addRow("Worm Cycle:", worm_cycle_);

    harmonics_ = new QSpinBox(trainBody);
    harmonics_->setRange(1, 20);
    harmonics_->setValue(5);
    trainForm->addRow("Harmonics:", harmonics_);
    trainLayout->addLayout(trainForm);

    training_progress_ = new QProgressBar(trainBody);
    training_progress_->setRange(0, 100);
    trainLayout->addWidget(training_progress_);

    auto* trainBtnRow = new QHBoxLayout();
    train_btn_ = new QPushButton("Train", trainBody);
    train_btn_->setProperty("primary", true);
    stop_btn_ = new QPushButton("Stop", trainBody);
    stop_btn_->setProperty("danger", true);
    trainBtnRow->addWidget(train_btn_);
    trainBtnRow->addWidget(stop_btn_);
    trainLayout->addLayout(trainBtnRow);

    trainCard->setLayout(new QVBoxLayout());
    trainCard->layout()->setContentsMargins(0, 0, 0, 0);
    trainCard->layout()->addWidget(trainBody);
    mainLayout->addWidget(trainCard);

    // File Operations Card
    auto* fileCard = makeCard("File Operations", scrollContent);
    auto* fileBody = makeCardBody(fileCard);
    auto* fileLayout = new QHBoxLayout(fileBody);
    fileLayout->setSpacing(8);

    save_btn_ = new QPushButton("Save PEC Data", fileBody);
    load_btn_ = new QPushButton("Load PEC Data", fileBody);
    fileLayout->addWidget(save_btn_);
    fileLayout->addWidget(load_btn_);

    fileCard->setLayout(new QVBoxLayout());
    fileCard->layout()->setContentsMargins(0, 0, 0, 0);
    fileCard->layout()->addWidget(fileBody);
    mainLayout->addWidget(fileCard);

    mainLayout->addStretch();

    scrollArea->setWidget(scrollContent);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);
}

} // namespace panels
