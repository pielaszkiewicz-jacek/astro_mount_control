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
    
    if (!InsertObject(object, &id)) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to create object");
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
    // TODO: Implement catalog import
    response->set_objects_imported(0);
    response->set_objects_skipped(0);
    response->set_objects_updated(0);
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

bool ObjectDatabaseServiceImpl::InsertObject(const AstronomicalObject& object, std::string* id) {
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
                  ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 
                  ?, ?, ?, ?, ?, ?)
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, query, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }
    
    ConvertObjectToStatement(stmt, object);
    
    rc = sqlite3_step(stmt);
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
            updated_at = ?, created_by = ?, notes = ?,
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
    
    ConvertObjectToStatement(stmt, object);
    
    // Bind the ID as the last parameter
    sqlite3_bind_text(stmt, 65, object.id().c_str(), -1, SQLITE_TRANSIENT);
    
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
    
    object->set_id(reinterpret_cast<const char*>(sqlite3_column_text(stmt, col++)));
    object->set_name(reinterpret_cast<const char*>(sqlite3_column_text(stmt, col++)));
    object->set_catalog_name(reinterpret_cast<const char*>(sqlite3_column_text(stmt, col++)));
    object->set_alternate_names(reinterpret_cast<const char*>(sqlite3_column_text(stmt, col++)));
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
    object->set_spectral_type(reinterpret_cast<const char*>(sqlite3_column_text(stmt, col++)));
    object->set_luminosity_class(reinterpret_cast<const char*>(sqlite3_column_text(stmt, col++)));
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
    object->set_catalog_id(reinterpret_cast<const char*>(sqlite3_column_text(stmt, col++)));
    object->set_catalog_version(reinterpret_cast<const char*>(sqlite3_column_text(stmt, col++)));
    object->set_data_source(reinterpret_cast<const char*>(sqlite3_column_text(stmt, col++)));
    SetTimestampFromString(object->mutable_created_at(),
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, col++)));
    SetTimestampFromString(object->mutable_updated_at(),
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, col++)));
    object->set_created_by(reinterpret_cast<const char*>(sqlite3_column_text(stmt, col++)));
    object->set_notes(reinterpret_cast<const char*>(sqlite3_column_text(stmt, col++)));
    object->set_observation_count(sqlite3_column_int(stmt, col++));
    object->set_last_observed_jd(sqlite3_column_double(stmt, col++));
    object->set_is_favorite(sqlite3_column_int(stmt, col++) != 0);
    object->set_is_visible(sqlite3_column_int(stmt, col++) != 0);
    object->set_ephemeris_available(sqlite3_column_int(stmt, col++) != 0);
    object->set_has_light_curve(sqlite3_column_int(stmt, col++) != 0);
    object->set_has_spectrum(sqlite3_column_int(stmt, col++) != 0);
    object->set_user_rating(sqlite3_column_double(stmt, col++));
    object->set_user_notes(reinterpret_cast<const char*>(sqlite3_column_text(stmt, col++)));
    
    // tags (repeated string)
    const char* tags_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col++));
    if (tags_str) {
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
    const char* cats_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col++));
    if (cats_str) {
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
    const char* cfs_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col++));
    if (cfs_str) {
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

void ObjectDatabaseServiceImpl::ConvertObjectToStatement(sqlite3_stmt* stmt, const AstronomicalObject& object) {
    int col = 1;
    
    sqlite3_bind_text(stmt, col++, object.id().c_str(), -1, SQLITE_TRANSIENT);
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
