#include "db_grpc_client.h"
#include <grpcpp/grpcpp.h>

DbGrpcClient::DbGrpcClient(const std::string& address, QObject* parent)
    : QObject(parent), address_(address) {
    channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    stub_ = astro_objects::ObjectDatabaseService::NewStub(channel_);
}

DbGrpcClient::~DbGrpcClient() = default;

void DbGrpcClient::reconnect(const std::string& address) {
    address_ = address;
    channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    stub_ = astro_objects::ObjectDatabaseService::NewStub(channel_);
}

astro_objects::ObjectList DbGrpcClient::listObjects(const astro_objects::ObjectListRequest& req) {
    grpc::ClientContext ctx;
    astro_objects::ObjectList resp;
    stub_->ListObjects(&ctx, req, &resp);
    return resp;
}

astro_objects::ObjectList DbGrpcClient::searchObjects(const astro_objects::ObjectSearchRequest& req) {
    grpc::ClientContext ctx;
    astro_objects::ObjectList resp;
    stub_->SearchObjects(&ctx, req, &resp);
    return resp;
}

astro_objects::DatabaseStats DbGrpcClient::getStats() {
    grpc::ClientContext ctx;
    google::protobuf::Empty req;
    astro_objects::DatabaseStats resp;
    stub_->GetDatabaseStats(&ctx, req, &resp);
    return resp;
}
