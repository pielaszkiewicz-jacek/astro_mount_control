#include "api/grpc_server.h"
#include "api/service_impl.h"
#include <grpcpp/grpcpp.h>
#include <memory>
#include <thread>
#include <atomic>

namespace astro_mount {
namespace api {

class GrpcServer::Impl {
public:
    Impl(const std::string& address, int port, controllers::MountController& controller)
        : address_(address + ":" + std::to_string(port))
        , controller_(controller)
        , server_(nullptr)
        , running_(false) {}
    
    ~Impl() {
        stop();
    }
    
    bool start() {
        if (running_) {
            return false;
        }
        
        grpc::ServerBuilder builder;
        builder.AddListeningPort(address_, grpc::InsecureServerCredentials());
        
        // Create service implementation
        service_ = std::make_unique<MountControllerServiceImpl>(controller_);
        builder.RegisterService(service_.get());
        
        // Set server options
        builder.SetMaxReceiveMessageSize(64 * 1024 * 1024); // 64MB
        builder.SetMaxSendMessageSize(64 * 1024 * 1024);    // 64MB
        builder.SetMaxMessageSize(64 * 1024 * 1024);        // 64MB
        
        // Build and start server
        server_ = builder.BuildAndStart();
        if (!server_) {
            return false;
        }
        
        running_ = true;
        
        // Start server thread
        server_thread_ = std::thread([this]() {
            server_->Wait();
        });
        
        return true;
    }
    
    void stop() {
        if (!running_) {
            return;
        }
        
        running_ = false;
        
        if (server_) {
            server_->Shutdown();
            server_->Wait();
        }
        
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }
    
    std::string getAddress() const {
        return address_;
    }
    
    bool isRunning() const {
        return running_;
    }
    
private:
    std::string address_;
    controllers::MountController& controller_;
    std::unique_ptr<MountControllerServiceImpl> service_;
    std::unique_ptr<grpc::Server> server_;
    std::thread server_thread_;
    std::atomic<bool> running_;
};

GrpcServer::GrpcServer(const std::string& address, int port, 
                       controllers::MountController& controller)
    : pimpl(std::make_unique<Impl>(address, port, controller)) {}

GrpcServer::~GrpcServer() = default;

bool GrpcServer::start() {
    return pimpl->start();
}

void GrpcServer::stop() {
    pimpl->stop();
}

std::string GrpcServer::getAddress() const {
    return pimpl->getAddress();
}

bool GrpcServer::isRunning() const {
    return pimpl->isRunning();
}

} // namespace api
} // namespace astro_mount