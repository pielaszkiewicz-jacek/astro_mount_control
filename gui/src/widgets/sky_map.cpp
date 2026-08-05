#include "widgets/sky_map.h"
#include <QWheelEvent>
#include <QMouseEvent>

SkyMap::SkyMap(QWidget* parent) : QGraphicsView(parent) {}
void SkyMap::setLocation(double, double) {}
void SkyMap::setTime(double) {}
void SkyMap::drawStars() {}
void SkyMap::wheelEvent(QWheelEvent* event) { QGraphicsView::wheelEvent(event); }
void SkyMap::mouseMoveEvent(QMouseEvent* event) { QGraphicsView::mouseMoveEvent(event); }
