#include "main_window.h"
#include "grpc_client.h"
#include "db_grpc_client.h"
#include <QApplication>
#include "panels/mount_panel.h"
#include "panels/status_panel.h"
#include "panels/calibration_wizard.h"
#include "panels/sequencer_panel.h"
#include "panels/dome_panel.h"
#include "panels/focuser_panel.h"
#include "panels/camera_panel.h"
#include "panels/weather_panel.h"
#include "panels/derotator_panel.h"
#include "panels/pec_panel.h"
#include "panels/power_panel.h"
#include "panels/notifications_panel.h"
#include "panels/settings_panel.h"
#include "panels/database_panel.h"
#include <QMenuBar>
#include <QStatusBar>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QIntValidator>
#include <QFont>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    client_ = new GrpcClient("localhost:50051", this);
    db_client_ = new DbGrpcClient("localhost:50052", this);

    setupUi();

    // Pass the DB client to the database panel
    if (database_panel_) {
        database_panel_->setDbClient(db_client_);
    }

    refresh_timer_ = new QTimer(this);
    connect(refresh_timer_, &QTimer::timeout, this, [this]() {
        try {
            auto state = client_->getState();
            status_panel_->refresh(client_);
            mount_panel_->refresh(client_);
            updateConnectionBadge(true);
            statusBar()->showMessage("Connected to mount controller");
        } catch (const std::exception& e) {
            updateConnectionBadge(false);
            statusBar()->showMessage(QString("Connection error: %1").arg(e.what()));
        } catch (...) {
            updateConnectionBadge(false);
            statusBar()->showMessage("Connection lost");
        }
    });
    refresh_timer_->start(1000);

    statusBar()->showMessage("Astro Mount Control — Ready");
}

MainWindow::~MainWindow() {
    refresh_timer_->stop();
}

void MainWindow::onConnectClicked() {
    QString host = host_input_->text().trimmed();
    QString port = port_input_->text().trimmed();

    if (host.isEmpty()) host = "localhost";
    if (port.isEmpty()) port = "50051";

    std::string address = host.toStdString() + ":" + port.toStdString();
    client_->reconnect(address);
    updateConnectionBadge(false);
    statusBar()->showMessage(QString("Connecting to %1...").arg(QString::fromStdString(address)));
}

void MainWindow::onFontSizeChanged(int index) {
    // Map combo box index to font size in points
    const int sizes[] = {10, 11, 12, 13, 14, 15, 16, 18, 20, 22};
    if (index >= 0 && index < static_cast<int>(sizeof(sizes) / sizeof(sizes[0]))) {
        applyFontSize(sizes[index]);
    }
}

void MainWindow::applyFontSize(int sizePt) {
    QFont f = font();
    f.setPointSize(sizePt);
    qApp->setFont(f);

    // Update the base font size in the stylesheet by reapplying
    // We adjust the global QWidget font-size in a dynamic property
    setStyleSheet(QString(
        "QWidget { font-size: %1px; }"
    ).arg(sizePt));
}

