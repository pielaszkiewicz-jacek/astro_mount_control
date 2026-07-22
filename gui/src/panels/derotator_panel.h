#ifndef DEROTATOR_PANEL_H
#define DEROTATOR_PANEL_H
#include <QWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QDoubleSpinBox>
#include <QLabel>
namespace panels {
class DerotatorPanel : public QWidget {
    Q_OBJECT
public:
    explicit DerotatorPanel(QWidget* parent = nullptr);
private:
    QRadioButton *disabled_, *auto_, *fixed_, *manual_;
    QDoubleSpinBox *angle_spin_, *rate_spin_;
    QPushButton *set_angle_btn_, *set_rate_btn_, *home_btn_;
    QLabel *pos_label_, *rate_label_, *homed_label_;
};
}
#endif
