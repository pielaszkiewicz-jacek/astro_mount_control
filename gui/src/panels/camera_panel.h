#ifndef CAMERA_PANEL_H
#define CAMERA_PANEL_H
#include <QWidget>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QProgressBar>
namespace panels {
class CameraPanel : public QWidget {
    Q_OBJECT
public:
    explicit CameraPanel(QWidget* parent = nullptr);
private:
    QPushButton *expose_btn_, *abort_btn_;
    QDoubleSpinBox *exposure_spin_;
    QComboBox *binning_combo_, *filter_combo_;
    QLabel *info_label_, *temp_label_, *cooler_label_;
    QProgressBar *exposure_progress_;
};
}
#endif
