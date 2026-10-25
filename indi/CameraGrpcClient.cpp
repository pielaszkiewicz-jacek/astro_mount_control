#include "CameraGrpcClient.h"
#include <grpcpp/grpcpp.h>
#include "camera.grpc.pb.h"
#include <chrono>
#include <stdexcept>

struct CameraGrpcClient::Impl
{
    std::shared_ptr<grpc::Channel> channel;
    std::unique_ptr<astro_mount::CameraService::Stub> stub;
    std::string host;
    int port{50051};
    bool use_ssl{false};
    std::atomic<grpc::ClientContext*> active_exposure_ctx{nullptr};

    bool isConnected() const
    {
        return channel && channel->GetState(false) == GRPC_CHANNEL_READY;
    }

    void ensureConnected() const
    {
        if (!isConnected())
            throw std::runtime_error("CameraGrpcClient: not connected");
    }
};

CameraGrpcClient::CameraGrpcClient(const std::string& host, int port, bool useSsl)
    : impl_(std::make_unique<Impl>())
{
    impl_->host = host;
    impl_->port = port > 0 ? port : 50051;
    impl_->use_ssl = useSsl;
}

CameraGrpcClient::~CameraGrpcClient() = default;

CameraGrpcClient::CameraGrpcClient(CameraGrpcClient&&) noexcept = default;
CameraGrpcClient& CameraGrpcClient::operator=(CameraGrpcClient&&) noexcept = default;

void CameraGrpcClient::connect()
{
    if (impl_->isConnected())
        return;

    auto creds = impl_->use_ssl
        ? grpc::SslCredentials(grpc::SslCredentialsOptions())
        : grpc::InsecureChannelCredentials();

    // Camera frames (16-bit FITS images) can exceed the default 4 MB gRPC
    // receive limit — a 16 MP sensor produces ~16 MB per frame. Without this,
    // StartExposure fails with RESOURCE_EXHAUSTED when the final image is
    // streamed back from the controller.
    grpc::ChannelArguments channel_args;
    channel_args.SetMaxReceiveMessageSize(64 * 1024 * 1024);
    channel_args.SetMaxSendMessageSize(64 * 1024 * 1024);

    impl_->channel = grpc::CreateCustomChannel(
        impl_->host + ":" + std::to_string(impl_->port), creds, channel_args);
    impl_->stub = astro_mount::CameraService::NewStub(impl_->channel);

    const auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(10);
    if (!impl_->channel->WaitForConnected(deadline))
    {
        disconnect();
        throw std::runtime_error("CameraGrpcClient: failed to connect to " + impl_->host +
                                 ":" + std::to_string(impl_->port) + " within 10s timeout");
    }
}

void CameraGrpcClient::disconnect()
{
    if (!impl_) return;
    impl_->stub.reset();
    if (impl_->channel)
        impl_->channel.reset();
}

bool CameraGrpcClient::isConnected() const
{
    return impl_ && impl_->isConnected();
}

void CameraGrpcClient::reconnect()
{
    disconnect();
    connect();
}

CameraGrpcClient::CameraInfoData CameraGrpcClient::getCameraInfo()
{
    impl_->ensureConnected();

    grpc::ClientContext context;
    google::protobuf::Empty request;
    astro_mount::CameraInfo response;

    auto status = impl_->stub->GetCameraInfo(&context, request, &response);
    if (!status.ok())
        throw std::runtime_error("CameraGrpcClient::getCameraInfo failed: " +
                                 status.error_message());

    CameraInfoData out;
    out.name = response.name();
    out.manufacturer = response.manufacturer();
    out.sensor_name = response.sensor_name();
    out.driver_version = response.driver_version();
    out.width = response.width();
    out.height = response.height();
    out.max_bin = response.max_bin();
    out.num_filters = response.num_filters();
    out.pixel_size_um = response.pixel_size_um();
    out.bit_depth = response.bit_depth();
    out.has_cooler = response.has_cooler();
    out.connected = response.connected();
    for (const auto& f : response.filter_names())
        out.filter_names.push_back(f);
    return out;
}

