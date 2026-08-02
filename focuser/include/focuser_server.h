#ifndef FOCUSER_SERVER_H
#define FOCUSER_SERVER_H

#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>

namespace astro_focuser {

class FocuserServiceImpl;

class FocuserServer {
public:
    FocuserServer(const std::string& server_address,
                  const std::string& focuser_config_path,
                  bool enable_ssl = false,
                  const std::string& ssl_cert_path = "",
                  const std::string& ssl_key_path = "");
    ~FocuserServer();
    bool Start();
    void Stop();
    void Wait();
    FocuserServer(const FocuserServer&) = delete;
    FocuserServer& operator=(const FocuserServer&) = delete;
    FocuserServer(FocuserServer&&) = delete;
    FocuserServer& operator=(FocuserServer&&) = delete;
private:
    std::string server_address_;
    std::string focuser_config_path_;
    bool enable_ssl_;
    std::string ssl_cert_path_;
    std::string ssl_key_path_;
    std::unique_ptr<FocuserServiceImpl> service_;
    std::unique_ptr<grpc::Server> server_;
};

} // namespace astro_focuser

#endif // FOCUSER_SERVER_H
