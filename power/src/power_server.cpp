#include "power/include/power_server.h"
#include "power/include/power_service_impl.h"
#include <grpcpp/grpcpp.h>
#include <fstream>
#include <iostream>

namespace astro_power {

PowerServer::PowerServer(const std::string& server_address,
                         const std::string& power_config_path,
                         bool enable_ssl,
                         const std::string& ssl_cert_path,
                         const std::string& ssl_key_path)
    : server_address_(server_address),
      power_config_path_(power_config_path),
      enable_ssl_(enable_ssl),
      ssl_cert_path_(ssl_cert_path),
      ssl_key_path_(ssl_key_path) {}

PowerServer::~PowerServer() { Stop(); }

bool PowerServer::Start() {
    try {
        service_ = std::make_unique<PowerServiceImpl>(power_config_path_);
        grpc::ServerBuilder builder;

        if (enable_ssl_) {
            std::string cert, key;
            std::ifstream cert_file(ssl_cert_path_);
            std::ifstream key_file(ssl_key_path_);
            if (!cert_file.is_open() || !key_file.is_open()) {
                std::cerr << "[PowerServer] Failed to open SSL cert/key files\n";
                return false;
            }
            cert = std::string(std::istreambuf_iterator<char>(cert_file), {});
            key = std::string(std::istreambuf_iterator<char>(key_file), {});
            grpc::SslServerCredentialsOptions::PemKeyCertPair key_cert_pair;
            key_cert_pair.private_key = key;
            key_cert_pair.cert_chain = cert;
            grpc::SslServerCredentialsOptions ssl_opts;
            ssl_opts.pem_key_cert_pairs.push_back(key_cert_pair);
            builder.AddListeningPort(server_address_, grpc::SslServerCredentials(ssl_opts));
        } else {
            builder.AddListeningPort(server_address_, grpc::InsecureServerCredentials());
        }

        builder.RegisterService(service_.get());
        builder.SetMaxReceiveMessageSize(4 * 1024 * 1024);
        builder.SetMaxSendMessageSize(4 * 1024 * 1024);

        server_ = builder.BuildAndStart();
        if (!server_) {
            std::cerr << "[PowerServer] Failed to build and start gRPC server\n";
            return false;
        }
        std::cout << "[PowerServer] Listening on " << server_address_ << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[PowerServer] Startup error: " << e.what() << "\n";
        return false;
    }
}

void PowerServer::Stop() {
    if (server_) { server_->Shutdown(); server_.reset(); }
    service_.reset();
}

void PowerServer::Wait() {
    if (server_) server_->Wait();
}

} // namespace astro_power
