#include "object_database_service.h"
#include <grpcpp/grpcpp.h>
#include <sqlite3.h>
#include <uuid/uuid.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/util/time_util.h>

namespace astro_objects {

namespace {

const char* CREATE_OBJECTS_TABLE = R"(
CREATE TABLE IF NOT EXISTS astronomical_objects (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    catalog_name TEXT,
    alternate_names TEXT,
    ra_hours REAL,
    dec_degrees REAL,
    pm_ra REAL,
    pm_dec REAL,
    parallax_mas REAL,
    distance_pc REAL,
    distance_ly REAL,
    v_magnitude REAL,
    b_magnitude REAL,
    j_magnitude REAL,
    h_magnitude REAL,
    k_magnitude REAL,
    spectral_type TEXT,
    luminosity_class TEXT,
    object_type INTEGER,
    mass_solar REAL,
    radius_solar REAL,
    temperature_k REAL,
    luminosity_solar REAL,
    age_gyr REAL,
    semi_major_axis_au REAL,
    eccentricity REAL,
    inclination_deg REAL,
    longitude_asc_node_deg REAL,
    argument_perihelion_deg REAL,
    mean_anomaly_deg REAL,
    epoch_of_elements_jd REAL,
    diameter_km REAL,
    albedo REAL,
    rotation_period_hours REAL,
    angular_size_arcmin REAL,
    redshift REAL,
    radial_velocity_kms REAL,
    apparent_dimensions_arcmin_x REAL,
    apparent_dimensions_arcmin_y REAL,
    ra_error_mas REAL,
    dec_error_mas REAL,
    pm_ra_error REAL,
    pm_dec_error REAL,
    parallax_error REAL,
    catalog_id TEXT,
    catalog_version TEXT,
    data_source TEXT,
    created_at TEXT,
    updated_at TEXT,
    created_by TEXT,
    notes TEXT,
    observation_count INTEGER,
    last_observed_jd REAL,
    is_favorite INTEGER DEFAULT 0,
    is_visible INTEGER DEFAULT 0,
    has_ephemeris INTEGER DEFAULT 0,
    has_light_curve INTEGER DEFAULT 0,
    has_spectrum INTEGER DEFAULT 0,
    user_rating REAL,
    user_notes TEXT,
    tags TEXT,
    categories TEXT,
    custom_fields TEXT
))";

const char* CREATE_FAVORITES_TABLE = R"(
CREATE TABLE IF NOT EXISTS favorites (
    user_id TEXT,
    object_id TEXT,
    added_at TEXT,
    PRIMARY KEY (user_id, object_id),
    FOREIGN KEY (object_id) REFERENCES astronomical_objects(id) ON DELETE CASCADE
))";

const char* CREATE_CATEGORIES_TABLE = R"(
CREATE TABLE IF NOT EXISTS categories (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    description TEXT,
    color TEXT,
    icon TEXT,
    object_count INTEGER DEFAULT 0,
    created_at TEXT
))";

const char* CREATE_OBJECT_CATEGORIES_TABLE = R"(
CREATE TABLE IF NOT EXISTS object_categories (
    object_id TEXT,
    category_id TEXT,
    assigned_at TEXT,
    PRIMARY KEY (object_id, category_id),
    FOREIGN KEY (object_id) REFERENCES astronomical_objects(id) ON DELETE CASCADE,
    FOREIGN KEY (category_id) REFERENCES categories(id) ON DELETE CASCADE
))";

const char* CREATE_EPHEMERIS_TABLE = R"(
CREATE TABLE IF NOT EXISTS ephemeris_data (
    object_id TEXT PRIMARY KEY,
    heliocentric_distance_au REAL,
    geocentric_distance_au REAL,
    phase_angle_deg REAL,
    solar_elongation_deg REAL,
    phase REAL,
    magnitude_predicted REAL,
    altitude_at_sunset_deg REAL,
    altitude_at_sunrise_deg REAL,
    transit_altitude_deg REAL,
    transit_time_utc REAL,
    rise_time_utc REAL,
    set_time_utc REAL,
    current_elements TEXT,
    visibility_windows TEXT,
    FOREIGN KEY (object_id) REFERENCES astronomical_objects(id) ON DELETE CASCADE
))";

std::string SqlEscape(const std::string& str) {
    std::string result;
    for (char c : str) {
        if (c == '\'') {
            result += "''";
        } else {
            result += c;
        }
    }
    return result;
}

std::string DoubleToString(double value) {
    std::ostringstream oss;
    oss << std::setprecision(15) << value;
    return oss.str();
}

std::string IntToString(int value) {
    return std::to_string(value);
}

std::string BoolToString(bool value) {
    return value ? "1" : "0";
}

double StringToDouble(const std::string& str) {
    if (str.empty()) return 0.0;
    return std::stod(str);
}

int StringToInt(const std::string& str) {
    if (str.empty()) return 0;
    return std::stoi(str);
}

bool StringToBool(const std::string& str) {
    return str == "1" || str == "true" || str == "TRUE";
}

std::string TimestampToString(const google::protobuf::Timestamp& ts) {
    if (ts.seconds() == 0 && ts.nanos() == 0) return "";
    return google::protobuf::util::TimeUtil::ToString(ts);
}

std::string JoinRepeatedField(const google::protobuf::RepeatedPtrField<std::string>& field, 
                               const std::string& separator = ",") {
    std::string result;
    for (int i = 0; i < field.size(); ++i) {
        if (i > 0) result += separator;
        result += field[i];
    }
    return result;
}

std::string MapToString(const google::protobuf::Map<std::string, std::string>& map) {
    std::string result;
    bool first = true;
    for (const auto& pair : map) {
        if (!first) result += ",";
        result += pair.first + ":" + pair.second;
        first = false;
    }
    return result;
}

void SetTimestampFromString(google::protobuf::Timestamp* ts, const std::string& str) {
    if (str.empty()) return;
    google::protobuf::util::TimeUtil::FromString(str, ts);
}

// ─── CSV Import Helpers ─────────────────────────────────────────────────────

static char DetectCsvDelimiter(const std::string& header) {
    int semicolons = 0, commas = 0;
    for (char c : header) {
        if (c == ';') semicolons++;
        if (c == ',') commas++;
    }
    return semicolons >= commas ? ';' : ',';
}

static std::vector<std::string> SplitCsvLine(const std::string& line, char delimiter) {
    std::vector<std::string> columns;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, delimiter)) {
        columns.push_back(cell);
    }
    return columns;
}

static std::string TrimQuotes(const std::string& str) {
    std::string result = str;
    // Trim leading whitespace and quotes
    size_t start = result.find_first_not_of(" \t\"");
    if (start == std::string::npos) return "";
    result = result.substr(start);
    // Trim trailing whitespace and quotes
    size_t end = result.find_last_not_of(" \t\"");
    if (end != std::string::npos) result = result.substr(0, end + 1);
    return result;
}

