#ifndef DOME_SERVER_H
#define DOME_SERVER_H

#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>

namespace astro_dome {

class DomeServiceImpl;

/**
 * @brief gRPC server for the dome control subsystem
 *
 * Runs as a standalone process, exposing dome control operations
 * via gRPC on a configurable address:port.
 */
class DomeServer {
public:
    /**
     * @brief Construct a new DomeServer
     * @param server_address Address to bind (e.g. "0.0.0.0:50053")
     * @param dome_config_path Path to JSON configuration for dome HAL
     * @param enable_ssl Enable TLS encryption
     * @param ssl_cert_path Path to PEM certificate file
     * @param ssl_key_path Path to PEM private key file
     */
    DomeServer(const std::string& server_address,
               const std::string& dome_config_path,
               bool enable_ssl = false,
               const std::string& ssl_cert_path = "",
               const std::string& ssl_key_path = "");
    ~DomeServer();

    /// Start the gRPC server (blocks until Stop is called)
    bool Start();

    /// Stop the gRPC server gracefully
    void Stop();

    /// Wait for the server to shut down
    void Wait();

    // Non-copyable
    DomeServer(const DomeServer&) = delete;
    DomeServer& operator=(const DomeServer&) = delete;

    // Non-movable
    DomeServer(DomeServer&&) = delete;
    DomeServer& operator=(DomeServer&&) = delete;

private:
    std::string server_address_;
    std::string dome_config_path_;
    bool enable_ssl_;
    std::string ssl_cert_path_;
    std::string ssl_key_path_;
    std::unique_ptr<DomeServiceImpl> service_;
    std::unique_ptr<grpc::Server> server_;
};

} // namespace astro_dome

#endif // DOME_SERVER_H
