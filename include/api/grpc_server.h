#ifndef GRPC_SERVER_H
#define GRPC_SERVER_H

#include <string>
#include <memory>
#include <vector>

namespace grpc {
class Service;
} // namespace grpc

namespace astro_mount {
namespace controllers {
class MountController;
} // namespace controllers

namespace api {

/**
 * @brief gRPC server for mount controller
 *
 * Manages the gRPC server lifecycle and provides remote access
 * to the mount controller functionality.
 *
 * Since the dome, derotator and focuser subsystems are hosted
 * in-process within the mount controller, the same gRPC server can
 * register their services as well. Use registerService() before
 * calling start().
 */
class GrpcServer {
public:
    /**
     * @brief Construct a new GrpcServer object
     * @param address Network address to bind to
     * @param port Port to listen on
     * @param controller Reference to mount controller
     * @param enable_ssl Enable TLS encryption
     * @param ssl_cert_path Path to SSL certificate file (PEM)
     * @param ssl_key_path Path to SSL private key file (PEM)
     */
    GrpcServer(const std::string& address, int port,
               controllers::MountController& controller,
               bool enable_ssl = false,
               const std::string& ssl_cert_path = "",
               const std::string& ssl_key_path = "");
    
    ~GrpcServer();
    
    /**
     * @brief Start the gRPC server
     * @return True if server started successfully
     */
    bool start();
    
    /**
     * @brief Stop the gRPC server
     */
    void stop();
    
    /**
     * @brief Get server address
     * @return Server address string
     */
    std::string getAddress() const;
    
    /**
     * @brief Check if server is running
     * @return True if server is running
     */
    bool isRunning() const;

    /**
     * @brief Register an additional gRPC service to serve in-process.
     *
     * The service is registered on every listening address of this
     * server. Must be called before start().
     *
     * @param service Pointer to a grpc::Service implementation whose
     *                lifetime must outlive this server.
     */
    void registerService(grpc::Service* service);

    // Non-copyable
    GrpcServer(const GrpcServer&) = delete;
    GrpcServer& operator=(const GrpcServer&) = delete;
    
    // Non-movable
    GrpcServer(GrpcServer&&) = delete;
    GrpcServer& operator=(GrpcServer&&) = delete;
    
private:
    class Impl;
    std::unique_ptr<Impl> pimpl;
};

} // namespace api
} // namespace astro_mount

#endif // GRPC_SERVER_H
