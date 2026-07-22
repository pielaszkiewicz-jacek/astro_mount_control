#ifndef FOCUS_GRAPH_H
#define FOCUS_GRAPH_H
#include <QWidget>
#include <QVector>
class FocusGraph : public QWidget {
    Q_OBJECT
public:
    explicit FocusGraph(QWidget* parent = nullptr);
    void addPoint(int position, double hfd);
    void clear();
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    QVector<int> positions_;
    QVector<double> hfds_;
    int best_position_{0};
    double best_hfd_{0};
};
#endif