static ObjectType StringToObjectType(const std::string& typeStr) {
    if (typeStr.empty()) return ObjectType::UNKNOWN_TYPE;
    
    // OpenNGC type codes (https://github.com/mattiaverga/OpenNGC)
    // Exact matches for non-galaxy types (checked before prefix matches)
    if (typeStr == "OC" || typeStr == "OCl" || typeStr == "Cl*")
        return ObjectType::STAR_CLUSTER_OPEN;
    if (typeStr == "Gb" || typeStr == "GlC")
        return ObjectType::STAR_CLUSTER_GLOBULAR;
    if (typeStr == "Nb" || typeStr == "Neb" || typeStr == "ISM")
        return ObjectType::DIFFUSE_NEBULA;
    if (typeStr == "Pn" || typeStr == "PN")
        return ObjectType::PLANETARY_NEBULA;
    if (typeStr == "En" || typeStr == "EmO" || typeStr == "EmN" || typeStr == "HII")
        return ObjectType::EMISSION_NEBULA;
    if (typeStr == "Rn" || typeStr == "RfN")
        return ObjectType::REFLECTION_NEBULA;
    if (typeStr == "Dk" || typeStr == "MolC")
        return ObjectType::DARK_NEBULA;
    if (typeStr == "SNR")
        return ObjectType::SUPERNOVA_REMNANT;
    if (typeStr == "*" || typeStr == "Star")
        return ObjectType::STAR;
    if (typeStr == "D*" || typeStr == "Double")
        return ObjectType::DOUBLE_STAR;
    if (typeStr == "V*" || typeStr == "Var")
        return ObjectType::VARIABLE_STAR;
    if (typeStr == "Ast")
        return ObjectType::ASTEROID;
    if (typeStr == "Kt" || typeStr == "Comet")
        return ObjectType::COMET;
    if (typeStr == "QSO" || typeStr == "GxQ")
        return ObjectType::QUASAR;
    if (typeStr == "Nova")
        return ObjectType::VARIABLE_STAR;
    if (typeStr == "SC?" || typeStr == "*Ass")
        return ObjectType::STAR_CLUSTER_OPEN;
    if (typeStr == "**" || typeStr == "Dup")
        return ObjectType::DOUBLE_STAR;
    
    // Combined types (OpenNGC compound codes for objects with dual classification)
    if (typeStr == "OC+Gb" || typeStr == "Cl+Nb" || typeStr == "OC+Nb" || typeStr == "Cl+N")
        return ObjectType::STAR_CLUSTER_OPEN;
    if (typeStr == "Gb+Nb")
        return ObjectType::STAR_CLUSTER_GLOBULAR;
    if (typeStr == "Nb+Pn")
        return ObjectType::PLANETARY_NEBULA;
    
    // Galaxy cluster (GxC = Cluster of galaxies, GCl = cluster generic)
    if (typeStr == "GxC" || typeStr == "GCl")
        return ObjectType::GALAXY_ELLIPTICAL;
    
    // Galaxy groups / irregular galaxies
    if (typeStr == "GxG" || typeStr == "GPair" || typeStr == "GTrpl" || typeStr == "GGroup" || typeStr == "Irr")
        return ObjectType::GALAXY_IRREGULAR;
    
    // --- Galaxy type matching (uses prefix matching for OpenNGC sub-types) ---
    
    // Elliptical galaxies: "E", "E0".."E7", "Ell"
    if (typeStr == "Ell" || typeStr == "E" ||
        (typeStr.size() == 2 && typeStr[0] == 'E' && typeStr[1] >= '0' && typeStr[1] <= '7'))
        return ObjectType::GALAXY_ELLIPTICAL;
    
    // Lenticular: "S0", "SB0", "Lent"
    if (typeStr == "S0" || typeStr == "SB0" || typeStr == "Lent")
        return ObjectType::GALAXY_LENTICULAR;
    
    // Barred spiral galaxies: "SBa", "SBb", "SBc", "SBd", "SBm", "SB"
    if (typeStr.size() >= 2 && typeStr.size() <= 3 &&
        typeStr[0] == 'S' && typeStr[1] == 'B')
        return ObjectType::GALAXY_SPIRAL;
    
    // Unbarred spiral galaxies: "Sa", "Sab", "Sb", "Sbc", "Sc", "Scd", "Sd", "Sdm", "Sm", "S"
    if (typeStr.size() >= 1 && typeStr.size() <= 3 &&
        typeStr[0] == 'S')
        return ObjectType::GALAXY_SPIRAL;
    
    // Generic galaxy (OpenNGC "Gx" = unspecified galaxy type)
    if (typeStr == "Gx" || typeStr == "G")
        return ObjectType::GALAXY_SPIRAL;  // Most common galaxy in OpenNGC is spiral
    
    // Non-existent objects (keep as unknown)
    if (typeStr == "NonEx" || typeStr == "Other")
        return ObjectType::UNKNOWN_TYPE;
    
    return ObjectType::UNKNOWN_TYPE;
}

static double ParseRaHours(const std::string& value) {
    if (value.empty()) return 0.0;
    // HH:MM:SS.s format (OpenNGC)
    size_t c1 = value.find(':');
    if (c1 != std::string::npos) {
        double h = std::stod(value.substr(0, c1));
        size_t c2 = value.find(':', c1 + 1);
        if (c2 != std::string::npos) {
            double m = std::stod(value.substr(c1 + 1, c2 - c1 - 1));
            double s = std::stod(value.substr(c2 + 1));
            return h + m / 60.0 + s / 3600.0;
        }
        double m = std::stod(value.substr(c1 + 1));
        return h + m / 60.0;
    }
    // Decimal hours (HYG)
    try { return std::stod(value); } catch (...) { return 0.0; }
}

static double ParseDecDegrees(const std::string& value) {
    if (value.empty()) return 0.0;
    // DD:MM:SS.s format (OpenNGC)
    size_t c1 = value.find(':');
    if (c1 != std::string::npos) {
        double d = std::stod(value.substr(0, c1));
        size_t c2 = value.find(':', c1 + 1);
        if (c2 != std::string::npos) {
            double m = std::stod(value.substr(c1 + 1, c2 - c1 - 1));
            double s = std::stod(value.substr(c2 + 1));
            double result = std::abs(d) + m / 60.0 + s / 3600.0;
            return (d < 0) ? -result : result;
        }
        double m = std::stod(value.substr(c1 + 1));
        double result = std::abs(d) + m / 60.0;
        return (d < 0) ? -result : result;
    }
    // Decimal degrees (HYG)
    try { return std::stod(value); } catch (...) { return 0.0; }
}

// Safe wrapper for sqlite3_column_text that returns empty string on NULL
inline const char* SafeString(sqlite3_stmt* stmt, int col) {
    const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
    return text ? text : "";
}

} // anonymous namespace

ObjectDatabaseServiceImpl::ObjectDatabaseServiceImpl(const std::string& db_path)
    : db_(nullptr), db_path_(db_path) {
    if (!InitializeDatabase()) {
        throw std::runtime_error("Failed to initialize database");
    }
}

ObjectDatabaseServiceImpl::~ObjectDatabaseServiceImpl() {
    if (db_) {
        sqlite3_close(db_);
    }
}

bool ObjectDatabaseServiceImpl::InitializeDatabase() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    int rc = sqlite3_open(db_path_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        return false;
    }
    
    // Enable WAL mode for better concurrent read/write performance
    // This also reduces memory usage during large transactions
    ExecuteQuery("PRAGMA journal_mode=WAL");
    
    // Set busy timeout to avoid SQLITE_BUSY errors during concurrent access
    ExecuteQuery("PRAGMA busy_timeout=5000");
    
    // Enable foreign keys
    ExecuteQuery("PRAGMA foreign_keys = ON");
    
    // Create tables
    if (!CreateTables()) {
        return false;
    }
    
    // Create indexes
    if (!CreateIndexes()) {
        return false;
    }
    
    return true;
}

bool ObjectDatabaseServiceImpl::CreateTables() {
    if (!ExecuteQuery(CREATE_OBJECTS_TABLE)) return false;
    if (!ExecuteQuery(CREATE_FAVORITES_TABLE)) return false;
    if (!ExecuteQuery(CREATE_CATEGORIES_TABLE)) return false;
    if (!ExecuteQuery(CREATE_OBJECT_CATEGORIES_TABLE)) return false;
    if (!ExecuteQuery(CREATE_EPHEMERIS_TABLE)) return false;
    return true;
}

