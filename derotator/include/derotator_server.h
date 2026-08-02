#ifndef DEROTATOR_SERVER_H
#define DEROTATOR_SERVER_H

#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>

namespace astro_derotator {

class DerotatorServiceImpl;

class DerotatorServer {
public:
    DerotatorServer(const std::string& server_address,
                    const std::string& config_path,
                    bool enable_ssl = false,
                    const std::string& ssl_cert_path = "",
                    const std::string& ssl_key_path = "");
    ~DerotatorServer();

    bool Start();
    void Stop();
    void Wait();

    DerotatorServer(const DerotatorServer&) = delete;
    DerotatorServer& operator=(const DerotatorServer&) = delete;
    DerotatorServer(DerotatorServer&&) = delete;
    DerotatorServer& operator=(DerotatorServer&&) = delete;

private:
    std::string server_address_;
    std::string config_path_;
    bool enable_ssl_;
    std::string ssl_cert_path_;
    std::string ssl_key_path_;
    std::unique_ptr<DerotatorServiceImpl> service_;
    std::unique_ptr<grpc::Server> server_;
};

} // namespace astro_derotator

#endif // DEROTATOR_SERVER_H
