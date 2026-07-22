#include "main_window.h"
#include "grpc_client.h"
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
#include <QMenuBar>
#include <QStatusBar>
#include <QLabel>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    client_ = new GrpcClient("localhost:50051", this);

    setupUi();

    refresh_timer_ = new QTimer(this);
    connect(refresh_timer_, &QTimer::timeout, this, [this]() {
        status_panel_->refresh(client_);
        mount_panel_->refresh(client_);
    });
    refresh_timer_->start(1000);

    statusBar()->showMessage("Connected to mount controller");
}

MainWindow::~MainWindow() {
    refresh_timer_->stop();
}

void MainWindow::setupUi() {
    tabs_ = new QTabWidget(this);
    setCentralWidget(tabs_);

    mount_panel_ = new panels::MountPanel(this);
    tabs_->addTab(mount_panel_, "🔭 Mount");

    status_panel_ = new panels::StatusPanel(this);
    tabs_->addTab(status_panel_, "📊 Status");

    calibration_panel_ = new panels::CalibrationWizard(this);
    tabs_->addTab(calibration_panel_, "🎯 Calibration");

    sequencer_panel_ = new panels::SequencerPanel(this);
    tabs_->addTab(sequencer_panel_, "📋 Sequencer");

    focuser_panel_ = new panels::FocuserPanel(this);
    tabs_->addTab(focuser_panel_, "🔍 Focuser");

    camera_panel_ = new panels::CameraPanel(this);
    tabs_->addTab(camera_panel_, "📷 Camera");

    dome_panel_ = new panels::DomePanel(this);
    tabs_->addTab(dome_panel_, "🏠 Dome");

    weather_panel_ = new panels::WeatherPanel(this);
    tabs_->addTab(weather_panel_, "🌤️ Weather");

    derotator_panel_ = new panels::DerotatorPanel(this);
    tabs_->addTab(derotator_panel_, "🔄 Derotator");

    pec_panel_ = new panels::PecPanel(this);
    tabs_->addTab(pec_panel_, "⚙ PEC");

    power_panel_ = new panels::PowerPanel(this);
    tabs_->addTab(power_panel_, "🔋 Power");

    notifications_panel_ = new panels::NotificationsPanel(this);
    tabs_->addTab(notifications_panel_, "🔔 Notifications");
}