bool ObjectDatabaseServiceImpl::CreateIndexes() {
    std::vector<std::string> indexes = {
        "CREATE INDEX IF NOT EXISTS idx_objects_name ON astronomical_objects(name)",
        "CREATE INDEX IF NOT EXISTS idx_objects_ra_dec ON astronomical_objects(ra_hours, dec_degrees)",
        "CREATE INDEX IF NOT EXISTS idx_objects_magnitude ON astronomical_objects(v_magnitude)",
        "CREATE INDEX IF NOT EXISTS idx_objects_type ON astronomical_objects(object_type)",
        "CREATE INDEX IF NOT EXISTS idx_objects_catalog ON astronomical_objects(catalog_id)",
        "CREATE INDEX IF NOT EXISTS idx_favorites_user ON favorites(user_id)",
        "CREATE INDEX IF NOT EXISTS idx_categories_name ON categories(name)",
        "CREATE INDEX IF NOT EXISTS idx_object_categories ON object_categories(object_id, category_id)"
    };
    
    for (const auto& index : indexes) {
        if (!ExecuteQuery(index)) {
            return false;
        }
    }
    return true;
}

bool ObjectDatabaseServiceImpl::ExecuteQuery(const std::string& query) {
    char* errmsg = nullptr;
    int rc = sqlite3_exec(db_, query.c_str(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        if (errmsg) {
            sqlite3_free(errmsg);
        }
        return false;
    }
    return true;
}

bool ObjectDatabaseServiceImpl::ExecuteQueryWithParams(const std::string& query, 
                                                       const std::vector<std::string>& params) {
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }
    
    for (size_t i = 0; i < params.size(); ++i) {
        sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
    }
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE || rc == SQLITE_ROW;
}

std::string ObjectDatabaseServiceImpl::GenerateUUID() {
    uuid_t uuid;
    uuid_generate(uuid);
    char uuid_str[37];
    uuid_unparse(uuid, uuid_str);
    return std::string(uuid_str);
}

std::string ObjectDatabaseServiceImpl::GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&now_time_t), "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << now_ms.count();
    return oss.str();
}

bool ObjectDatabaseServiceImpl::BeginTransaction() {
    return ExecuteQuery("BEGIN TRANSACTION");
}

bool ObjectDatabaseServiceImpl::CommitTransaction() {
    return ExecuteQuery("COMMIT");
}

bool ObjectDatabaseServiceImpl::RollbackTransaction() {
    return ExecuteQuery("ROLLBACK");
}

// Implementation of gRPC service methods

grpc::Status ObjectDatabaseServiceImpl::CreateObject(grpc::ServerContext* context,
                                                     const AstronomicalObject* request,
                                                     ObjectId* response) {
    std::string id = GenerateUUID();
    
    AstronomicalObject object = *request;
    object.set_id(id);
    // Set timestamps to current time
    auto now = google::protobuf::util::TimeUtil::SecondsToTimestamp(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    *object.mutable_created_at() = now;
    *object.mutable_updated_at() = now;
    
    std::string sqlite_error;
    if (!InsertObject(object, &id, &sqlite_error)) {
        std::string error_msg = "Failed to create object: " + sqlite_error;
        return grpc::Status(grpc::StatusCode::INTERNAL, error_msg);
    }
    
    response->set_id(id);
    return grpc::Status::OK;
}

grpc::Status ObjectDatabaseServiceImpl::GetObject(grpc::ServerContext* context,
                                                  const ObjectId* request,
                                                  AstronomicalObject* response) {
    std::string id = request->id();
    
    if (!GetObjectInternal(id, response)) {
        return grpc::Status(grpc::StatusCode::NOT_FOUND, "Object not found");
    }
    
    return grpc::Status::OK;
}

grpc::Status ObjectDatabaseServiceImpl::UpdateObject(grpc::ServerContext* context,
                                                     const AstronomicalObject* request,
                                                     google::protobuf::Empty* response) {
    if (!UpdateObjectInternal(*request)) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to update object");
    }
    
    return grpc::Status::OK;
}

grpc::Status ObjectDatabaseServiceImpl::DeleteObject(grpc::ServerContext* context,
                                                     const ObjectId* request,
                                                     google::protobuf::Empty* response) {
    std::string id = request->id();
    
    if (!DeleteObjectInternal(id)) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to delete object");
    }
    
    return grpc::Status::OK;
}

grpc::Status ObjectDatabaseServiceImpl::ListObjects(grpc::ServerContext* context,
                                                    const ObjectListRequest* request,
                                                    ObjectList* response) {
    if (!ListObjectsInternal(*request, response)) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to list objects");
    }
    
    return grpc::Status::OK;
}

grpc::Status ObjectDatabaseServiceImpl::SearchObjects(grpc::ServerContext* context,
                                                      const ObjectSearchRequest* request,
                                                      ObjectList* response) {
    if (!SearchObjectsInternal(*request, response)) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to search objects");
    }
    
    return grpc::Status::OK;
}

