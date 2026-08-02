#ifndef MOUNT_PANEL_H
#define MOUNT_PANEL_H
#include <QWidget>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QLabel>
class GrpcClient;
namespace panels {
class MountPanel : public QWidget {
    Q_OBJECT
public:
    explicit MountPanel(QWidget* parent = nullptr);
    void refresh(GrpcClient* client);
private:
    QDoubleSpinBox *ra_spin_, *dec_spin_;
    QLabel *ra_label_, *dec_label_, *state_label_, *tracking_label_;
    QPushButton *slew_btn_, *track_btn_, *stop_btn_, *park_btn_;
};
}
#endif
