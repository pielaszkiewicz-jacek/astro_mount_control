#ifndef DOME_PANEL_H
#define DOME_PANEL_H
#include <QWidget>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QLabel>
namespace panels {
class DomePanel : public QWidget {
    Q_OBJECT
public:
    explicit DomePanel(QWidget* parent = nullptr);
private:
    QPushButton *open_btn_, *close_btn_, *rotate_btn_, *park_btn_;
    QDoubleSpinBox *azimuth_spin_;
    QLabel *state_label_, *azimuth_label_, *shutter_label_;
};
}
#endif
