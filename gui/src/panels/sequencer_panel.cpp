#include "panels/sequencer_panel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
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

SequencerPanel::SequencerPanel(QWidget* parent) : QWidget(parent) {
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* scrollContent = new QWidget(scrollArea);
    auto* mainLayout = new QVBoxLayout(scrollContent);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    // Sequencer Status Card
    auto* statusCard = makeCard("Sequencer Status", scrollContent);
    auto* statusBody = makeCardBody(statusCard);
    auto* statusLayout = new QVBoxLayout(statusBody);
    statusLayout->setSpacing(10);
    statusLayout->addWidget(makeDesc("Current observation sequence progress, target, and exposure information.", statusBody));

    state_label_ = new QLabel("State: Idle", statusBody);
    state_label_->setStyleSheet("font-size: 13px; font-weight: 600;");
    target_label_ = new QLabel("Target: --", statusBody);
    exposure_label_ = new QLabel("Exposure: --", statusBody);
    statusLayout->addWidget(state_label_);
    statusLayout->addWidget(target_label_);
    statusLayout->addWidget(exposure_label_);

    progress_ = new QProgressBar(statusBody);
    progress_->setRange(0, 100);
    progress_->setValue(0);
    statusLayout->addWidget(progress_);

    statusCard->setLayout(new QVBoxLayout());
    statusCard->layout()->setContentsMargins(0, 0, 0, 0);
    statusCard->layout()->addWidget(statusBody);
    mainLayout->addWidget(statusCard);

    // Target List Card
    auto* listCard = makeCard("Target List", scrollContent);
    auto* listBody = makeCardBody(listCard);
    auto* listLayout = new QVBoxLayout(listBody);
    listLayout->addWidget(makeDesc("List of targets in the current observation plan.", listBody));

    target_list_ = new QListWidget(listBody);
    target_list_->setMinimumHeight(150);
    listLayout->addWidget(target_list_);

    listCard->setLayout(new QVBoxLayout());
    listCard->layout()->setContentsMargins(0, 0, 0, 0);
    listCard->layout()->addWidget(listBody);
    mainLayout->addWidget(listCard);

    // Control Card
    auto* ctrlCard = makeCard("Controls", scrollContent);
    auto* ctrlBody = makeCardBody(ctrlCard);
    auto* ctrlLayout = new QHBoxLayout(ctrlBody);
    ctrlLayout->setSpacing(8);

    start_btn_ = new QPushButton("▶ Start", ctrlBody);
    start_btn_->setProperty("primary", true);
    pause_btn_ = new QPushButton("⏸ Pause", ctrlBody);
    pause_btn_->setProperty("warning", true);
    stop_btn_ = new QPushButton("⏹ Stop", ctrlBody);
    stop_btn_->setProperty("danger", true);
    load_btn_ = new QPushButton("📂 Load Plan", ctrlBody);

    ctrlLayout->addWidget(start_btn_);
    ctrlLayout->addWidget(pause_btn_);
    ctrlLayout->addWidget(stop_btn_);
    ctrlLayout->addWidget(load_btn_);

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