CameraGrpcClient::ExposureResult CameraGrpcClient::startExposure(
    double exposure_time_s, int gain, int offset, int binning, int filter_position,
    const std::function<void(float, double)>& onProgress)
{
    impl_->ensureConnected();

    astro_mount::ExposureRequest request;
    request.set_exposure_time_s(exposure_time_s);
    request.set_gain(gain);
    request.set_offset(offset);
    request.set_binning(binning);
    request.set_save_to_disk(false);
    if (filter_position >= 0)
        request.mutable_filter()->set_position(filter_position);

    grpc::ClientContext context;
    impl_->active_exposure_ctx = &context;

    std::unique_ptr<grpc::ClientReader<astro_mount::ExposureProgress>> reader =
        impl_->stub->StartExposure(&context, request);

    astro_mount::ExposureProgress msg;
    astro_mount::ExposureProgress last;
    while (reader->Read(&msg))
    {
        last = msg;
        if (onProgress)
            onProgress(static_cast<float>(msg.remaining_ms()) / 1000.0f, msg.progress_percent());
    }

    grpc::Status status = reader->Finish();
    impl_->active_exposure_ctx = nullptr;

    if (!status.ok())
        throw std::runtime_error("CameraGrpcClient::startExposure failed: " +
                                 status.error_message());

    ExposureResult out;
    out.complete = (last.status() == "complete");
    if (last.has_image())
    {
        const auto& img = last.image();
        out.width = img.width();
        out.height = img.height();
        out.bit_depth = img.bit_depth();
        out.format = img.format();
        out.image_bytes = img.data();
        out.exposure_time_s = img.exposure_time_s();
        out.gain = img.gain();
        out.binning = img.binning();
    }
    return out;
}

void CameraGrpcClient::abortExposure()
{
    if (impl_)
    {
        auto* ctx = impl_->active_exposure_ctx.load();
        if (ctx)
            ctx->TryCancel();
    }

    try
    {
        impl_->ensureConnected();
        grpc::ClientContext context;
        google::protobuf::Empty request;
        google::protobuf::Empty response;
        auto status = impl_->stub->AbortExposure(&context, request, &response);
        (void)status;
    }
    catch (const std::exception&)
    {
        // Abort is best-effort — the streaming cancel above is authoritative.
    }
}

void CameraGrpcClient::setFilter(int position)
{
    impl_->ensureConnected();

    grpc::ClientContext context;
    astro_mount::FilterRequest request;
    request.set_position(position);
    google::protobuf::Empty response;

    auto status = impl_->stub->SetFilter(&context, request, &response);
    if (!status.ok())
        throw std::runtime_error("CameraGrpcClient::setFilter failed: " +
                                 status.error_message());
}

CameraGrpcClient::CoolerStatusData CameraGrpcClient::setCooler(double target_c, bool enabled)
{
    impl_->ensureConnected();

    grpc::ClientContext context;
    astro_mount::SetCoolerRequest request;
    request.set_target_c(target_c);
    request.set_enabled(enabled);
    astro_mount::CoolerStatus response;

    auto status = impl_->stub->SetCooler(&context, request, &response);
    if (!status.ok())
        throw std::runtime_error("CameraGrpcClient::setCooler failed: " +
                                 status.error_message());

    CoolerStatusData out;
    out.enabled = response.enabled();
    out.target_c = response.target_c();
    out.current_c = response.current_c();
    out.power_percent = response.power_percent();
    out.reached_target = response.reached_target();
    return out;
}

CameraGrpcClient::CoolerStatusData CameraGrpcClient::getCoolerStatus()
{
    impl_->ensureConnected();

    grpc::ClientContext context;
    google::protobuf::Empty request;
    astro_mount::CoolerStatus response;

    auto status = impl_->stub->GetCoolerStatus(&context, request, &response);
    if (!status.ok())
        throw std::runtime_error("CameraGrpcClient::getCoolerStatus failed: " +
                                 status.error_message());

    CoolerStatusData out;
    out.enabled = response.enabled();
    out.target_c = response.target_c();
    out.current_c = response.current_c();
    out.power_percent = response.power_percent();
    out.reached_target = response.reached_target();
    return out;
}
