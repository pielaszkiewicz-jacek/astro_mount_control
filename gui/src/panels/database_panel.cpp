#include "panels/database_panel.h"
#include "db_grpc_client.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QLabel>
#include <QHeaderView>
#include <QMessageBox>

namespace panels {

// ── Helpers ──────────────────────────────────────────────────────────────

static QGroupBox* makeCard(const QString& title, QWidget* parent) {
    auto* card = new QGroupBox(title, parent);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    return card;
}

static QWidget* makeCardBody(QWidget* parent) {
    auto* body = new QWidget(parent);
    body->setObjectName("cardBody");
    return body;
}

static QLabel* makeDesc(const QString& text, QWidget* parent) {
    auto* lbl = new QLabel(text, parent);
    lbl->setWordWrap(true);
    lbl->setStyleSheet(
        "color: #6c6c80; font-size: 11px; padding: 2px 0 6px 0; "
        "border-bottom: 1px solid rgba(255,255,255,0.04); margin-bottom: 2px;");
    return lbl;
}

static QString objTypeToString(astro_objects::ObjectType t) {
    switch (t) {
        case astro_objects::STAR: return "Star";
        case astro_objects::DOUBLE_STAR: return "Double Star";
        case astro_objects::VARIABLE_STAR: return "Variable Star";
        case astro_objects::STAR_CLUSTER_OPEN: return "Open Cluster";
        case astro_objects::STAR_CLUSTER_GLOBULAR: return "Globular Cluster";
        case astro_objects::PLANETARY_NEBULA: return "Planetary Nebula";
        case astro_objects::DIFFUSE_NEBULA: return "Diffuse Nebula";
        case astro_objects::DARK_NEBULA: return "Dark Nebula";
        case astro_objects::EMISSION_NEBULA: return "Emission Nebula";
        case astro_objects::REFLECTION_NEBULA: return "Reflection Nebula";
        case astro_objects::GALAXY_SPIRAL: return "Spiral Galaxy";
        case astro_objects::GALAXY_ELLIPTICAL: return "Elliptical Galaxy";
        case astro_objects::GALAXY_IRREGULAR: return "Irregular Galaxy";
        case astro_objects::GALAXY_LENTICULAR: return "Lenticular Galaxy";
        case astro_objects::QUASAR: return "Quasar";
        case astro_objects::SUPERNOVA_REMNANT: return "Supernova Remnant";
        case astro_objects::PLANET: return "Planet";
        case astro_objects::DWARF_PLANET: return "Dwarf Planet";
        case astro_objects::MOON: return "Moon";
        case astro_objects::ASTEROID: return "Asteroid";
        case astro_objects::COMET: return "Comet";
        case astro_objects::SATELLITE: return "Satellite";
        case astro_objects::EXOPLANET: return "Exoplanet";
        case astro_objects::ARTIFICIAL_SATELLITE: return "Artificial Satellite";
        case astro_objects::SPACE_DEBRIS: return "Space Debris";
        default: return "Unknown";
    }
}

static astro_objects::ObjectType stringToObjType(const QString& s) {
    if (s == "Star") return astro_objects::STAR;
    if (s == "Double Star") return astro_objects::DOUBLE_STAR;
    if (s == "Open Cluster") return astro_objects::STAR_CLUSTER_OPEN;
    if (s == "Globular Cluster") return astro_objects::STAR_CLUSTER_GLOBULAR;
    if (s == "Planetary Nebula") return astro_objects::PLANETARY_NEBULA;
    if (s == "Nebula") return astro_objects::DIFFUSE_NEBULA;
    if (s == "Galaxy") return astro_objects::GALAXY_SPIRAL;
    if (s == "Supernova Remnant") return astro_objects::SUPERNOVA_REMNANT;
    if (s == "Planet") return astro_objects::PLANET;
    if (s == "Comet") return astro_objects::COMET;
    if (s == "Asteroid") return astro_objects::ASTEROID;
    return astro_objects::UNKNOWN_TYPE;
}

// ── DatabasePanel ────────────────────────────────────────────────────────

DatabasePanel::DatabasePanel(QWidget* parent) : QWidget(parent) {
    setupUi();
}

void DatabasePanel::setDbClient(DbGrpcClient* client) {
    db_client_ = client;
    if (db_client_) {
        stats_label_->setText("Connected to " + QString::fromStdString(db_client_->address()));
        refreshStats();
        onSearch();
    }
}

void DatabasePanel::onConnect() {
    QString host = db_host_input_->text().trimmed();
    QString port = db_port_input_->text().trimmed();
    if (host.isEmpty()) host = "localhost";
    if (port.isEmpty()) port = "50052";

    std::string address = host.toStdString() + ":" + port.toStdString();
    if (db_client_) {
        db_client_->reconnect(address);
    }
    stats_label_->setText("Connecting to " + QString::fromStdString(address) + "...");
    refreshStats();
}

void DatabasePanel::refreshStats() {
    if (!db_client_) {
        stats_label_->setText("No database connection configured.");
        return;
    }
    try {
        auto stats = db_client_->getStats();
        stats_label_->setText(QString("Objects: %1 | Favorites: %2 | Catalogs: %3")
            .arg(stats.total_objects())
            .arg(stats.favorite_count())
            .arg(stats.objects_by_catalog_size()));
    } catch (const std::exception& e) {
        stats_label_->setText(QString("DB error: %1").arg(e.what()));
    }
}

void DatabasePanel::onSearch() {
    current_page_ = 1;
    onPageChanged(1);
}

void DatabasePanel::displayResults(const QList<QStringList>& rows, int totalCount) {
    int pageSize = page_size_->value();
    total_objects_ = totalCount;
    total_pages_ = qMax(1, (total_objects_ + pageSize - 1) / pageSize);

    page_info_->setText(QString("Page %1 of %2 (%3 objects)")
        .arg(current_page_).arg(total_pages_).arg(total_objects_));
    prev_btn_->setEnabled(current_page_ > 1);
    next_btn_->setEnabled(current_page_ < total_pages_);

    results_table_->setRowCount(0);
    results_table_->setRowCount(rows.size());

    for (int i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        for (int j = 0; j < r.size() && j < 6; ++j) {
            results_table_->setItem(i, j, new QTableWidgetItem(r[j]));
        }
    }
}

void DatabasePanel::onPageChanged(int page) {
    current_page_ = page;
    if (!db_client_) {
        stats_label_->setText("Not connected to database. Configure host/port and click Connect.");
        return;
    }

    try {
        QString name = search_input_->text().trimmed();
        QString typeFilter = type_filter_->currentData().toString();
        int pageSize = page_size_->value();
        QString sortBy = sort_by_->currentText().toLower();
        bool sortDesc = sort_desc_->isChecked();
        double minMag = mag_min_->value();
        double maxMag = mag_max_->value();
        QString constellation = constellation_filter_->currentData().toString();

        astro_objects::ObjectList result;

        if (!name.isEmpty() || !constellation.isEmpty()) {
            // Use SearchObjects for text/constellation queries
            astro_objects::ObjectSearchRequest sreq;
            sreq.set_query(name.toStdString());
            if (!typeFilter.isEmpty())
                sreq.set_object_type(stringToObjType(typeFilter));
            sreq.set_min_magnitude(minMag);
            sreq.set_max_magnitude(maxMag);
            if (!constellation.isEmpty())
                sreq.set_constellation(constellation.toStdString());
            result = db_client_->searchObjects(sreq);
        } else {
            // Use ListObjects for browsing with filters
            astro_objects::ObjectListRequest lreq;
            lreq.set_page(page);
            lreq.set_page_size(pageSize);
            lreq.set_sort_by(sortBy.toStdString());
            lreq.set_sort_descending(sortDesc);
            if (!typeFilter.isEmpty())
                lreq.set_filter_type(stringToObjType(typeFilter));
            lreq.set_min_magnitude(minMag);
            lreq.set_max_magnitude(maxMag);
            result = db_client_->listObjects(lreq);
        }

        QList<QStringList> rows;
        for (int i = 0; i < result.objects_size(); ++i) {
            const auto& obj = result.objects(i);
            QStringList row;
            row << QString::fromStdString(obj.name());
            row << objTypeToString(obj.object_type());
            row << (obj.v_magnitude() != 0 ? QString::number(obj.v_magnitude(), 'f', 2) : "--");
            row << "--"; // constellation not in proto response
            row << QString::number(obj.ra_hours(), 'f', 4) + "h";
            row << QString::number(obj.dec_degrees(), 'f', 3) + "°";
            rows.append(row);
        }

        displayResults(rows, result.total_count());
    } catch (const std::exception& e) {
        stats_label_->setText(QString("Query error: %1").arg(e.what()));
    }
}

void DatabasePanel::onObjectSelected(int row, int /*col*/) {
    if (row < 0 || row >= results_table_->rowCount()) return;

    auto* nameItem = results_table_->item(row, 0);
    auto* typeItem = results_table_->item(row, 1);
    auto* magItem = results_table_->item(row, 2);
    auto* constItem = results_table_->item(row, 3);
    auto* raItem = results_table_->item(row, 4);
    auto* decItem = results_table_->item(row, 5);

    if (!nameItem) return;

    detail_name_->setText(nameItem->text());
    detail_type_->setText(typeItem ? typeItem->text() : "--");
    detail_mag_->setText(magItem ? magItem->text() : "--");
    detail_constellation_->setText(constItem ? constItem->text() : "--");
    detail_ra_->setText(raItem ? raItem->text() : "--");
    detail_dec_->setText(decItem ? decItem->text() : "--");
    detail_catalog_->setText("--");
    detail_size_->setText("--");
    detail_distance_->setText("--");

    detail_widget_->parentWidget()->parentWidget()->setVisible(true);
}

void DatabasePanel::setupUi() {
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* scrollContent = new QWidget(scrollArea);
    auto* mainLayout = new QVBoxLayout(scrollContent);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    // ════════════════════════════════════════════════════════════════
    // Database Connection & Stats Card
    // ════════════════════════════════════════════════════════════════
    {
        auto* card = makeCard("Database Connection", scrollContent);
        auto* body = makeCardBody(card);
        auto* lay = new QVBoxLayout(body);
        lay->addWidget(makeDesc("Connect to the object database gRPC service. "
                                "Enter the server address and port, then click Connect.", body));

        auto* connRow = new QHBoxLayout();
        connRow->addWidget(new QLabel("Host:", body));
        db_host_input_ = new QLineEdit(body);
        db_host_input_->setPlaceholderText("localhost");
        db_host_input_->setText("localhost");
        db_host_input_->setFixedWidth(140);
        connRow->addWidget(db_host_input_);

        connRow->addWidget(new QLabel("Port:", body));
        db_port_input_ = new QLineEdit(body);
        db_port_input_->setPlaceholderText("50052");
        db_port_input_->setText("50052");
        db_port_input_->setFixedWidth(60);
        connRow->addWidget(db_port_input_);

        db_connect_btn_ = new QPushButton("Connect", body);
        db_connect_btn_->setProperty("primary", true);
        connect(db_connect_btn_, &QPushButton::clicked, this, &DatabasePanel::onConnect);
        connRow->addWidget(db_connect_btn_);
        connRow->addStretch();
        lay->addLayout(connRow);

        stats_label_ = new QLabel("Not connected. Configure host/port and click Connect.", body);
        stats_label_->setStyleSheet("font-size: 12px; color: #a0a0b0; padding: 4px 0;");
        lay->addWidget(stats_label_);

        card->setLayout(new QVBoxLayout());
        card->layout()->setContentsMargins(0, 0, 0, 0);
        card->layout()->addWidget(body);
        mainLayout->addWidget(card);
    }

    // ════════════════════════════════════════════════════════════════
    // Search & Filter Card
    // ════════════════════════════════════════════════════════════════
    {
        auto* card = makeCard("Search Objects", scrollContent);
        auto* body = makeCardBody(card);
        auto* lay = new QVBoxLayout(body);
        lay->addWidget(makeDesc("Search the astronomical object database by name, type, magnitude, "
                                "and constellation.", body));

        auto* searchRow = new QHBoxLayout();
        search_input_ = new QLineEdit(body);
        search_input_->setPlaceholderText("Search by name or catalog ID...");
        search_btn_ = new QPushButton("Search", body);
        search_btn_->setProperty("primary", true);
        connect(search_btn_, &QPushButton::clicked, this, &DatabasePanel::onSearch);
        connect(search_input_, &QLineEdit::returnPressed, this, &DatabasePanel::onSearch);
        searchRow->addWidget(search_input_);
        searchRow->addWidget(search_btn_);
        lay->addLayout(searchRow);

        auto* filterRow1 = new QHBoxLayout();
        type_filter_ = new QComboBox(body);
        type_filter_->addItem("All Types", "");
        type_filter_->addItem("Star", "Star");
        type_filter_->addItem("Double Star", "Double Star");
        type_filter_->addItem("Open Cluster", "Open Cluster");
        type_filter_->addItem("Globular Cluster", "Globular Cluster");
        type_filter_->addItem("Planetary Nebula", "Planetary Nebula");
        type_filter_->addItem("Nebula", "Nebula");
        type_filter_->addItem("Galaxy", "Galaxy");
        type_filter_->addItem("Supernova Remnant", "Supernova Remnant");
        type_filter_->addItem("Planet", "Planet");
        type_filter_->addItem("Comet", "Comet");
        type_filter_->addItem("Asteroid", "Asteroid");

        sort_by_ = new QComboBox(body);
        sort_by_->addItems({"Name", "Magnitude", "RA", "Dec"});
        sort_desc_ = new QCheckBox("Descending", body);

        filterRow1->addWidget(new QLabel("Type:", body));
        filterRow1->addWidget(type_filter_);
        filterRow1->addWidget(new QLabel("Sort:", body));
        filterRow1->addWidget(sort_by_);
        filterRow1->addWidget(sort_desc_);
        lay->addLayout(filterRow1);

        auto* filterRow2 = new QHBoxLayout();
        mag_min_ = new QDoubleSpinBox(body);
        mag_min_->setRange(-30, 30);
        mag_min_->setValue(-30);
        mag_min_->setDecimals(1);
        mag_min_->setPrefix("Min mag: ");
        mag_max_ = new QDoubleSpinBox(body);
        mag_max_->setRange(-30, 30);
        mag_max_->setValue(30);
        mag_max_->setDecimals(1);
        mag_max_->setPrefix("Max mag: ");

        constellation_filter_ = new QComboBox(body);
        constellation_filter_->addItem("All Constellations", "");
        const QStringList consts = {
            "AND","ANT","APS","AQR","AQL","ARA","ARI","AUR","BOO","CAE","CAM","CNC",
            "CVN","CMA","CMI","CAP","CAR","CAS","CEN","CEP","CET","CHA","CIR","COL",
            "COM","CRA","CRB","CRV","CRT","CRU","CYG","DEL","DOR","DRA","EQU","ERI",
            "FOR","GEM","GRU","HER","HOR","HYA","HYI","IND","LAC","LEO","LMI","LEP",
            "LIB","LUP","LYN","LYR","MEN","MIC","MON","MUS","NOR","OCT","OPH","ORI",
            "PAV","PEG","PER","PHE","PIC","PSC","PSA","PUP","PYX","RET","SGE","SGR",
            "SCO","SCL","SCT","SER","SEX","TAU","TEL","TRI","TRA","TUC","UMA","UMI",
            "VEL","VIR","VOL","VUL"};
        for (const auto& c : consts) constellation_filter_->addItem(c, c);

        filterRow2->addWidget(mag_min_);
        filterRow2->addWidget(mag_max_);
        filterRow2->addWidget(new QLabel("Const:", body));
        filterRow2->addWidget(constellation_filter_);
        filterRow2->addStretch();
        lay->addLayout(filterRow2);

        card->setLayout(new QVBoxLayout());
        card->layout()->setContentsMargins(0, 0, 0, 0);
        card->layout()->addWidget(body);
        mainLayout->addWidget(card);
    }

    // ════════════════════════════════════════════════════════════════
    // Objects List Card
    // ════════════════════════════════════════════════════════════════
    {
        auto* card = makeCard("Objects", scrollContent);
        auto* body = makeCardBody(card);
        auto* lay = new QVBoxLayout(body);
        lay->addWidget(makeDesc("Search results. Click a row to view object details.", body));

        results_table_ = new QTableWidget(0, 6, body);
        results_table_->setHorizontalHeaderLabels({"Name", "Type", "Mag", "Const", "RA", "Dec"});
        results_table_->horizontalHeader()->setStretchLastSection(true);
        results_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        results_table_->setSelectionMode(QAbstractItemView::SingleSelection);
        results_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        results_table_->setMinimumHeight(250);
        results_table_->verticalHeader()->setVisible(false);
        connect(results_table_, &QTableWidget::cellClicked,
                this, &DatabasePanel::onObjectSelected);
        lay->addWidget(results_table_);

        auto* pagRow = new QHBoxLayout();
        page_size_ = new QSpinBox(body);
        page_size_->setRange(10, 100);
        page_size_->setValue(20);
        page_size_->setPrefix("Per page: ");
        connect(page_size_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int) { onPageChanged(1); });

        prev_btn_ = new QPushButton("← Prev", body);
        prev_btn_->setEnabled(false);
        next_btn_ = new QPushButton("Next →", body);
        next_btn_->setEnabled(false);
        page_info_ = new QLabel("Page 1 of 1 (0 objects)", body);
        page_info_->setStyleSheet("color: #a0a0b0; font-size: 11px;");

        connect(prev_btn_, &QPushButton::clicked, this, [this]() {
            if (current_page_ > 1) onPageChanged(current_page_ - 1);
        });
        connect(next_btn_, &QPushButton::clicked, this, [this]() {
            if (current_page_ < total_pages_) onPageChanged(current_page_ + 1);
        });

        pagRow->addWidget(page_size_);
        pagRow->addStretch();
        pagRow->addWidget(prev_btn_);
        pagRow->addWidget(page_info_);
        pagRow->addWidget(next_btn_);
        lay->addLayout(pagRow);

        card->setLayout(new QVBoxLayout());
        card->layout()->setContentsMargins(0, 0, 0, 0);
        card->layout()->addWidget(body);
        mainLayout->addWidget(card);
    }

