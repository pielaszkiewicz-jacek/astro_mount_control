#include "panels/settings_dialog.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QScrollArea>

namespace panels {

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Settings");
    resize(550, 450);
    setMinimumSize(500, 400);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    // Connection Settings Card
    auto* connCard = new QGroupBox("Connection", this);
    auto* connBody = new QWidget(connCard);
    connBody->setObjectName("cardBody");
    auto* connLayout = new QFormLayout(connBody);
    connLayout->setSpacing(8);

    grpc_host_ = new QLineEdit(connBody);
    grpc_host_->setPlaceholderText("localhost");
    grpc_host_->setText("localhost");
    connLayout->addRow("gRPC Host:", grpc_host_);

    grpc_port_ = new QLineEdit(connBody);
    grpc_port_->setPlaceholderText("50051");
    grpc_port_->setText("50051");
    connLayout->addRow("gRPC Port:", grpc_port_);

    ssl_ = new QCheckBox("Enable SSL/TLS", connBody);
    connLayout->addRow("", ssl_);

    ssl_cert_ = new QLineEdit(connBody);
    ssl_cert_->setPlaceholderText("Path to certificate file");
    connLayout->addRow("SSL Certificate:", ssl_cert_);

    ssl_key_ = new QLineEdit(connBody);
    ssl_key_->setPlaceholderText("Path to private key file");
    connLayout->addRow("SSL Key:", ssl_key_);

    connCard->setLayout(new QVBoxLayout());
    connCard->layout()->setContentsMargins(0, 0, 0, 0);
    connCard->layout()->addWidget(connBody);
    mainLayout->addWidget(connCard);

    // Logging Settings Card
    auto* logCard = new QGroupBox("Logging", this);
    auto* logBody = new QWidget(logCard);
    logBody->setObjectName("cardBody");
    auto* logLayout = new QFormLayout(logBody);
    logLayout->setSpacing(8);

    log_level_ = new QLineEdit(logBody);
    log_level_->setPlaceholderText("info");
    log_level_->setText("info");
    logLayout->addRow("Log Level:", log_level_);

    log_dir_ = new QLineEdit(logBody);
    log_dir_->setPlaceholderText("/var/log/astro_mount");
    log_dir_->setText("/var/log/astro_mount");
    logLayout->addRow("Log Directory:", log_dir_);

    logCard->setLayout(new QVBoxLayout());
    logCard->layout()->setContentsMargins(0, 0, 0, 0);
    logCard->layout()->addWidget(logBody);
    mainLayout->addWidget(logCard);

    // Dialog buttons
    auto* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    mainLayout->addStretch();
}

} // namespace panels