void MainWindow::setupHeader() {
    // Header bar matching web interface design
    header_bar_ = new QWidget(this);
    header_bar_->setObjectName("headerBar");
    auto* headerLayout = new QHBoxLayout(header_bar_);
    headerLayout->setContentsMargins(12, 0, 12, 0);
    headerLayout->setSpacing(8);

    // App title
    auto* titleLabel = new QLabel("Mount Control", header_bar_);
    titleLabel->setObjectName("appTitle");
    headerLayout->addWidget(titleLabel);

    // ─── Connection bar: Host | Port | Connect ─────────────────────────
    auto* hostLabel = new QLabel("Host:", header_bar_);
    hostLabel->setStyleSheet("color: #a0a0b0; font-size: 11px; background: transparent;");
    headerLayout->addWidget(hostLabel);

    host_input_ = new QLineEdit(header_bar_);
    host_input_->setPlaceholderText("localhost");
    host_input_->setText("localhost");
    host_input_->setFixedWidth(130);
    host_input_->setStyleSheet("padding: 3px 6px; font-size: 11px; min-height: 18px;");
    headerLayout->addWidget(host_input_);

    auto* portLabel = new QLabel("Port:", header_bar_);
    portLabel->setStyleSheet("color: #a0a0b0; font-size: 11px; background: transparent;");
    headerLayout->addWidget(portLabel);

    port_input_ = new QLineEdit(header_bar_);
    port_input_->setPlaceholderText("50051");
    port_input_->setText("50051");
    port_input_->setFixedWidth(60);
    port_input_->setValidator(new QIntValidator(1, 65535, port_input_));
    port_input_->setStyleSheet("padding: 3px 6px; font-size: 11px; min-height: 18px;");
    headerLayout->addWidget(port_input_);

    connect_btn_ = new QPushButton("Connect", header_bar_);
    connect_btn_->setProperty("primary", true);
    connect_btn_->setStyleSheet("padding: 4px 12px; font-size: 11px; min-height: 18px;");
    connect(connect_btn_, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    headerLayout->addWidget(connect_btn_);

    headerLayout->addStretch();

    // ─── Font size control ─────────────────────────────────────────────
    auto* fontSizeLabel = new QLabel("Font:", header_bar_);
    fontSizeLabel->setStyleSheet("color: #a0a0b0; font-size: 11px; background: transparent;");
    headerLayout->addWidget(fontSizeLabel);

    font_size_combo_ = new QComboBox(header_bar_);
    font_size_combo_->addItems({"10", "11", "12", "13", "14", "15", "16", "18", "20", "22"});
    font_size_combo_->setCurrentIndex(3); // Default 13pt
    font_size_combo_->setFixedWidth(55);
    font_size_combo_->setStyleSheet("padding: 2px 4px; font-size: 11px; min-height: 18px;");
    connect(font_size_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onFontSizeChanged);
    headerLayout->addWidget(font_size_combo_);

    // ─── Connection badge ──────────────────────────────────────────────
    conn_badge_widget_ = new QWidget(header_bar_);
    conn_badge_widget_->setObjectName("connectionBadge");
    auto* badgeLayout = new QHBoxLayout(conn_badge_widget_);
    badgeLayout->setContentsMargins(6, 3, 8, 3);
    badgeLayout->setSpacing(6);

    conn_badge_dot_ = new QLabel(conn_badge_widget_);
    conn_badge_dot_->setObjectName("badgeDot");
    conn_badge_dot_->setFixedSize(8, 8);
    badgeLayout->addWidget(conn_badge_dot_);

    conn_badge_label_ = new QLabel("Disconnected", conn_badge_widget_);
    badgeLayout->addWidget(conn_badge_label_);

    headerLayout->addWidget(conn_badge_widget_);

    // Initially disconnected
    updateConnectionBadge(false);
}

void MainWindow::updateConnectionBadge(bool connected) {
    if (!conn_badge_widget_) return;
    if (connected) {
        conn_badge_widget_->setProperty("connected", true);
        conn_badge_label_->setText("Connected");
    } else {
        conn_badge_widget_->setProperty("connected", false);
        conn_badge_label_->setText("Disconnected");
    }
    conn_badge_widget_->style()->unpolish(conn_badge_widget_);
    conn_badge_widget_->style()->polish(conn_badge_widget_);
    conn_badge_dot_->style()->unpolish(conn_badge_dot_);
    conn_badge_dot_->style()->polish(conn_badge_dot_);
    conn_badge_label_->style()->unpolish(conn_badge_label_);
    conn_badge_label_->style()->polish(conn_badge_label_);
}

void MainWindow::setupUi() {
    // ─── Header ────────────────────────────────────────────────────────
    setupHeader();

    // ─── Central tab widget ─────────────────────────────────────────────
    tabs_ = new QTabWidget(this);

    // Wrap layout: header on top, tabs below
    auto* centralWidget = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(header_bar_);
    mainLayout->addWidget(tabs_);
    setCentralWidget(centralWidget);

    // ─── Panels (matching web interface tabs) ───────────────────────────

    // Status panel
    status_panel_ = new panels::StatusPanel(this);
    tabs_->addTab(status_panel_, "📊 Status");

    // Control (Mount) panel
    mount_panel_ = new panels::MountPanel(this);
    mount_panel_->setGrpcClient(client_);
    tabs_->addTab(mount_panel_, "🎮 Control");

    // Calibration panel
    calibration_panel_ = new panels::CalibrationWizard(this);
    tabs_->addTab(calibration_panel_, "🎯 Calibration");

    // Database (object catalog)
    database_panel_ = new panels::DatabasePanel(this);
    tabs_->addTab(database_panel_, "🗄️ Database");

    // Sequencer
    sequencer_panel_ = new panels::SequencerPanel(this);
    tabs_->addTab(sequencer_panel_, "📋 Sequencer");

    // Camera
    camera_panel_ = new panels::CameraPanel(this);
    tabs_->addTab(camera_panel_, "📷 Camera");

    // Focuser
    focuser_panel_ = new panels::FocuserPanel(this);
    tabs_->addTab(focuser_panel_, "🔍 Focuser");

    // PEC
    pec_panel_ = new panels::PecPanel(this);
    tabs_->addTab(pec_panel_, "📈 PEC");

    // Derotator
    derotator_panel_ = new panels::DerotatorPanel(this);
    tabs_->addTab(derotator_panel_, "🔄 Derotator");

    // Power
    power_panel_ = new panels::PowerPanel(this);
    tabs_->addTab(power_panel_, "🔋 Power");

    // Dome
    dome_panel_ = new panels::DomePanel(this);
    tabs_->addTab(dome_panel_, "🏠 Dome");

    // Weather
    weather_panel_ = new panels::WeatherPanel(this);
    tabs_->addTab(weather_panel_, "🌤️ Weather");

    // Notifications
    notifications_panel_ = new panels::NotificationsPanel(this);
    tabs_->addTab(notifications_panel_, "🔔 Notifications");

    // Settings (Configuration)
    settings_panel_ = new panels::SettingsPanel(this);
    tabs_->addTab(settings_panel_, "⚙️ Settings");

    // Auto-load config when Settings tab is activated
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
        if (tabs_->widget(index) == settings_panel_) {
            settings_panel_->loadConfig(client_);
        }
    });

    // Wire save buttons in the settings panel
    {
        auto buttons = settings_panel_->findChildren<QPushButton*>();
        for (auto* btn : buttons) {
            if (btn->text().contains("Save")) {
                connect(btn, &QPushButton::clicked, this, [this]() {
                    settings_panel_->saveConfig(client_);
                });
            }
            if (btn->text().contains("Refresh")) {
                connect(btn, &QPushButton::clicked, this, [this]() {
                    settings_panel_->loadConfig(client_);
                });
            }
        }
    }
}