grpc::Status ObjectDatabaseServiceImpl::ImportCatalog(grpc::ServerContext* context,
                                                      const ImportCatalogRequest* request,
                                                      ImportResult* response) {
    // 1. Validate format is CSV
    if (request->format() != ImportCatalogRequest::CSV) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
            "Only CSV format is supported (requested format code: " +
            std::to_string(static_cast<int>(request->format())) + ")");
    }

    // 2. Get CSV data
    const std::string& csv_data = request->data();
    if (csv_data.empty()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "No data provided");
    }

    // 3. Split into non-empty lines
    std::vector<std::string> lines;
    {
        std::istringstream ss(csv_data);
        std::string line;
        while (std::getline(ss, line)) {
            // Trim trailing carriage return (Windows line endings)
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) lines.push_back(line);
        }
    }

    if (lines.size() < 2) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
            "CSV must contain at least a header line and one data row (found " +
            std::to_string(lines.size()) + " lines)");
    }

    // 4. Parse header to build column-name → index map
    std::string header = lines[0];
    char delimiter = DetectCsvDelimiter(header);
    std::vector<std::string> header_cols = SplitCsvLine(header, delimiter);

    std::map<std::string, int> col_index;
    for (size_t i = 0; i < header_cols.size(); ++i) {
        col_index[TrimQuotes(header_cols[i])] = static_cast<int>(i);
    }

    // 5. Get field_mapping and catalog_name from request
    const auto& field_mapping = request->field_mapping();
    std::string request_catalog_name = request->catalog_name();

    // 6. Process each data row in batches to avoid holding large transactions
    int imported = 0, skipped = 0, updated = 0;
    std::vector<std::string> errors;
    std::string now_timestamp = GetCurrentTimestamp();

    // Batch size: commit every 1000 rows to keep transaction journal small
    static constexpr size_t BATCH_SIZE = 1000;

    // Pre-allocate SQL statements (reused per batch for efficiency)
    const char* check_sql = "SELECT id FROM astronomical_objects WHERE name = ? AND catalog_name = ?";
    const char* update_sql = R"(
        UPDATE astronomical_objects SET
            ra_hours = ?, dec_degrees = ?, v_magnitude = ?, b_magnitude = ?,
            spectral_type = ?, object_type = ?, alternate_names = ?,
            catalog_id = ?, notes = ?, custom_fields = ?, updated_at = ?
        WHERE id = ?
    )";
    const char* insert_sql = R"(
        INSERT INTO astronomical_objects (
            id, name, catalog_name, alternate_names, ra_hours, dec_degrees,
            v_magnitude, b_magnitude, spectral_type, object_type,
            catalog_id, notes, custom_fields, created_at, updated_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

    // Lock the database mutex for the entire batch operation
    std::lock_guard<std::mutex> lock(db_mutex_);

    BeginTransaction();

    for (size_t row_idx = 1; row_idx < lines.size(); ++row_idx) {
        try {
            const std::string& raw_line = lines[row_idx];
            std::vector<std::string> cells = SplitCsvLine(raw_line, delimiter);
            if (cells.empty()) {
                continue;
            }

            // Build AstronomicalObject from CSV row using field_mapping
            AstronomicalObject obj;
            bool has_name = false;

            for (const auto& [proto_field, csv_value] : field_mapping) {
                // Determine if csv_value refers to a CSV column or is a literal
                int idx = -1;
                auto it = col_index.find(csv_value);
                if (it != col_index.end()) {
                    idx = it->second;
                }

                std::string cell_value;
                if (idx >= 0 && idx < static_cast<int>(cells.size())) {
                    cell_value = TrimQuotes(cells[idx]);
                } else {
                    // Use as a literal value (e.g. catalog_name = "HYG")
                    cell_value = csv_value;
                }

                if (cell_value.empty()) continue;

                // Map proto field names to AstronomicalObject setters
                if (proto_field == "name") {
                    obj.set_name(cell_value);
                    has_name = true;
                } else if (proto_field == "ra") {
                    obj.set_ra_hours(ParseRaHours(cell_value));
                } else if (proto_field == "dec") {
                    obj.set_dec_degrees(ParseDecDegrees(cell_value));
                } else if (proto_field == "type") {
                    obj.set_object_type(StringToObjectType(cell_value));
                } else if (proto_field == "magnitude") {
                    try { obj.set_v_magnitude(std::stod(cell_value)); }
                    catch (...) { /* ignore parse errors for optional fields */ }
                } else if (proto_field == "spectral_type") {
                    obj.set_spectral_type(cell_value);
                } else if (proto_field == "b_magnitude") {
                    try { obj.set_b_magnitude(std::stod(cell_value)); }
                    catch (...) { /* ignore parse errors */ }
                } else if (proto_field == "catalog_id") {
                    obj.set_catalog_id(cell_value);
                } else if (proto_field == "catalog_name") {
                    obj.set_catalog_name(cell_value);
                } else if (proto_field == "constellation") {
                    // No constellation field in proto yet; store in custom_fields
                    (*obj.mutable_custom_fields())["constellation"] = cell_value;
                } else if (proto_field == "angular_size") {
                    try { obj.set_angular_size_arcmin(std::stod(cell_value)); }
                    catch (...) { /* ignore parse errors */ }
                } else if (proto_field == "radial_velocity") {
                    try { obj.set_radial_velocity_kms(std::stod(cell_value)); }
                    catch (...) { /* ignore parse errors */ }
                } else if (proto_field == "redshift") {
                    try { obj.set_redshift(std::stod(cell_value)); }
                    catch (...) { /* ignore parse errors */ }
                } else if (proto_field == "distance") {
                    try { obj.set_distance_ly(std::stod(cell_value)); }
                    catch (...) { /* ignore parse errors */ }
                } else if (proto_field == "notes") {
                    obj.set_notes(cell_value);
                }
                // Unknown proto_field keys are silently ignored
            }

            // Set catalog_name from request if not set via field_mapping
            if (!request_catalog_name.empty() && obj.catalog_name().empty()) {
                obj.set_catalog_name(request_catalog_name);
            }

            // Skip objects without a name
            if (!has_name) {
                skipped++;
                continue;
            }

            // 7. Check if an object with the same (name, catalog_name) already exists
            sqlite3_stmt* check_stmt = nullptr;
            bool exists = false;
            std::string existing_id;

            if (sqlite3_prepare_v2(db_, check_sql, -1, &check_stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(check_stmt, 1, obj.name().c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(check_stmt, 2, obj.catalog_name().c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(check_stmt) == SQLITE_ROW) {
                    exists = true;
                    const char* eid = reinterpret_cast<const char*>(sqlite3_column_text(check_stmt, 0));
                    if (eid) existing_id = eid;
                }
                sqlite3_finalize(check_stmt);
            }

            if (exists) {
                if (request->overwrite()) {
                    // Update existing object with imported data
                    sqlite3_stmt* update_stmt = nullptr;
                    if (sqlite3_prepare_v2(db_, update_sql, -1, &update_stmt, nullptr) == SQLITE_OK) {
                        int p = 1;
                        sqlite3_bind_double(update_stmt, p++, obj.ra_hours());
                        sqlite3_bind_double(update_stmt, p++, obj.dec_degrees());
                        sqlite3_bind_double(update_stmt, p++, obj.v_magnitude());
                        sqlite3_bind_double(update_stmt, p++, obj.b_magnitude());
                        sqlite3_bind_text(update_stmt, p++, obj.spectral_type().c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int(update_stmt, p++, static_cast<int>(obj.object_type()));
                        sqlite3_bind_text(update_stmt, p++, obj.alternate_names().c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(update_stmt, p++, obj.catalog_id().c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(update_stmt, p++, obj.notes().c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(update_stmt, p++, MapToString(obj.custom_fields()).c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(update_stmt, p++, now_timestamp.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(update_stmt, p++, existing_id.c_str(), -1, SQLITE_TRANSIENT);

                        if (sqlite3_step(update_stmt) == SQLITE_DONE) {
                            updated++;
                        }
                        sqlite3_finalize(update_stmt);
                    }
                } else {
                    skipped++;
                }
            } else {
                // Insert new object
                std::string new_id = GenerateUUID();

                sqlite3_stmt* insert_stmt = nullptr;
                if (sqlite3_prepare_v2(db_, insert_sql, -1, &insert_stmt, nullptr) == SQLITE_OK) {
                    int p = 1;
                    sqlite3_bind_text(insert_stmt, p++, new_id.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(insert_stmt, p++, obj.name().c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(insert_stmt, p++, obj.catalog_name().c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(insert_stmt, p++, obj.alternate_names().c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_double(insert_stmt, p++, obj.ra_hours());
                    sqlite3_bind_double(insert_stmt, p++, obj.dec_degrees());
                    sqlite3_bind_double(insert_stmt, p++, obj.v_magnitude());
                    sqlite3_bind_double(insert_stmt, p++, obj.b_magnitude());
                    sqlite3_bind_text(insert_stmt, p++, obj.spectral_type().c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int(insert_stmt, p++, static_cast<int>(obj.object_type()));
                    sqlite3_bind_text(insert_stmt, p++, obj.catalog_id().c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(insert_stmt, p++, obj.notes().c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(insert_stmt, p++, MapToString(obj.custom_fields()).c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(insert_stmt, p++, now_timestamp.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(insert_stmt, p++, now_timestamp.c_str(), -1, SQLITE_TRANSIENT);

                    if (sqlite3_step(insert_stmt) == SQLITE_DONE) {
                        imported++;
                    }
                    sqlite3_finalize(insert_stmt);
                }
            }
        } catch (const std::exception& e) {
            errors.push_back("Row " + std::to_string(row_idx + 1) + ": " + e.what());
        }

        // Commit in batches to keep transaction journal small
        if (row_idx % BATCH_SIZE == 0 && row_idx < lines.size()) {
            CommitTransaction();
            BeginTransaction();
        }
    }

    CommitTransaction();

    // 8. Populate response
    response->set_objects_imported(imported);
    response->set_objects_skipped(skipped);
    response->set_objects_updated(updated);
    for (const auto& err : errors) {
        response->add_errors(err);
    }
    auto now = google::protobuf::util::TimeUtil::SecondsToTimestamp(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    *response->mutable_import_time() = now;

    return grpc::Status::OK;
}

grpc::Status ObjectDatabaseServiceImpl::ExportCatalog(grpc::ServerContext* context,
                                                      const ExportCatalogRequest* request,
                                                      ExportResult* response) {
    // TODO: Implement catalog export
    response->set_object_count(0);
    return grpc::Status::OK;
}

grpc::Status ObjectDatabaseServiceImpl::AddToFavorites(grpc::ServerContext* context,
                                                       const FavoriteRequest* request,
                                                       google::protobuf::Empty* response) {
    std::string object_id = request->object_id();
    std::string user_id = request->user_id().empty() ? "default" : request->user_id();
    
    if (!AddFavorite(object_id, user_id)) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to add to favorites");
    }
    
    return grpc::Status::OK;
}

grpc::Status ObjectDatabaseServiceImpl::RemoveFromFavorites(grpc::ServerContext* context,
                                                            const FavoriteRequest* request,
                                                            google::protobuf::Empty* response) {
    std::string object_id = request->object_id();
    std::string user_id = request->user_id().empty() ? "default" : request->user_id();
    
    if (!RemoveFavorite(object_id, user_id)) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to remove from favorites");
    }
    
    return grpc::Status::OK;
}

grpc::Status ObjectDatabaseServiceImpl::GetFavorites(grpc::ServerContext* context,
                                                     const google::protobuf::Empty* request,
                                                     ObjectList* response) {
    ObjectSearchRequest search_request;
    search_request.set_include_favorites_only(true);
    
    if (!SearchObjectsInternal(search_request, response)) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to get favorites");
    }
    
    return grpc::Status::OK;
}

grpc::Status ObjectDatabaseServiceImpl::CreateCategory(grpc::ServerContext* context,
                                                       const Category* request,
                                                       CategoryId* response) {
    std::string id = GenerateUUID();
    
    Category category = *request;
    category.set_id(id);
    auto now = google::protobuf::util::TimeUtil::SecondsToTimestamp(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    *category.mutable_created_at() = now;
    category.set_object_count(0);
    
    if (!CreateCategoryInternal(category, &id)) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to create category");
    }
    
    response->set_id(id);
    return grpc::Status::OK;
}

grpc::Status ObjectDatabaseServiceImpl::ListCategories(grpc::ServerContext* context,
                                                       const google::protobuf::Empty* request,
                                                       CategoryList* response) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    const char* query = "SELECT id, name, description, color, icon, object_count, created_at FROM categories ORDER BY name";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, query, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to prepare query");
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Category* category = response->add_categories();
        category->set_id(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        category->set_name(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        category->set_description(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        category->set_color(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
        category->set_icon(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));
        category->set_object_count(sqlite3_column_int(stmt, 5));
        SetTimestampFromString(category->mutable_created_at(),
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)));
    }
    
    sqlite3_finalize(stmt);
    return grpc::Status::OK;
}

grpc::Status ObjectDatabaseServiceImpl::AssignCategory(grpc::ServerContext* context,
                                                       const ObjectCategory* request,
                                                       google::protobuf::Empty* response) {
    if (!AssignCategoryInternal(request->object_id(), request->category_id())) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to assign category");
    }
    
    return grpc::Status::OK;
}

grpc::Status ObjectDatabaseServiceImpl::RemoveCategory(grpc::ServerContext* context,
                                                       const ObjectCategory* request,
                                                       google::protobuf::Empty* response) {
    if (!RemoveCategoryInternal(request->object_id(), request->category_id())) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to remove category");
    }
    
    return grpc::Status::OK;
}

grpc::Status ObjectDatabaseServiceImpl::FindVisibleObjects(grpc::ServerContext* context,
                                                           const VisibilityRequest* request,
                                                           ObjectList* response) {
    // TODO: Implement visibility calculations
    return grpc::Status::OK;
}

grpc::Status ObjectDatabaseServiceImpl::GetTonightBestObjects(grpc::ServerContext* context,
                                                              const TonightRequest* request,
                                                              ObjectList* response) {
    // TODO: Implement tonight's best objects
    return grpc::Status::OK;
}

grpc::Status ObjectDatabaseServiceImpl::GetObjectVisibility(grpc::ServerContext* context,
                                                            const ObjectVisibilityRequest* request,
                                                            VisibilityInfo* response) {
    // TODO: Implement object visibility calculations
    return grpc::Status::OK;
}

grpc::Status ObjectDatabaseServiceImpl::GetDatabaseStats(grpc::ServerContext* context,
                                                         const google::protobuf::Empty* request,
                                                         DatabaseStats* response) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    // Get total objects
    const char* count_query = "SELECT COUNT(*) FROM astronomical_objects";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, count_query, -1, &stmt, nullptr);
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        response->set_total_objects(sqlite3_column_int(stmt, 0));
    }
    sqlite3_finalize(stmt);
    
    // Get objects by type
    const char* type_query = "SELECT object_type, COUNT(*) FROM astronomical_objects GROUP BY object_type";
    rc = sqlite3_prepare_v2(db_, type_query, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int type = sqlite3_column_int(stmt, 0);
            int count = sqlite3_column_int(stmt, 1);
            (*response->mutable_objects_by_type())[std::to_string(type)] = count;
        }
    }
    sqlite3_finalize(stmt);
    
    // Get favorite count
    const char* fav_query = "SELECT COUNT(*) FROM favorites";
    rc = sqlite3_prepare_v2(db_, fav_query, -1, &stmt, nullptr);
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        response->set_favorite_count(sqlite3_column_int(stmt, 0));
    }
    sqlite3_finalize(stmt);
    
    auto now = google::protobuf::util::TimeUtil::SecondsToTimestamp(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    *response->mutable_last_update() = now;
    return grpc::Status::OK;
}

grpc::Status ObjectDatabaseServiceImpl::BackupDatabase(grpc::ServerContext* context,
                                                       const BackupRequest* request,
                                                       BackupResult* response) {
    // TODO: Implement database backup
    response->set_backup_path("");
    response->set_backup_size(0);
    auto now = google::protobuf::util::TimeUtil::SecondsToTimestamp(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    *response->mutable_backup_time() = now;
    return grpc::Status::OK;
}

grpc::Status ObjectDatabaseServiceImpl::RestoreDatabase(grpc::ServerContext* context,
                                                        const RestoreRequest* request,
                                                        google::protobuf::Empty* response) {
    // TODO: Implement database restore
    return grpc::Status::OK;
}

// Internal helper methods

bool ObjectDatabaseServiceImpl::InsertObject(const AstronomicalObject& object, std::string* id, std::string* error_msg) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    const char* query = R"(
        INSERT INTO astronomical_objects (
            id, name, catalog_name, alternate_names, ra_hours, dec_degrees,
            pm_ra, pm_dec, parallax_mas, distance_pc, distance_ly,
            v_magnitude, b_magnitude, j_magnitude, h_magnitude, k_magnitude,
            spectral_type, luminosity_class, object_type,
            mass_solar, radius_solar, temperature_k, luminosity_solar, age_gyr,
            semi_major_axis_au, eccentricity, inclination_deg, longitude_asc_node_deg,
            argument_perihelion_deg, mean_anomaly_deg, epoch_of_elements_jd,
            diameter_km, albedo, rotation_period_hours,
            angular_size_arcmin, redshift, radial_velocity_kms,
            apparent_dimensions_arcmin_x, apparent_dimensions_arcmin_y,
            ra_error_mas, dec_error_mas, pm_ra_error, pm_dec_error, parallax_error,
            catalog_id, catalog_version, data_source,
            created_at, updated_at, created_by, notes,
            observation_count, last_observed_jd,
            is_favorite, is_visible, has_ephemeris, has_light_curve, has_spectrum,
            user_rating, user_notes, tags, categories, custom_fields
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?,
                  ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?,
                  ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?,
                  ?, ?, ?, ?, ?, ?)
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, query, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        if (error_msg) {
            *error_msg = sqlite3_errmsg(db_);
        }
        return false;
    }
    
    ConvertObjectToStatement(stmt, object);
    
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE && error_msg) {
        *error_msg = sqlite3_errmsg(db_);
    }
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool ObjectDatabaseServiceImpl::UpdateObjectInternal(const AstronomicalObject& object) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    const char* query = R"(
        UPDATE astronomical_objects SET
            name = ?, catalog_name = ?, alternate_names = ?, ra_hours = ?, dec_degrees = ?,
            pm_ra = ?, pm_dec = ?, parallax_mas = ?, distance_pc = ?, distance_ly = ?,
            v_magnitude = ?, b_magnitude = ?, j_magnitude = ?, h_magnitude = ?, k_magnitude = ?,
            spectral_type = ?, luminosity_class = ?, object_type = ?,
            mass_solar = ?, radius_solar = ?, temperature_k = ?, luminosity_solar = ?, age_gyr = ?,
            semi_major_axis_au = ?, eccentricity = ?, inclination_deg = ?, longitude_asc_node_deg = ?,
            argument_perihelion_deg = ?, mean_anomaly_deg = ?, epoch_of_elements_jd = ?,
            diameter_km = ?, albedo = ?, rotation_period_hours = ?,
            angular_size_arcmin = ?, redshift = ?, radial_velocity_kms = ?,
            apparent_dimensions_arcmin_x = ?, apparent_dimensions_arcmin_y = ?,
            ra_error_mas = ?, dec_error_mas = ?, pm_ra_error = ?, pm_dec_error = ?, parallax_error = ?,
            catalog_id = ?, catalog_version = ?, data_source = ?,
            created_at = ?, updated_at = ?, created_by = ?, notes = ?,
            observation_count = ?, last_observed_jd = ?,
            is_favorite = ?, is_visible = ?, has_ephemeris = ?, has_light_curve = ?, has_spectrum = ?,
            user_rating = ?, user_notes = ?, tags = ?, categories = ?, custom_fields = ?
        WHERE id = ?
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, query, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }
    
    ConvertObjectToStatement(stmt, object, true);
    
    // Bind the ID as the last parameter
    // skip_id=true in ConvertObjectToStatement binds 62 params (name through custom_fields),
    // so the WHERE id is parameter 63
    sqlite3_bind_text(stmt, 63, object.id().c_str(), -1, SQLITE_TRANSIENT);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool ObjectDatabaseServiceImpl::DeleteObjectInternal(const std::string& id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    const char* query = "DELETE FROM astronomical_objects WHERE id = ?";
    std::vector<std::string> params = {id};
    
    return ExecuteQueryWithParams(query, params);
}

bool ObjectDatabaseServiceImpl::GetObjectInternal(const std::string& id, AstronomicalObject* object) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    const char* query = "SELECT * FROM astronomical_objects WHERE id = ?";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, query, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        ConvertRowToObject(stmt, object);
        sqlite3_finalize(stmt);
        return true;
    }
    
    sqlite3_finalize(stmt);
    return false;
}

bool ObjectDatabaseServiceImpl::ListObjectsInternal(const ObjectListRequest& request, 
                                                    ObjectList* response) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    std::string query = "SELECT * FROM astronomical_objects WHERE 1=1";
    std::vector<std::string> params;
    
    // Apply filters
    if (request.filter_type() != ObjectType::UNKNOWN_TYPE) {
        query += " AND object_type = ?";
        params.push_back(std::to_string(request.filter_type()));
    }
    
    if (request.min_magnitude() != 0) {
        query += " AND v_magnitude >= ?";
        params.push_back(DoubleToString(request.min_magnitude()));
    }
    
    if (request.max_magnitude() != 0) {
        query += " AND v_magnitude <= ?";
        params.push_back(DoubleToString(request.max_magnitude()));
    }
    
    // Apply sorting
    std::string order_by = "name";
    if (!request.sort_by().empty()) {
        order_by = request.sort_by();
    }
    
    query += " ORDER BY " + order_by;
    if (request.sort_descending()) {
        query += " DESC";
    }
    
    // Apply pagination
    int page = request.page() > 0 ? request.page() : 1;
    int page_size = request.page_size() > 0 ? request.page_size() : 50;
    int offset = (page - 1) * page_size;
    
    query += " LIMIT ? OFFSET ?";
    params.push_back(std::to_string(page_size));
    params.push_back(std::to_string(offset));
    
    // Execute query
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }
    
    // Bind parameters
    for (size_t i = 0; i < params.size(); ++i) {
        sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
    }
    
    // Process results
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AstronomicalObject* object = response->add_objects();
        ConvertRowToObject(stmt, object);
    }
    
    sqlite3_finalize(stmt);
    
    // Get total count
    std::string count_query = "SELECT COUNT(*) FROM astronomical_objects WHERE 1=1";
    rc = sqlite3_prepare_v2(db_, count_query.c_str(), -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            response->set_total_count(sqlite3_column_int(stmt, 0));
        }
    }
    sqlite3_finalize(stmt);
    
    response->set_page(page);
    response->set_page_size(page_size);
    response->set_total_pages((response->total_count() + page_size - 1) / page_size);
    
    return true;
}

