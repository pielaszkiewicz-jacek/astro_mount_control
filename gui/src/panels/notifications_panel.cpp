#include "panels/notifications_panel.h"
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

NotificationsPanel::NotificationsPanel(QWidget* parent) : QWidget(parent) {
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* scrollContent = new QWidget(scrollArea);
    auto* mainLayout = new QVBoxLayout(scrollContent);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    // Email Notifications Card
    auto* emailCard = makeCard("Email Notifications", scrollContent);
    auto* emailBody = makeCardBody(emailCard);
    auto* emailLayout = new QVBoxLayout(emailBody);
    emailLayout->setSpacing(10);
    emailLayout->addWidget(makeDesc("SMTP server settings for sending alert emails on mount events.", emailBody));

    auto* emailForm = new QFormLayout();
    smtp_host_ = new QLineEdit(emailBody);
    smtp_host_->setPlaceholderText("smtp.example.com");
    emailForm->addRow("SMTP Host:", smtp_host_);

    smtp_port_ = new QLineEdit(emailBody);
    smtp_port_->setPlaceholderText("587");
    emailForm->addRow("SMTP Port:", smtp_port_);

    from_ = new QLineEdit(emailBody);
    from_->setPlaceholderText("from@example.com");
    emailForm->addRow("From:", from_);

    to_ = new QLineEdit(emailBody);
    to_->setPlaceholderText("to@example.com");
    emailForm->addRow("To:", to_);
    emailLayout->addLayout(emailForm);

    tls_ = new QCheckBox("Enable TLS", emailBody);
    emailLayout->addWidget(tls_);

    test_email_ = new QPushButton("Send Test Email", emailBody);
    test_email_->setProperty("primary", true);
    emailLayout->addWidget(test_email_);

    emailCard->setLayout(new QVBoxLayout());
    emailCard->layout()->setContentsMargins(0, 0, 0, 0);
    emailCard->layout()->addWidget(emailBody);
    mainLayout->addWidget(emailCard);

    // Webhook Notifications Card
    auto* webhookCard = makeCard("Webhook Notifications", scrollContent);
    auto* webhookBody = makeCardBody(webhookCard);
    auto* webhookLayout = new QVBoxLayout(webhookBody);
    webhookLayout->setSpacing(10);
    webhookLayout->addWidget(makeDesc("HTTP webhook endpoint for integrating with external services.", webhookBody));

    auto* webhookForm = new QFormLayout();
    webhook_url_ = new QLineEdit(webhookBody);
    webhook_url_->setPlaceholderText("https://hooks.example.com/...");
    webhookForm->addRow("Webhook URL:", webhook_url_);

    webhook_token_ = new QLineEdit(webhookBody);
    webhook_token_->setPlaceholderText("token");
    webhook_token_->setEchoMode(QLineEdit::Password);
    webhookForm->addRow("Token:", webhook_token_);
    webhookLayout->addLayout(webhookForm);

    test_webhook_ = new QPushButton("Send Test Webhook", webhookBody);
    test_webhook_->setProperty("primary", true);
    webhookLayout->addWidget(test_webhook_);

    webhookCard->setLayout(new QVBoxLayout());
    webhookCard->layout()->setContentsMargins(0, 0, 0, 0);
    webhookCard->layout()->addWidget(webhookBody);
    mainLayout->addWidget(webhookCard);

    // Event List Card
    auto* eventCard = makeCard("Notification Events", scrollContent);
    auto* eventBody = makeCardBody(eventCard);
    auto* eventLayout = new QVBoxLayout(eventBody);
    eventLayout->addWidget(makeDesc("List of events that can trigger notifications.", eventBody));

    event_list_ = new QListWidget(eventBody);
    event_list_->setMinimumHeight(150);
    eventLayout->addWidget(event_list_);

    eventCard->setLayout(new QVBoxLayout());
    eventCard->layout()->setContentsMargins(0, 0, 0, 0);
    eventCard->layout()->addWidget(eventBody);
    mainLayout->addWidget(eventCard);

    mainLayout->addStretch();

    scrollArea->setWidget(scrollContent);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);
}

} // namespace panels
