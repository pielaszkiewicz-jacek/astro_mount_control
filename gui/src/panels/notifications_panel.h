#ifndef NOTIFICATIONS_PANEL_H
#define NOTIFICATIONS_PANEL_H
#include <QWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QCheckBox>
#include <QListWidget>
#include <QLabel>
namespace panels {
class NotificationsPanel : public QWidget {
    Q_OBJECT
public:
    explicit NotificationsPanel(QWidget* parent = nullptr);
private:
    QLineEdit *smtp_host_, *smtp_port_, *from_, *to_;
    QCheckBox *tls_;
    QPushButton *test_email_;
    QLineEdit *webhook_url_, *webhook_token_;
    QPushButton *test_webhook_;
    QListWidget *event_list_;
};
}
#endif