bool ObjectDatabaseServiceImpl::SearchObjectsInternal(const ObjectSearchRequest& request,
                                                      ObjectList* response) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    std::string query = "SELECT * FROM astronomical_objects WHERE 1=1";
    std::vector<std::string> params;
    
    // Text search
    if (!request.query().empty()) {
        query += " AND (name LIKE ? OR catalog_name LIKE ? OR alternate_names LIKE ? OR notes LIKE ?)";
        std::string search_term = "%" + request.query() + "%";
        params.push_back(search_term);
        params.push_back(search_term);
        params.push_back(search_term);
        params.push_back(search_term);
    }
    
    // Object type filter
    if (request.object_type() != ObjectType::UNKNOWN_TYPE) {
        query += " AND object_type = ?";
        params.push_back(std::to_string(request.object_type()));
    }
    
    // Coordinate range filter
    if (request.ra_min() != 0 || request.ra_max() != 0) {
        query += " AND ra_hours BETWEEN ? AND ?";
        params.push_back(DoubleToString(request.ra_min()));
        params.push_back(DoubleToString(request.ra_max()));
    }
    
    if (request.dec_min() != 0 || request.dec_max() != 0) {
        query += " AND dec_degrees BETWEEN ? AND ?";
        params.push_back(DoubleToString(request.dec_min()));
        params.push_back(DoubleToString(request.dec_max()));
    }
    
    // Magnitude filter
    if (request.min_magnitude() != 0) {
        query += " AND v_magnitude >= ?";
        params.push_back(DoubleToString(request.min_magnitude()));
    }
    
    if (request.max_magnitude() != 0) {
        query += " AND v_magnitude <= ?";
        params.push_back(DoubleToString(request.max_magnitude()));
    }
    
    // Favorites filter
    if (request.include_favorites_only()) {
        query += " AND is_favorite = 1";
    }
    
    // Visible only filter
    if (request.include_visible_only()) {
        query += " AND is_visible = 1";
    }
    
    // Catalogs filter
    if (request.catalogs_size() > 0) {
        query += " AND (";
        for (int i = 0; i < request.catalogs_size(); ++i) {
            if (i > 0) query += " OR ";
            query += "catalog_name LIKE ?";
            params.push_back(request.catalogs(i) + "%");
        }
        query += ")";
    }
    
    // Constellation filter (stored in custom_fields as "constellation:XXX,...")
    if (!request.constellation().empty()) {
        query += " AND custom_fields LIKE ?";
        params.push_back("%constellation:" + request.constellation() + "%");
    }
    
    // Execute query
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }
    
    // Bind parameters
    for (size_t i = 0; i < params.size(); ++i) {
        sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
    }
    
    // Process results
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AstronomicalObject* object = response->add_objects();
        ConvertRowToObject(stmt, object);
    }
    
    sqlite3_finalize(stmt);
    response->set_total_count(response->objects_size());
    
    return true;
}

