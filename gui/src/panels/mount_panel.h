#ifndef MOUNT_PANEL_H
#define MOUNT_PANEL_H
#include <QWidget>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSlider>
#include <QGroupBox>
#include <QLineEdit>
#include <QCheckBox>
class GrpcClient;
namespace panels {
class MountPanel : public QWidget {
    Q_OBJECT
public:
    explicit MountPanel(QWidget* parent = nullptr);
    void refresh(GrpcClient* client);
    void setGrpcClient(GrpcClient* client);

private slots:
    void onAxisButtonPressed(int axis_id, int direction);
    void onAxisButtonReleased(int axis_id);

private:
    void connectAxisButtons();
    GrpcClient* grpc_client_ = nullptr;
    // Slew controls
    QLineEdit *ra_input_, *dec_input_;
    QPushButton *slew_btn_, *slew_track_btn_, *stop_btn_, *park_btn_, *unpark_btn_;
    QPushButton *clear_errors_btn_, *home_btn_;

    // Axis directional pad
    QPushButton *btn_axis_up_, *btn_axis_down_, *btn_axis_left_, *btn_axis_right_;
    QPushButton *btn_emergency_stop_;

    // Speed / acceleration controls
    QSlider *speed_slider_, *accel_slider_, *decel_slider_;
    QLabel *speed_label_, *accel_label_, *decel_label_;
    QCheckBox *axis_speed_ref_toggle_;

    // Axis mode (velocity / step)
    QPushButton *axis_mode_toggle_;
    QLabel *axis_mode_label_;
    QDoubleSpinBox *axis_step_size_;

    // State
    QLabel *mount_state_label_;
    QLabel *tracking_label_;
};
}
#endif
