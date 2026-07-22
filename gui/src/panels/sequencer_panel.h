#ifndef SEQUENCER_PANEL_H
#define SEQUENCER_PANEL_H
#include <QWidget>
#include <QPushButton>
#include <QProgressBar>
#include <QListWidget>
#include <QLabel>
namespace panels {
class SequencerPanel : public QWidget {
    Q_OBJECT
public:
    explicit SequencerPanel(QWidget* parent = nullptr);
private:
    QPushButton *start_btn_, *stop_btn_, *pause_btn_, *load_btn_;
    QProgressBar *progress_;
    QListWidget *target_list_;
    QLabel *state_label_, *target_label_, *exposure_label_;
};
}
#endif