bool ObjectDatabaseServiceImpl::AddFavorite(const std::string& object_id, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    const char* query = "INSERT OR IGNORE INTO favorites (user_id, object_id, added_at) VALUES (?, ?, ?)";
    std::vector<std::string> params = {user_id, object_id, GetCurrentTimestamp()};
    
    if (!ExecuteQueryWithParams(query, params)) {
        return false;
    }
    
    // Update the object's favorite flag
    query = "UPDATE astronomical_objects SET is_favorite = 1 WHERE id = ?";
    params = {object_id};
    
    return ExecuteQueryWithParams(query, params);
}

bool ObjectDatabaseServiceImpl::RemoveFavorite(const std::string& object_id, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    const char* query = "DELETE FROM favorites WHERE user_id = ? AND object_id = ?";
    std::vector<std::string> params = {user_id, object_id};
    
    if (!ExecuteQueryWithParams(query, params)) {
        return false;
    }
    
    // Update the object's favorite flag if no other users have it as favorite
    query = R"(
        UPDATE astronomical_objects 
        SET is_favorite = CASE 
            WHEN (SELECT COUNT(*) FROM favorites WHERE object_id = ?) > 0 THEN 1 
            ELSE 0 
        END 
        WHERE id = ?
    )";
    params = {object_id, object_id};
    
    return ExecuteQueryWithParams(query, params);
}

