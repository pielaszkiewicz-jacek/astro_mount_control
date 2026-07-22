#ifndef WEATHER_PLOT_H
#define WEATHER_PLOT_H
#include <QWidget>
#include <QVector>
#include <QDateTime>
class WeatherPlot : public QWidget {
    Q_OBJECT
public:
    explicit WeatherPlot(QWidget* parent = nullptr);
    void addReading(QDateTime time, double temp, double humidity, double pressure);
    void setTimeRange(int hours);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    struct Reading { QDateTime time; double temp, humidity, pressure; };
    QVector<Reading> readings_;
    int time_range_hours_{24};
};
#endif
