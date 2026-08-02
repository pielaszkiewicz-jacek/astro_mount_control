#ifndef WEATHER_PANEL_H
#define WEATHER_PANEL_H
#include <QWidget>
#include <QLabel>
namespace panels {
class WeatherPanel : public QWidget {
    Q_OBJECT
public:
    explicit WeatherPanel(QWidget* parent = nullptr);
private:
    QLabel *temp_, *humidity_, *pressure_, *wind_, *rain_, *clouds_;
    QLabel *safety_label_;
};
}
#endif
