#include "panels/focuser_panel.h"
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

FocuserPanel::FocuserPanel(QWidget* parent) : QWidget(parent) {
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* scrollContent = new QWidget(scrollArea);
    auto* mainLayout = new QVBoxLayout(scrollContent);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    // Focuser Status Card
    auto* statusCard = makeCard("Focuser Status", scrollContent);
    auto* statusBody = makeCardBody(statusCard);
    auto* statusLayout = new QVBoxLayout(statusBody);
    statusLayout->setSpacing(6);
    statusLayout->addWidget(makeDesc("Current focuser position, temperature, and HFD (Half-Flux Diameter) reading.", statusBody));

    pos_label_ = new QLabel("Position: --", statusBody);
    pos_label_->setStyleSheet("font-size: 13px; font-weight: 600;");
    temp_label_ = new QLabel("Temperature: --", statusBody);
    hfd_label_ = new QLabel("HFD: --", statusBody);
    statusLayout->addWidget(pos_label_);
    statusLayout->addWidget(temp_label_);
    statusLayout->addWidget(hfd_label_);

    statusCard->setLayout(new QVBoxLayout());
    statusCard->layout()->setContentsMargins(0, 0, 0, 0);
    statusCard->layout()->addWidget(statusBody);
    mainLayout->addWidget(statusCard);

    // Position Control Card
    auto* posCard = makeCard("Position Control", scrollContent);
    auto* posBody = makeCardBody(posCard);
    auto* posCtrlLayout = new QVBoxLayout(posBody);
    posCtrlLayout->setSpacing(10);
    posCtrlLayout->addWidget(makeDesc("Manual focuser movement with adjustable speed.", posBody));

    auto* posForm = new QFormLayout();
    position_spin_ = new QSpinBox(posBody);
    position_spin_->setRange(0, 100000);
    position_spin_->setValue(0);
    posForm->addRow("Target Position:", position_spin_);
    posCtrlLayout->addLayout(posForm);

    speed_slider_ = new QSlider(Qt::Horizontal, posBody);
    speed_slider_->setRange(1, 100);
    speed_slider_->setValue(50);
    posCtrlLayout->addWidget(new QLabel("Speed:", posBody));
    posCtrlLayout->addWidget(speed_slider_);

    auto* moveBtnRow = new QHBoxLayout();
    move_btn_ = new QPushButton("Move", posBody);
    move_btn_->setProperty("primary", true);
    halt_btn_ = new QPushButton("Halt", posBody);
    halt_btn_->setProperty("danger", true);
    moveBtnRow->addWidget(move_btn_);
    moveBtnRow->addWidget(halt_btn_);
    posCtrlLayout->addLayout(moveBtnRow);

    posCard->setLayout(new QVBoxLayout());
    posCard->layout()->setContentsMargins(0, 0, 0, 0);
    posCard->layout()->addWidget(posBody);
    mainLayout->addWidget(posCard);

    // Autofocus Card
    auto* afCard = makeCard("Autofocus", scrollContent);
    auto* afBody = makeCardBody(afCard);
    auto* afLayout = new QVBoxLayout(afBody);
    afLayout->setSpacing(10);
    afLayout->addWidget(makeDesc("Automated focus routine: scans from start to end position in steps to find optimal focus.", afBody));

    auto* afForm = new QFormLayout();
    af_start_ = new QSpinBox(afBody);
    af_start_->setRange(0, 100000);
    af_start_->setValue(0);
    af_end_ = new QSpinBox(afBody);
    af_end_->setRange(0, 100000);
    af_end_->setValue(10000);
    af_step_ = new QSpinBox(afBody);
    af_step_->setRange(1, 1000);
    af_step_->setValue(100);

    afForm->addRow("Start:", af_start_);
    afForm->addRow("End:", af_end_);
    afForm->addRow("Step:", af_step_);
    afLayout->addLayout(afForm);

    autofocus_btn_ = new QPushButton("Run Autofocus", afBody);
    autofocus_btn_->setProperty("primary", true);
    afLayout->addWidget(autofocus_btn_);

    afCard->setLayout(new QVBoxLayout());
    afCard->layout()->setContentsMargins(0, 0, 0, 0);
    afCard->layout()->addWidget(afBody);
    mainLayout->addWidget(afCard);

    mainLayout->addStretch();

    scrollArea->setWidget(scrollContent);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);
}

} // namespace panels