bool ObjectDatabaseServiceImpl::CreateCategoryInternal(const Category& category, std::string* id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    const char* query = "INSERT INTO categories (id, name, description, color, icon, object_count, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)";
    std::vector<std::string> params = {
        category.id(),
        category.name(),
        category.description(),
        category.color(),
        category.icon(),
        std::to_string(category.object_count()),
        TimestampToString(category.created_at())
    };
    
    return ExecuteQueryWithParams(query, params);
}

bool ObjectDatabaseServiceImpl::AssignCategoryInternal(const std::string& object_id, const std::string& category_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    const char* query = "INSERT OR IGNORE INTO object_categories (object_id, category_id, assigned_at) VALUES (?, ?, ?)";
    std::vector<std::string> params = {object_id, category_id, GetCurrentTimestamp()};
    
    return ExecuteQueryWithParams(query, params);
}

bool ObjectDatabaseServiceImpl::RemoveCategoryInternal(const std::string& object_id, const std::string& category_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    const char* query = "DELETE FROM object_categories WHERE object_id = ? AND category_id = ?";
    std::vector<std::string> params = {object_id, category_id};
    
    return ExecuteQueryWithParams(query, params);
}

void ObjectDatabaseServiceImpl::ConvertRowToObject(sqlite3_stmt* stmt, AstronomicalObject* object) {
    // Map column indices to object fields
    int col = 0;
    
    object->set_id(SafeString(stmt, col++));
    object->set_name(SafeString(stmt, col++));
    object->set_catalog_name(SafeString(stmt, col++));
    object->set_alternate_names(SafeString(stmt, col++));
    object->set_ra_hours(sqlite3_column_double(stmt, col++));
    object->set_dec_degrees(sqlite3_column_double(stmt, col++));
    object->set_pm_ra(sqlite3_column_double(stmt, col++));
    object->set_pm_dec(sqlite3_column_double(stmt, col++));
    object->set_parallax_mas(sqlite3_column_double(stmt, col++));
    object->set_distance_pc(sqlite3_column_double(stmt, col++));
    object->set_distance_ly(sqlite3_column_double(stmt, col++));
    object->set_v_magnitude(sqlite3_column_double(stmt, col++));
    object->set_b_magnitude(sqlite3_column_double(stmt, col++));
    object->set_j_magnitude(sqlite3_column_double(stmt, col++));
    object->set_h_magnitude(sqlite3_column_double(stmt, col++));
    object->set_k_magnitude(sqlite3_column_double(stmt, col++));
    object->set_spectral_type(SafeString(stmt, col++));
    object->set_luminosity_class(SafeString(stmt, col++));
    object->set_object_type(static_cast<ObjectType>(sqlite3_column_int(stmt, col++)));
    object->set_mass_solar(sqlite3_column_double(stmt, col++));
    object->set_radius_solar(sqlite3_column_double(stmt, col++));
    object->set_temperature_k(sqlite3_column_double(stmt, col++));
    object->set_luminosity_solar(sqlite3_column_double(stmt, col++));
    object->set_age_gyr(sqlite3_column_double(stmt, col++));
    object->set_semi_major_axis_au(sqlite3_column_double(stmt, col++));
    object->set_eccentricity(sqlite3_column_double(stmt, col++));
    object->set_inclination_deg(sqlite3_column_double(stmt, col++));
    object->set_longitude_asc_node_deg(sqlite3_column_double(stmt, col++));
    object->set_argument_perihelion_deg(sqlite3_column_double(stmt, col++));
    object->set_mean_anomaly_deg(sqlite3_column_double(stmt, col++));
    object->set_epoch_of_elements_jd(sqlite3_column_double(stmt, col++));
    object->set_diameter_km(sqlite3_column_double(stmt, col++));
    object->set_albedo(sqlite3_column_double(stmt, col++));
    object->set_rotation_period_hours(sqlite3_column_double(stmt, col++));
    object->set_angular_size_arcmin(sqlite3_column_double(stmt, col++));
    object->set_redshift(sqlite3_column_double(stmt, col++));
    object->set_radial_velocity_kms(sqlite3_column_double(stmt, col++));
    object->set_apparent_dimensions_arcmin_x(sqlite3_column_double(stmt, col++));
    object->set_apparent_dimensions_arcmin_y(sqlite3_column_double(stmt, col++));
    object->set_ra_error_mas(sqlite3_column_double(stmt, col++));
    object->set_dec_error_mas(sqlite3_column_double(stmt, col++));
    object->set_pm_ra_error(sqlite3_column_double(stmt, col++));
    object->set_pm_dec_error(sqlite3_column_double(stmt, col++));
    object->set_parallax_error(sqlite3_column_double(stmt, col++));
    object->set_catalog_id(SafeString(stmt, col++));
    object->set_catalog_version(SafeString(stmt, col++));
    object->set_data_source(SafeString(stmt, col++));
    SetTimestampFromString(object->mutable_created_at(), SafeString(stmt, col++));
    SetTimestampFromString(object->mutable_updated_at(), SafeString(stmt, col++));
    object->set_created_by(SafeString(stmt, col++));
    object->set_notes(SafeString(stmt, col++));
    object->set_observation_count(sqlite3_column_int(stmt, col++));
    object->set_last_observed_jd(sqlite3_column_double(stmt, col++));
    object->set_is_favorite(sqlite3_column_int(stmt, col++) != 0);
    object->set_is_visible(sqlite3_column_int(stmt, col++) != 0);
    object->set_ephemeris_available(sqlite3_column_int(stmt, col++) != 0);
    object->set_has_light_curve(sqlite3_column_int(stmt, col++) != 0);
    object->set_has_spectrum(sqlite3_column_int(stmt, col++) != 0);
    object->set_user_rating(sqlite3_column_double(stmt, col++));
    object->set_user_notes(SafeString(stmt, col++));
    
    // tags (repeated string)
    const char* tags_str = SafeString(stmt, col++);
    if (*tags_str) {
        std::string tags_s(tags_str);
        size_t start = 0, end = 0;
        while ((end = tags_s.find(',', start)) != std::string::npos) {
            object->add_tags(tags_s.substr(start, end - start));
            start = end + 1;
        }
        if (start < tags_s.length()) {
            object->add_tags(tags_s.substr(start));
        }
    }
    
    // categories (repeated string)
    const char* cats_str = SafeString(stmt, col++);
    if (*cats_str) {
        std::string cats_s(cats_str);
        size_t start = 0, end = 0;
        while ((end = cats_s.find(',', start)) != std::string::npos) {
            object->add_categories(cats_s.substr(start, end - start));
            start = end + 1;
        }
        if (start < cats_s.length()) {
            object->add_categories(cats_s.substr(start));
        }
    }
    
    // custom_fields (map<string, string>)
    const char* cfs_str = SafeString(stmt, col++);
    if (*cfs_str) {
        std::string cfs_s(cfs_str);
        size_t start = 0, end = 0;
        while ((end = cfs_s.find(',', start)) != std::string::npos) {
            std::string pair = cfs_s.substr(start, end - start);
            size_t colon = pair.find(':');
            if (colon != std::string::npos) {
                (*object->mutable_custom_fields())[pair.substr(0, colon)] = pair.substr(colon + 1);
            }
            start = end + 1;
        }
        if (start < cfs_s.length()) {
            std::string pair = cfs_s.substr(start);
            size_t colon = pair.find(':');
            if (colon != std::string::npos) {
                (*object->mutable_custom_fields())[pair.substr(0, colon)] = pair.substr(colon + 1);
            }
        }
    }
}

