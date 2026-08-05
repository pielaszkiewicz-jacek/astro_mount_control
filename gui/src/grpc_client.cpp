#include "grpc_client.h"
#include <grpcpp/grpcpp.h>

GrpcClient::GrpcClient(const std::string& address, QObject* parent)
    : QObject(parent), address_(address) {
    channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    stub_ = astro_mount::MountControllerService::NewStub(channel_);
}

GrpcClient::~GrpcClient() = default;

void GrpcClient::reconnect(const std::string& address) {
    address_ = address;
    channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    stub_ = astro_mount::MountControllerService::NewStub(channel_);
}

astro_mount::ControllerState GrpcClient::getState() {
    grpc::ClientContext ctx;
    google::protobuf::Empty req;
    astro_mount::ControllerState resp;
    stub_->GetState(&ctx, req, &resp);
    return resp;
}

astro_mount::ControllerState::MountStatus GrpcClient::getStatus() {
    return getState().status();
}

bool GrpcClient::slewTo(double ra, double dec) {
    grpc::ClientContext ctx;
    astro_mount::Coordinates req;
    req.set_ra(ra);
    req.set_dec(dec);
    google::protobuf::Empty resp;
    auto status = stub_->SlewToCoordinates(&ctx, req, &resp);
    return status.ok();
}

bool GrpcClient::track(double ra, double dec) {
    grpc::ClientContext ctx;
    astro_mount::Coordinates req;
    req.set_ra(ra);
    req.set_dec(dec);
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

bool GrpcClient::unpark() {
    grpc::ClientContext ctx;
    google::protobuf::Empty req, resp;
    return stub_->Unpark(&ctx, req, &resp).ok();
}

bool GrpcClient::clearErrors() {
    grpc::ClientContext ctx;
    google::protobuf::Empty req, resp;
    return stub_->ClearErrors(&ctx, req, &resp).ok();
}

astro_mount::Configuration GrpcClient::getConfig() {
    grpc::ClientContext ctx;
    google::protobuf::Empty req;
    astro_mount::Configuration resp;
    stub_->GetConfiguration(&ctx, req, &resp);
    return resp;
}

bool GrpcClient::updateConfig(const astro_mount::Configuration& config) {
    grpc::ClientContext ctx;
    google::protobuf::Empty resp;
    auto status = stub_->UpdateConfiguration(&ctx, config, &resp);
    return status.ok();
}

bool GrpcClient::controlAxis(int axis_id, int mode, double target_position,
                             double target_velocity, double acceleration, bool relative) {
    grpc::ClientContext ctx;
    astro_mount::AxisControlRequest req;
    req.set_axis_id(axis_id);
    req.set_mode(static_cast<astro_mount::AxisControlMode>(mode));
    req.set_target_position(target_position);
    req.set_target_velocity(target_velocity);
    req.set_acceleration(acceleration);
    req.set_relative(relative);
    google::protobuf::Empty resp;
    auto status = stub_->ControlAxis(&ctx, req, &resp);
    return status.ok();
}

bool GrpcClient::stopAxis(int axis_id, bool decelerate, double deceleration) {
    grpc::ClientContext ctx;
    astro_mount::AxisStopRequest req;
    req.set_axis_id(axis_id);
    req.set_decelerate(decelerate);
    req.set_deceleration(deceleration);
    google::protobuf::Empty resp;
    auto status = stub_->StopAxis(&ctx, req, &resp);
    return status.ok();
}

bool GrpcClient::emergencyStop(int axis_id, bool reset_after) {
    grpc::ClientContext ctx;
    astro_mount::EmergencyStopRequest req;
    req.set_axis_id(axis_id);
    req.set_reset_after(reset_after);
    google::protobuf::Empty resp;
    auto status = stub_->EmergencyStop(&ctx, req, &resp);
    return status.ok();
}
