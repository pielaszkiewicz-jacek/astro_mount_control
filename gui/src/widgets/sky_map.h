#ifndef SKY_MAP_H
#define SKY_MAP_H
#include <QGraphicsView>
class SkyMap : public QGraphicsView {
    Q_OBJECT
public:
    explicit SkyMap(QWidget* parent = nullptr);
    void setLocation(double lat, double lon);
    void setTime(double jd);
    void drawStars();
protected:
    void wheelEvent(QWheelEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
};
#endif
