#ifndef CANOPEN_SERVER_H
#define CANOPEN_SERVER_H

#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "controllers/icanopen_interface.h"
#include "proto/canopen_service.grpc.pb.h"

namespace astro_mount {
namespace canopen {

/**
 * @brief gRPC server for CANopen interface
 * 
 * Provides remote access to CANopen functionality via gRPC.
 * Can be run as a separate application or integrated.
 */
class CanOpenServer {
public:
    /**
     * @brief Construct a new CanOpenServer
     * @param address Network address to bind to
     * @param port Port to listen on
     * @param canopen_interface Underlying CANopen interface implementation
     */
    CanOpenServer(const std::string& address, int port,
                  std::unique_ptr<controllers::ICanOpenInterface> canopen_interface);
    
    ~CanOpenServer();
    
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
    CanOpenServer(const CanOpenServer&) = delete;
    CanOpenServer& operator=(const CanOpenServer&) = delete;
    
    // Non-movable
    CanOpenServer(CanOpenServer&&) = delete;
    CanOpenServer& operator=(CanOpenServer&&) = delete;

private:
    // Internal service implementation
    class ServiceImpl;
    
    std::unique_ptr<ServiceImpl> service_impl_;
    std::unique_ptr<grpc::Server> server_;
    std::string address_;
    int port_;
    bool running_;
};

} // namespace canopen
} // namespace astro_mount

#endif // CANOPEN_SERVER_H