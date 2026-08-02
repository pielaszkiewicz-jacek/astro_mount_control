#include "focuser/include/focuser_server.h"
#include "focuser/include/focuser_service_impl.h"
#include <grpcpp/grpcpp.h>
#include <fstream>
#include <iostream>

namespace astro_focuser {

FocuserServer::FocuserServer(const std::string& server_address,
                             const std::string& focuser_config_path,
                             bool enable_ssl, const std::string& ssl_cert_path,
                             const std::string& ssl_key_path)
    : server_address_(server_address), focuser_config_path_(focuser_config_path),
      enable_ssl_(enable_ssl), ssl_cert_path_(ssl_cert_path), ssl_key_path_(ssl_key_path) {}
FocuserServer::~FocuserServer() { Stop(); }

bool FocuserServer::Start() {
    try {
        service_ = std::make_unique<FocuserServiceImpl>(focuser_config_path_);
        grpc::ServerBuilder builder;
        if (enable_ssl_) {
            std::string cert, key;
            std::ifstream cert_file(ssl_cert_path_);
            std::ifstream key_file(ssl_key_path_);
            if (!cert_file.is_open() || !key_file.is_open()) return false;
            cert = std::string(std::istreambuf_iterator<char>(cert_file), {});
            key = std::string(std::istreambuf_iterator<char>(key_file), {});
            grpc::SslServerCredentialsOptions::PemKeyCertPair p;
            p.private_key = key; p.cert_chain = cert;
            grpc::SslServerCredentialsOptions s;
            s.pem_key_cert_pairs.push_back(p);
            builder.AddListeningPort(server_address_, grpc::SslServerCredentials(s));
        } else {
            builder.AddListeningPort(server_address_, grpc::InsecureServerCredentials());
        }
        builder.RegisterService(service_.get());
        builder.SetMaxReceiveMessageSize(4*1024*1024);
        builder.SetMaxSendMessageSize(4*1024*1024);
        server_ = builder.BuildAndStart();
        if (!server_) { std::cerr << "[FocuserServer] Failed to start\n"; return false; }
        std::cout << "[FocuserServer] Listening on " << server_address_ << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[FocuserServer] Error: " << e.what() << "\n"; return false;
    }
}

void FocuserServer::Stop() { if (server_) { server_->Shutdown(); server_.reset(); } service_.reset(); }
void FocuserServer::Wait() { if (server_) server_->Wait(); }

} // namespace astro_focuser
