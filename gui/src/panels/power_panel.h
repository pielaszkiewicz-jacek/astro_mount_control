#ifndef POWER_PANEL_H
#define POWER_PANEL_H
#include <QWidget>
#include <QLabel>
namespace panels {
class PowerPanel : public QWidget {
    Q_OBJECT
public:
    explicit PowerPanel(QWidget* parent = nullptr);
private:
    QLabel *voltage_, *current_, *power_, *battery_, *runtime_, *temp_;
    QLabel *source_, *charging_;
};
}
#endif
