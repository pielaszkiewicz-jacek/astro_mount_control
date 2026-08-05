#include "widgets/weather_plot.h"
#include <QPainter>

WeatherPlot::WeatherPlot(QWidget* parent) : QWidget(parent) {}
void WeatherPlot::addReading(QDateTime, double, double, double) {}
void WeatherPlot::setTimeRange(int) {}
void WeatherPlot::paintEvent(QPaintEvent*) {}
