#include "grpc_client.h"
#include <grpcpp/grpcpp.h>

GrpcClient::GrpcClient(const std::string& address, QObject* parent)
    : QObject(parent) {
    channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    stub_ = astro_mount::MountControllerService::NewStub(channel_);
}

GrpcClient::~GrpcClient() = default;

astro_mount::ControllerState GrpcClient::getState() {
    grpc::ClientContext ctx;
    google::protobuf::Empty req;
    astro_mount::ControllerState resp;
    stub_->GetState(&ctx, req, &resp);
    return resp;
}

astro_mount::MountStatus GrpcClient::getStatus() {
    grpc::ClientContext ctx;
    google::protobuf::Empty req;
    astro_mount::MountStatus resp;
    stub_->GetState(&ctx, req, &resp);
    return resp;
}

bool GrpcClient::slewTo(double ra, double dec) {
    grpc::ClientContext ctx;
    astro_mount::SlewToCoordinatesRequest req;
    req.set_ra(ra); req.set_dec(dec);
    google::protobuf::Empty resp;
    auto status = stub_->SlewToCoordinates(&ctx, req, &resp);
    return status.ok();
}

bool GrpcClient::track(double ra, double dec) {
    grpc::ClientContext ctx;
    astro_mount::TrackObjectRequest req;
    req.set_ra(ra); req.set_dec(dec);
    google::protobuf::Empty resp;
    auto status = stub_->TrackObject(&ctx, req, &resp);
    return status.ok();
}

bool GrpcClient::stop() {
    grpc::ClientContext ctx;
    google::protobuf::Empty req, resp;
    return stub_->Stop(&ctx, req, &resp).ok();
}

bool GrpcClient::park() {
    grpc::ClientContext ctx;
    google::protobuf::Empty req, resp;
    return stub_->Park(&ctx, req, &resp).ok();
}
