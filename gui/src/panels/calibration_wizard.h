#ifndef CALIBRATION_WIZARD_H
#define CALIBRATION_WIZARD_H
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
namespace panels {
class CalibrationWizard : public QWidget {
    Q_OBJECT
public:
    explicit CalibrationWizard(QWidget* parent = nullptr);
private:
    QPushButton *add_meas_btn_, *run_bootstrap_btn_, *run_tpoint_btn_, *clear_btn_;
    QTableWidget *measurements_table_;
    QLabel *bootstrap_status_, *tpoint_status_;
};
}
#endif
