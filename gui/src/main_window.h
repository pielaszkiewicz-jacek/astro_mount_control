#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H
#include <QMainWindow>
#include <QTabWidget>
#include <QTimer>
#include <QLabel>
#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>

namespace panels {
class MountPanel; class StatusPanel; class SequencerPanel;
class DomePanel; class FocuserPanel; class CameraPanel;
class WeatherPanel; class DerotatorPanel; class PecPanel;
class PowerPanel; class NotificationsPanel; class CalibrationWizard;
class SettingsPanel; class DatabasePanel;
}

class GrpcClient;
class DbGrpcClient;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onConnectClicked();
    void onFontSizeChanged(int index);

private:
    void setupUi();
    void setupHeader();
    void updateConnectionBadge(bool connected);
    void applyFontSize(int sizePt);

    GrpcClient* client_;
    DbGrpcClient* db_client_;
    QTabWidget* tabs_;
    QTimer* refresh_timer_;
    QWidget* header_bar_;
    QLabel* conn_badge_label_;
    QLabel* conn_badge_dot_;
    QWidget* conn_badge_widget_;

    // Connection bar widgets
    QLineEdit* host_input_;
    QLineEdit* port_input_;
    QPushButton* connect_btn_;

    // Font size control
    QComboBox* font_size_combo_;

    panels::MountPanel* mount_panel_;
    panels::StatusPanel* status_panel_;
    panels::CalibrationWizard* calibration_panel_;
    panels::SequencerPanel* sequencer_panel_;
    panels::DomePanel* dome_panel_;
    panels::FocuserPanel* focuser_panel_;
    panels::CameraPanel* camera_panel_;
    panels::WeatherPanel* weather_panel_;
    panels::DerotatorPanel* derotator_panel_;
    panels::PecPanel* pec_panel_;
    panels::PowerPanel* power_panel_;
    panels::NotificationsPanel* notifications_panel_;
    panels::SettingsPanel* settings_panel_;
    panels::DatabasePanel* database_panel_;
};

#endif