void ObjectDatabaseServiceImpl::ConvertObjectToStatement(sqlite3_stmt* stmt, const AstronomicalObject& object, bool skip_id) {
    int col = 1;
    
    if (!skip_id) {
        sqlite3_bind_text(stmt, col++, object.id().c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_text(stmt, col++, object.name().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, col++, object.catalog_name().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, col++, object.alternate_names().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, col++, object.ra_hours());
    sqlite3_bind_double(stmt, col++, object.dec_degrees());
    sqlite3_bind_double(stmt, col++, object.pm_ra());
    sqlite3_bind_double(stmt, col++, object.pm_dec());
    sqlite3_bind_double(stmt, col++, object.parallax_mas());
    sqlite3_bind_double(stmt, col++, object.distance_pc());
    sqlite3_bind_double(stmt, col++, object.distance_ly());
    sqlite3_bind_double(stmt, col++, object.v_magnitude());
    sqlite3_bind_double(stmt, col++, object.b_magnitude());
    sqlite3_bind_double(stmt, col++, object.j_magnitude());
    sqlite3_bind_double(stmt, col++, object.h_magnitude());
    sqlite3_bind_double(stmt, col++, object.k_magnitude());
    sqlite3_bind_text(stmt, col++, object.spectral_type().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, col++, object.luminosity_class().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, col++, static_cast<int>(object.object_type()));
    sqlite3_bind_double(stmt, col++, object.mass_solar());
    sqlite3_bind_double(stmt, col++, object.radius_solar());
    sqlite3_bind_double(stmt, col++, object.temperature_k());
    sqlite3_bind_double(stmt, col++, object.luminosity_solar());
    sqlite3_bind_double(stmt, col++, object.age_gyr());
    sqlite3_bind_double(stmt, col++, object.semi_major_axis_au());
    sqlite3_bind_double(stmt, col++, object.eccentricity());
    sqlite3_bind_double(stmt, col++, object.inclination_deg());
    sqlite3_bind_double(stmt, col++, object.longitude_asc_node_deg());
    sqlite3_bind_double(stmt, col++, object.argument_perihelion_deg());
    sqlite3_bind_double(stmt, col++, object.mean_anomaly_deg());
    sqlite3_bind_double(stmt, col++, object.epoch_of_elements_jd());
    sqlite3_bind_double(stmt, col++, object.diameter_km());
    sqlite3_bind_double(stmt, col++, object.albedo());
    sqlite3_bind_double(stmt, col++, object.rotation_period_hours());
    sqlite3_bind_double(stmt, col++, object.angular_size_arcmin());
    sqlite3_bind_double(stmt, col++, object.redshift());
    sqlite3_bind_double(stmt, col++, object.radial_velocity_kms());
    sqlite3_bind_double(stmt, col++, object.apparent_dimensions_arcmin_x());
    sqlite3_bind_double(stmt, col++, object.apparent_dimensions_arcmin_y());
    sqlite3_bind_double(stmt, col++, object.ra_error_mas());
    sqlite3_bind_double(stmt, col++, object.dec_error_mas());
    sqlite3_bind_double(stmt, col++, object.pm_ra_error());
    sqlite3_bind_double(stmt, col++, object.pm_dec_error());
    sqlite3_bind_double(stmt, col++, object.parallax_error());
    sqlite3_bind_text(stmt, col++, object.catalog_id().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, col++, object.catalog_version().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, col++, object.data_source().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, col++, TimestampToString(object.created_at()).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, col++, TimestampToString(object.updated_at()).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, col++, object.created_by().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, col++, object.notes().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, col++, object.observation_count());
    sqlite3_bind_double(stmt, col++, object.last_observed_jd());
    sqlite3_bind_int(stmt, col++, object.is_favorite() ? 1 : 0);
    sqlite3_bind_int(stmt, col++, object.is_visible() ? 1 : 0);
    sqlite3_bind_int(stmt, col++, object.ephemeris_available() ? 1 : 0);
    sqlite3_bind_int(stmt, col++, object.has_light_curve() ? 1 : 0);
    sqlite3_bind_int(stmt, col++, object.has_spectrum() ? 1 : 0);
    sqlite3_bind_double(stmt, col++, object.user_rating());
    sqlite3_bind_text(stmt, col++, object.user_notes().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, col++, JoinRepeatedField(object.tags()).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, col++, JoinRepeatedField(object.categories()).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, col++, MapToString(object.custom_fields()).c_str(), -1, SQLITE_TRANSIENT);
}

// ObjectDatabaseServer implementation
ObjectDatabaseServer::ObjectDatabaseServer(const std::string& server_address, const std::string& db_path)
    : server_address_(server_address), db_path_(db_path) {
}

ObjectDatabaseServer::~ObjectDatabaseServer() {
    Stop();
}

bool ObjectDatabaseServer::Start() {
    try {
        service_ = std::make_unique<ObjectDatabaseServiceImpl>(db_path_);
        
        grpc::ServerBuilder builder;
        builder.AddListeningPort(server_address_, grpc::InsecureServerCredentials());
        builder.RegisterService(service_.get());
        
        // Allow large messages (e.g. HYG catalog ~14MB CSV data)
        // Set to 64MB to accommodate future growth
        builder.SetMaxReceiveMessageSize(64 * 1024 * 1024);
        builder.SetMaxSendMessageSize(64 * 1024 * 1024);
        
        server_ = builder.BuildAndStart();
        if (!server_) {
            return false;
        }
        
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void ObjectDatabaseServer::Stop() {
    if (server_) {
        server_->Shutdown();
        server_.reset();
    }
    service_.reset();
}

void ObjectDatabaseServer::Wait() {
    if (server_) {
        server_->Wait();
    }
}

} // namespace astro_objects
