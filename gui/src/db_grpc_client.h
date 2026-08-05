#ifndef DB_GRPC_CLIENT_H
#define DB_GRPC_CLIENT_H
#include <QObject>
#include <grpcpp/grpcpp.h>
#include "object_database.grpc.pb.h"
#include <google/protobuf/empty.pb.h>

class DbGrpcClient : public QObject {
    Q_OBJECT
public:
    explicit DbGrpcClient(const std::string& address, QObject* parent = nullptr);
    ~DbGrpcClient();

    void reconnect(const std::string& address);
    std::string address() const { return address_; }

    astro_objects::ObjectList listObjects(const astro_objects::ObjectListRequest& req);
    astro_objects::ObjectList searchObjects(const astro_objects::ObjectSearchRequest& req);
    astro_objects::DatabaseStats getStats();

private:
    std::string address_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<astro_objects::ObjectDatabaseService::Stub> stub_;
};

#endif
