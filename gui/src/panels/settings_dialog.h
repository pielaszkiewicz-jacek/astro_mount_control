#ifndef SETTINGS_DIALOG_H
#define SETTINGS_DIALOG_H
#include <QDialog>
#include <QTabWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
namespace panels {
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);
private:
    QTabWidget* tabs_;
    QLineEdit *grpc_host_, *grpc_port_;
    QCheckBox *ssl_;
    QLineEdit *ssl_cert_, *ssl_key_;
    QLineEdit *log_level_, *log_dir_;
    QSpinBox *log_rotation_;
};
}
#endif
