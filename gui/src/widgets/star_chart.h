#ifndef STAR_CHART_H
#define STAR_CHART_H
#include <QWidget>
class StarChart : public QWidget {
    Q_OBJECT
public:
    explicit StarChart(QWidget* parent = nullptr);
    void setData(const QVector<double>& magnitudes, const QVector<double>& colors);
protected:
    void paintEvent(QPaintEvent* event) override;
};
#endif
