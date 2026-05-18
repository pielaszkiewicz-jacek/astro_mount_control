#pragma once

#include <grpcpp/grpcpp.h>
#include <sqlite3.h>
#include <memory>
#include <string>
#include <vector>
#include <mutex>

#include "object_database.grpc.pb.h"

namespace astro_objects {

class ObjectDatabaseServiceImpl final : public ObjectDatabaseService::Service {
public:
    explicit ObjectDatabaseServiceImpl(const std::string& db_path);
    ~ObjectDatabaseServiceImpl() override;

    // Object management
    grpc::Status CreateObject(grpc::ServerContext* context, const AstronomicalObject* request,
                              ObjectId* response) override;
    grpc::Status GetObject(grpc::ServerContext* context, const ObjectId* request,
                           AstronomicalObject* response) override;
    grpc::Status UpdateObject(grpc::ServerContext* context, const AstronomicalObject* request,
                              google::protobuf::Empty* response) override;
    grpc::Status DeleteObject(grpc::ServerContext* context, const ObjectId* request,
                              google::protobuf::Empty* response) override;
    grpc::Status ListObjects(grpc::ServerContext* context, const ObjectListRequest* request,
                             ObjectList* response) override;
    grpc::Status SearchObjects(grpc::ServerContext* context, const ObjectSearchRequest* request,
                               ObjectList* response) override;
    
    // Catalog operations
    grpc::Status ImportCatalog(grpc::ServerContext* context, const ImportCatalogRequest* request,
                               ImportResult* response) override;
    grpc::Status ExportCatalog(grpc::ServerContext* context, const ExportCatalogRequest* request,
                               ExportResult* response) override;
    
    // Favorites and user collections
    grpc::Status AddToFavorites(grpc::ServerContext* context, const FavoriteRequest* request,
                                google::protobuf::Empty* response) override;
    grpc::Status RemoveFromFavorites(grpc::ServerContext* context, const FavoriteRequest* request,
                                     google::protobuf::Empty* response) override;
    grpc::Status GetFavorites(grpc::ServerContext* context, const google::protobuf::Empty* request,
                              ObjectList* response) override;
    
    // Categories and tags
    grpc::Status CreateCategory(grpc::ServerContext* context, const Category* request,
                                CategoryId* response) override;
    grpc::Status ListCategories(grpc::ServerContext* context, const google::protobuf::Empty* request,
                                CategoryList* response) override;
    grpc::Status AssignCategory(grpc::ServerContext* context, const ObjectCategory* request,
                                google::protobuf::Empty* response) override;
    grpc::Status RemoveCategory(grpc::ServerContext* context, const ObjectCategory* request,
                                google::protobuf::Empty* response) override;
    
    // Observation planning
    grpc::Status FindVisibleObjects(grpc::ServerContext* context, const VisibilityRequest* request,
                                    ObjectList* response) override;
    grpc::Status GetTonightBestObjects(grpc::ServerContext* context, const TonightRequest* request,
                                       ObjectList* response) override;
    grpc::Status GetObjectVisibility(grpc::ServerContext* context, const ObjectVisibilityRequest* request,
                                     VisibilityInfo* response) override;
    
    // Statistics and metadata
    grpc::Status GetDatabaseStats(grpc::ServerContext* context, const google::protobuf::Empty* request,
                                  DatabaseStats* response) override;
    grpc::Status BackupDatabase(grpc::ServerContext* context, const BackupRequest* request,
                                BackupResult* response) override;
    grpc::Status RestoreDatabase(grpc::ServerContext* context, const RestoreRequest* request,
                                 google::protobuf::Empty* response) override;

private:
    sqlite3* db_;
    std::mutex db_mutex_;
    std::string db_path_;

    bool InitializeDatabase();
    bool CreateTables();
    bool CreateIndexes();
    
    bool InsertObject(const AstronomicalObject& object, std::string* id);
    bool UpdateObjectInternal(const AstronomicalObject& object);
    bool DeleteObjectInternal(const std::string& id);
    bool GetObjectInternal(const std::string& id, AstronomicalObject* object);
    bool ListObjectsInternal(const ObjectListRequest& request, ObjectList* response);
    bool SearchObjectsInternal(const ObjectSearchRequest& request, ObjectList* response);
    
    bool AddFavorite(const std::string& object_id, const std::string& user_id);
    bool RemoveFavorite(const std::string& object_id, const std::string& user_id);
    
    bool CreateCategoryInternal(const Category& category, std::string* id);
    bool AssignCategoryInternal(const std::string& object_id, const std::string& category_id);
    bool RemoveCategoryInternal(const std::string& object_id, const std::string& category_id);
    
    bool ExecuteQuery(const std::string& query);
    bool ExecuteQueryWithParams(const std::string& query, const std::vector<std::string>& params);
    
    void ConvertRowToObject(sqlite3_stmt* stmt, AstronomicalObject* object);
    void ConvertObjectToStatement(sqlite3_stmt* stmt, const AstronomicalObject& object);
    
    std::string GenerateUUID();
    std::string GetCurrentTimestamp();
    
    bool BeginTransaction();
    bool CommitTransaction();
    bool RollbackTransaction();
};

class ObjectDatabaseServer {
public:
    ObjectDatabaseServer(const std::string& server_address, const std::string& db_path);
    ~ObjectDatabaseServer();
    
    bool Start();
    void Stop();
    void Wait();
    
private:
    std::string server_address_;
    std::string db_path_;
    std::unique_ptr<ObjectDatabaseServiceImpl> service_;
    std::unique_ptr<grpc::Server> server_;
};

} // namespace astro_objects