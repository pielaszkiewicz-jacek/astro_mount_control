#ifndef STATUS_PANEL_H
#define STATUS_PANEL_H
#include <QWidget>
#include <QLabel>
class GrpcClient;
namespace panels {
class StatusPanel : public QWidget {
    Q_OBJECT
public:
    explicit StatusPanel(QWidget* parent = nullptr);
    void refresh(GrpcClient* client);
private:
    QLabel *axis1_pos_, *axis2_pos_, *axis1_rate_, *axis2_rate_;
    QLabel *tracking_ra_, *tracking_dec_, *error_ra_, *error_dec_;
    QLabel *encoders_, *guider_, *tpoint_;
    QLabel *pier_side_, *meridian_time_, *flip_pending_;
};
}
#endif
