#ifndef PEC_PANEL_H
#define PEC_PANEL_H
#include <QWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QLabel>
#include <QProgressBar>
namespace panels {
class PecPanel : public QWidget {
    Q_OBJECT
public:
    explicit PecPanel(QWidget* parent = nullptr);
private:
    QCheckBox *enable_;
    QPushButton *train_btn_, *stop_btn_, *save_btn_, *load_btn_;
    QSpinBox *worm_cycle_, *harmonics_;
    QLabel *status_label_, *peak_label_, *rms_label_;
    QProgressBar *training_progress_;
};
}
#endif
