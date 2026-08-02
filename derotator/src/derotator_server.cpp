#include "derotator/include/derotator_server.h"
#include "derotator/include/derotator_service_impl.h"
#include <fstream>
#include <iostream>

namespace astro_derotator {

DerotatorServer::DerotatorServer(const std::string& server_address,
                                 const std::string& config_path,
                                 bool enable_ssl,
                                 const std::string& ssl_cert_path,
                                 const std::string& ssl_key_path)
    : server_address_(server_address),
      config_path_(config_path),
      enable_ssl_(enable_ssl),
      ssl_cert_path_(ssl_cert_path),
      ssl_key_path_(ssl_key_path) {}

DerotatorServer::~DerotatorServer() { Stop(); }

bool DerotatorServer::Start() {
    try {
        service_ = std::make_unique<DerotatorServiceImpl>(config_path_);
        grpc::ServerBuilder builder;

        if (enable_ssl_) {
            std::string cert, key;
            std::ifstream cf(ssl_cert_path_), kf(ssl_key_path_);
            if (!cf.is_open() || !kf.is_open()) return false;
            cert = std::string(std::istreambuf_iterator<char>(cf), {});
            key = std::string(std::istreambuf_iterator<char>(kf), {});
            grpc::SslServerCredentialsOptions::PemKeyCertPair p;
            p.private_key = key; p.cert_chain = cert;
            grpc::SslServerCredentialsOptions ssl_opts;
            ssl_opts.pem_key_cert_pairs.push_back(p);
            builder.AddListeningPort(server_address_, grpc::SslServerCredentials(ssl_opts));
        } else {
            builder.AddListeningPort(server_address_, grpc::InsecureServerCredentials());
        }

        builder.RegisterService(service_.get());
        builder.SetMaxReceiveMessageSize(4 * 1024 * 1024);
        builder.SetMaxSendMessageSize(4 * 1024 * 1024);

        server_ = builder.BuildAndStart();
        if (!server_) return false;
        std::cout << "[DerotatorServer] Listening on " << server_address_ << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[DerotatorServer] Startup error: " << e.what() << "\n";
        return false;
    }
}

void DerotatorServer::Stop() {
    if (server_) { server_->Shutdown(); server_.reset(); }
    service_.reset();
}

void DerotatorServer::Wait() { if (server_) server_->Wait(); }

} // namespace astro_derotator
