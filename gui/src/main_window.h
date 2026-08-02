#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H
#include <QMainWindow>
#include <QTabWidget>
#include <QTimer>

namespace panels {
class MountPanel; class StatusPanel; class SequencerPanel;
class DomePanel; class FocuserPanel; class CameraPanel;
class WeatherPanel; class DerotatorPanel; class PecPanel;
class PowerPanel; class NotificationsPanel; class CalibrationWizard;
}
class GrpcClient;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private:
    void setupUi();
    GrpcClient* client_;
    QTabWidget* tabs_;
    QTimer* refresh_timer_;
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
};

#endif
