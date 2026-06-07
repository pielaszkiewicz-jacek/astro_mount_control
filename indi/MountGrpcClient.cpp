#include "MountGrpcClient.h"
#include <chrono>
#include <stdexcept>
#include <iostream>

MountGrpcClient::MountGrpcClient(const std::string& host, int port)
    : host_(host)
    , port_(port)
{
}

MountGrpcClient::~MountGrpcClient()
{
    disconnect();
}

void MountGrpcClient::connect()
{
    if (isConnected())
        return;

    // Create insecure channel (production should use SSL)
    channel_ = grpc::CreateChannel(
        host_ + ":" + std::to_string(port_),
        grpc::InsecureChannelCredentials());

    stub_ = astro_mount::MountControllerService::NewStub(channel_);

    // Verify connectivity with a health check (with deadline)
    const auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(10);
    if (!channel_->WaitForConnected(deadline))
    {
        disconnect();
        throw std::runtime_error(
            "MountGrpcClient: failed to connect to " + host_ + ":" +
            std::to_string(port_) + " within 10s timeout");
    }

    try
    {
        auto health = checkHealth();
        if (health.status() != astro_mount::HealthCheckResponse::SERVING)
        {
            disconnect();
            throw std::runtime_error(
                "MountGrpcClient: gRPC service is not serving. Status: " +
                std::to_string(health.status()));
        }
    }
    catch (const std::exception&)
    {
        disconnect();
        throw;
    }
}

void MountGrpcClient::disconnect()
{
    stub_.reset();
    if (channel_)
    {
        channel_.reset();
    }
}

bool MountGrpcClient::isConnected() const
{
    return channel_ &&
           channel_->GetState(false) == GRPC_CHANNEL_READY;
}

void MountGrpcClient::reconnect()
{
    disconnect();
    connect();
}

// ============================================
// Faza 1: Podstawowe metody
// ============================================

astro_mount::ControllerState MountGrpcClient::getState()
{
    ensureConnected();

    grpc::ClientContext context;
    google::protobuf::Empty request;
    astro_mount::ControllerState response;

    auto status = stub_->GetState(&context, request, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::getState failed: " + status.error_message());
    }

    return response;
}

astro_mount::HealthCheckResponse MountGrpcClient::checkHealth(
    const std::string& service)
{
    ensureConnected();

    grpc::ClientContext context;
    astro_mount::HealthCheckRequest request;
    astro_mount::HealthCheckResponse response;

    request.set_service(service);

    auto status = stub_->CheckHealth(&context, request, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::checkHealth failed: " + status.error_message());
    }

    return response;
}

void MountGrpcClient::clearErrors()
{
    ensureConnected();

    grpc::ClientContext context;
    google::protobuf::Empty request;
    google::protobuf::Empty response;

    auto status = stub_->ClearErrors(&context, request, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::clearErrors failed: " + status.error_message());
    }
}

// ============================================
// Faza 2: Podstawowe sterowanie montażem
// ============================================

void MountGrpcClient::slewToCoordinates(const astro_mount::Coordinates& coords)
{
    ensureConnected();

    grpc::ClientContext context;
    google::protobuf::Empty response;

    auto status = stub_->SlewToCoordinates(&context, coords, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::slewToCoordinates failed: " + status.error_message());
    }
}

void MountGrpcClient::slewToHorizontal(const astro_mount::HorizontalCoordinates& coords)
{
    ensureConnected();

    grpc::ClientContext context;
    google::protobuf::Empty response;

    auto status = stub_->SlewToHorizontal(&context, coords, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::slewToHorizontal failed: " + status.error_message());
    }
}

void MountGrpcClient::trackObject(const astro_mount::Coordinates& coords)
{
    ensureConnected();

    grpc::ClientContext context;
    google::protobuf::Empty response;

    auto status = stub_->TrackObject(&context, coords, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::trackObject failed: " + status.error_message());
    }
}

void MountGrpcClient::stop()
{
    ensureConnected();

    grpc::ClientContext context;
    google::protobuf::Empty request;
    google::protobuf::Empty response;

    auto status = stub_->Stop(&context, request, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::stop failed: " + status.error_message());
    }
}

void MountGrpcClient::park()
{
    ensureConnected();

    grpc::ClientContext context;
    google::protobuf::Empty request;
    google::protobuf::Empty response;

    auto status = stub_->Park(&context, request, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::park failed: " + status.error_message());
    }
}

void MountGrpcClient::unpark()
{
    ensureConnected();

    grpc::ClientContext context;
    google::protobuf::Empty request;
    google::protobuf::Empty response;

    auto status = stub_->Unpark(&context, request, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::unpark failed: " + status.error_message());
    }
}

astro_mount::Configuration MountGrpcClient::getConfiguration()
{
    ensureConnected();

    grpc::ClientContext context;
    google::protobuf::Empty request;
    astro_mount::Configuration response;

    auto status = stub_->GetConfiguration(&context, request, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::getConfiguration failed: " + status.error_message());
    }

    return response;
}

void MountGrpcClient::updateConfiguration(const astro_mount::Configuration& config)
{
    ensureConnected();

    grpc::ClientContext context;
    google::protobuf::Empty response;

    auto status = stub_->UpdateConfiguration(&context, config, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::updateConfiguration failed: " + status.error_message());
    }
}

