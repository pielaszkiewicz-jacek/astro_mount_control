#ifndef DATABASE_PANEL_H
#define DATABASE_PANEL_H
#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QTableWidget>
#include <QSpinBox>
#include <QTimer>

class DbGrpcClient;

namespace panels {

class DatabasePanel : public QWidget {
    Q_OBJECT
public:
    explicit DatabasePanel(QWidget* parent = nullptr);

    /// Set the database gRPC client and connect
    void setDbClient(DbGrpcClient* client);

private slots:
    void onSearch();
    void onPageChanged(int page);
    void onObjectSelected(int row, int col);
    void onConnect();
    void refreshStats();

private:
    void setupUi();
    void displayResults(const QList<QStringList>& rows, int totalCount);

    // Search/filter
    QLineEdit* search_input_;
    QComboBox* type_filter_;
    QDoubleSpinBox* mag_min_, *mag_max_;
    QComboBox* sort_by_;
    QCheckBox* sort_desc_;
    QComboBox* constellation_filter_;
    QPushButton* search_btn_;

    // Connection
    QLineEdit* db_host_input_;
    QLineEdit* db_port_input_;
    QPushButton* db_connect_btn_;

    // Results table
    QTableWidget* results_table_;

    // Pagination
    QSpinBox* page_size_;
    QLabel* page_info_;
    QPushButton* prev_btn_, *next_btn_;

    // Detail panel
    QWidget* detail_widget_;
    QLabel *detail_name_, *detail_type_, *detail_ra_, *detail_dec_;
    QLabel *detail_mag_, *detail_constellation_, *detail_catalog_;
    QLabel *detail_size_, *detail_distance_;
    QPushButton *slew_btn_, *close_detail_btn_;

    // Stats
    QLabel* stats_label_;

    // gRPC client
    DbGrpcClient* db_client_ = nullptr;

    // State
    int current_page_ = 1;
    int total_pages_ = 1;
    int total_objects_ = 0;
};

} // namespace panels
#endif
