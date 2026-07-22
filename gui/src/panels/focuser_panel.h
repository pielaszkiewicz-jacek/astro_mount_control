#ifndef FOCUSER_PANEL_H
#define FOCUSER_PANEL_H
#include <QWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QSlider>
#include <QLabel>
namespace panels {
class FocuserPanel : public QWidget {
    Q_OBJECT
public:
    explicit FocuserPanel(QWidget* parent = nullptr);
private:
    QPushButton *move_btn_, *halt_btn_, *autofocus_btn_;
    QSpinBox *position_spin_, *af_start_, *af_end_, *af_step_;
    QSlider *speed_slider_;
    QLabel *pos_label_, *temp_label_, *hfd_label_;
};
}
#endif
