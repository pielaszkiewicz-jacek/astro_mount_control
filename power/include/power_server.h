#ifndef POWER_SERVER_H
#define POWER_SERVER_H

#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>

namespace astro_power {

class PowerServiceImpl;

class PowerServer {
public:
    PowerServer(const std::string& server_address,
                const std::string& power_config_path,
                bool enable_ssl = false,
                const std::string& ssl_cert_path = "",
                const std::string& ssl_key_path = "");
    ~PowerServer();

    bool Start();
    void Stop();
    void Wait();

    PowerServer(const PowerServer&) = delete;
    PowerServer& operator=(const PowerServer&) = delete;
    PowerServer(PowerServer&&) = delete;
    PowerServer& operator=(PowerServer&&) = delete;

private:
    std::string server_address_;
    std::string power_config_path_;
    bool enable_ssl_;
    std::string ssl_cert_path_;
    std::string ssl_key_path_;
    std::unique_ptr<PowerServiceImpl> service_;
    std::unique_ptr<grpc::Server> server_;
};

} // namespace astro_power

#endif // POWER_SERVER_H
