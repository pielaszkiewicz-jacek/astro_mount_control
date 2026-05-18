#ifndef GRPC_SERVER_H
#define GRPC_SERVER_H

#include <string>
#include <memory>

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
 */
class GrpcServer {
public:
    /**
     * @brief Construct a new GrpcServer object
     * @param address Network address to bind to
     * @param port Port to listen on
     * @param controller Reference to mount controller
     */
    GrpcServer(const std::string& address, int port, 
               controllers::MountController& controller);
    
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