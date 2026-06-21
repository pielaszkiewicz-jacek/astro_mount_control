#include "api/grpc_server.h"
#include "api/service_impl.h"
#include "logging/logger.h"
#include <grpcpp/grpcpp.h>
#include <memory>
#include <thread>
#include <atomic>
#include <fstream>
#include <sstream>

namespace astro_mount {
namespace api {

class GrpcServer::Impl {
public:
    Impl(const std::string& address, int port, controllers::MountController& controller,
         bool enable_ssl, const std::string& ssl_cert_path, const std::string& ssl_key_path)
        : address_(address + ":" + std::to_string(port))
        , controller_(controller)
        , enable_ssl_(enable_ssl)
        , ssl_cert_path_(ssl_cert_path)
        , ssl_key_path_(ssl_key_path)
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
        
        // Configure SSL if enabled
        if (enable_ssl_) {
            auto logger = logging::Logger::get("api");
            
            // Read certificate
            std::ifstream cert_file(ssl_cert_path_);
            if (!cert_file.is_open()) {
                logger->error("gRPC SSL: failed to open certificate file: {}", ssl_cert_path_);
                return false;
            }
            std::stringstream cert_ss;
            cert_ss << cert_file.rdbuf();
            std::string cert_chain = cert_ss.str();
            cert_file.close();
            
            // Read private key
            std::ifstream key_file(ssl_key_path_);
            if (!key_file.is_open()) {
                logger->error("gRPC SSL: failed to open key file: {}", ssl_key_path_);
                return false;
            }
            std::stringstream key_ss;
            key_ss << key_file.rdbuf();
            std::string private_key = key_ss.str();
            key_file.close();
            
            grpc::SslServerCredentialsOptions ssl_opts;
            ssl_opts.pem_key_cert_pairs.push_back({private_key, cert_chain});
            
            builder.AddListeningPort(address_, grpc::SslServerCredentials(ssl_opts));
            logger->info("gRPC SSL enabled: cert={}, key={}", ssl_cert_path_, ssl_key_path_);
        } else {
            builder.AddListeningPort(address_, grpc::InsecureServerCredentials());
        }
        
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
    bool enable_ssl_;
    std::string ssl_cert_path_;
    std::string ssl_key_path_;
    std::unique_ptr<MountControllerServiceImpl> service_;
    std::unique_ptr<grpc::Server> server_;
    std::thread server_thread_;
    std::atomic<bool> running_;
};

GrpcServer::GrpcServer(const std::string& address, int port,
                       controllers::MountController& controller,
                       bool enable_ssl,
                       const std::string& ssl_cert_path,
                       const std::string& ssl_key_path)
    : pimpl(std::make_unique<Impl>(address, port, controller,
                                   enable_ssl, ssl_cert_path, ssl_key_path)) {}

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