    // ════════════════════════════════════════════════════════════════
    // Object Detail Card
    // ════════════════════════════════════════════════════════════════
    {
        auto* card = makeCard("Object Details", scrollContent);
        auto* body = makeCardBody(card);
        detail_widget_ = body;
        auto* lay = new QVBoxLayout(body);
        lay->addWidget(makeDesc("Selected object information.", body));

        auto* grid = new QFormLayout();
        detail_name_ = new QLabel("--", body);
        detail_name_->setStyleSheet("font-size: 14px; font-weight: 700; color: #4fc3f7;");
        detail_type_ = new QLabel("--", body);
        detail_ra_ = new QLabel("--", body);
        detail_ra_->setStyleSheet("font-family: monospace;");
        detail_dec_ = new QLabel("--", body);
        detail_dec_->setStyleSheet("font-family: monospace;");
        detail_mag_ = new QLabel("--", body);
        detail_constellation_ = new QLabel("--", body);
        detail_catalog_ = new QLabel("--", body);
        detail_size_ = new QLabel("--", body);
        detail_distance_ = new QLabel("--", body);

        grid->addRow("Name:", detail_name_);
        grid->addRow("Type:", detail_type_);
        grid->addRow("RA:", detail_ra_);
        grid->addRow("Dec:", detail_dec_);
        grid->addRow("Magnitude:", detail_mag_);
        grid->addRow("Constellation:", detail_constellation_);
        grid->addRow("Catalog:", detail_catalog_);
        grid->addRow("Size:", detail_size_);
        grid->addRow("Distance:", detail_distance_);
        lay->addLayout(grid);

        auto* btnRow = new QHBoxLayout();
        slew_btn_ = new QPushButton("🎯 Slew to Object", body);
        slew_btn_->setProperty("primary", true);
        close_detail_btn_ = new QPushButton("✕ Close", body);
        connect(close_detail_btn_, &QPushButton::clicked, this, [this]() {
            detail_widget_->parentWidget()->parentWidget()->setVisible(false);
        });
        btnRow->addWidget(slew_btn_);
        btnRow->addWidget(close_detail_btn_);
        lay->addLayout(btnRow);

        card->setLayout(new QVBoxLayout());
        card->layout()->setContentsMargins(0, 0, 0, 0);
        card->layout()->addWidget(body);
        mainLayout->addWidget(card);
    }

    mainLayout->addStretch();

    scrollArea->setWidget(scrollContent);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);
}

} // namespace panels