// ============================================
// Faza 2: Bootstrap calibration
// ============================================

void MountGrpcClient::addBootstrapMeasurement(
    const astro_mount::BootstrapMeasurement& measurement)
{
    ensureConnected();

    grpc::ClientContext context;
    google::protobuf::Empty response;

    auto status = stub_->AddBootstrapMeasurement(&context, measurement, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::addBootstrapMeasurement failed: " + status.error_message());
    }
}

astro_mount::BootstrapCalibrationResult MountGrpcClient::runBootstrapCalibration()
{
    ensureConnected();

    grpc::ClientContext context;
    google::protobuf::Empty request;
    astro_mount::BootstrapCalibrationResult response;

    auto status = stub_->RunBootstrapCalibration(&context, request, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::runBootstrapCalibration failed: " + status.error_message());
    }

    return response;
}

astro_mount::BootstrapStatus MountGrpcClient::getBootstrapStatus()
{
    ensureConnected();

    grpc::ClientContext context;
    google::protobuf::Empty request;
    astro_mount::BootstrapStatus response;

    auto status = stub_->GetBootstrapStatus(&context, request, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::getBootstrapStatus failed: " + status.error_message());
    }

    return response;
}

void MountGrpcClient::clearBootstrapMeasurements()
{
    ensureConnected();

    grpc::ClientContext context;
    google::protobuf::Empty request;
    google::protobuf::Empty response;

    auto status = stub_->ClearBootstrapMeasurements(&context, request, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::clearBootstrapMeasurements failed: " + status.error_message());
    }
}

// ============================================
// Faza 3: Axis control
// ============================================

void MountGrpcClient::controlAxis(const astro_mount::AxisControlRequest& req)
{
    ensureConnected();
    grpc::ClientContext context;
    google::protobuf::Empty response;
    auto status = stub_->ControlAxis(&context, req, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::controlAxis failed: " + status.error_message());
    }
}

void MountGrpcClient::stopAxis(const astro_mount::AxisStopRequest& req)
{
    ensureConnected();
    grpc::ClientContext context;
    google::protobuf::Empty response;
    auto status = stub_->StopAxis(&context, req, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::stopAxis failed: " + status.error_message());
    }
}

void MountGrpcClient::emergencyStop(const astro_mount::EmergencyStopRequest& req)
{
    ensureConnected();
    grpc::ClientContext context;
    google::protobuf::Empty response;
    auto status = stub_->EmergencyStop(&context, req, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::emergencyStop failed: " + status.error_message());
    }
}

// ============================================
// Faza 3: Guider
// ============================================

void MountGrpcClient::connectGuider(const astro_mount::GuiderConfig& config)
{
    ensureConnected();
    grpc::ClientContext context;
    google::protobuf::Empty response;
    auto status = stub_->ConnectGuider(&context, config, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::connectGuider failed: " + status.error_message());
    }
}

void MountGrpcClient::disconnectGuider()
{
    ensureConnected();
    grpc::ClientContext context;
    google::protobuf::Empty request;
    google::protobuf::Empty response;
    auto status = stub_->DisconnectGuider(&context, request, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::disconnectGuider failed: " + status.error_message());
    }
}

void MountGrpcClient::sendGuiderCorrection(const astro_mount::GuiderCorrection& correction)
{
    ensureConnected();
    grpc::ClientContext context;
    google::protobuf::Empty response;
    auto status = stub_->SendGuiderCorrection(&context, correction, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::sendGuiderCorrection failed: " + status.error_message());
    }
}

// ============================================
// Faza 5: Derotator / Field Rotation
// ============================================

void MountGrpcClient::enableFieldRotation(const astro_mount::FieldRotationParams& params)
{
    ensureConnected();
    grpc::ClientContext context;
    google::protobuf::Empty response;
    auto status = stub_->EnableFieldRotation(&context, params, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::enableFieldRotation failed: " + status.error_message());
    }
}

void MountGrpcClient::controlFieldRotation(const astro_mount::FieldRotationControlRequest& req)
{
    ensureConnected();
    grpc::ClientContext context;
    google::protobuf::Empty response;
    auto status = stub_->ControlFieldRotation(&context, req, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::controlFieldRotation failed: " + status.error_message());
    }
}

astro_mount::DerotatorStatus MountGrpcClient::getDerotatorStatus()
{
    ensureConnected();
    grpc::ClientContext context;
    google::protobuf::Empty request;
    astro_mount::DerotatorStatus response;
    auto status = stub_->GetDerotatorStatus(&context, request, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::getDerotatorStatus failed: " + status.error_message());
    }
    return response;
}

void MountGrpcClient::homeDerotator(const astro_mount::DerotatorHomingRequest& req)
{
    ensureConnected();
    grpc::ClientContext context;
    google::protobuf::Empty response;
    auto status = stub_->HomeDerotator(&context, req, &response);
    if (!status.ok())
    {
        throw std::runtime_error(
            "MountGrpcClient::homeDerotator failed: " + status.error_message());
    }
}

// ============================================
// Helper methods
// ============================================

void MountGrpcClient::ensureConnected() const
{
    if (!stub_ || !channel_)
    {
        throw std::runtime_error(
            "MountGrpcClient is not connected. Call connect() first.");
    }
}
