#include "widgets/star_chart.h"
#include <QPainter>

StarChart::StarChart(QWidget* parent) : QWidget(parent) {}
void StarChart::setData(const QVector<double>&, const QVector<double>&) {}
void StarChart::paintEvent(QPaintEvent*) {}
