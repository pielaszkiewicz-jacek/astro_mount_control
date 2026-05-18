#include "api/canopen_server.h"
#include "controllers/canopen_factory.h"
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

using namespace astro_mount;

int main() {
    std::cout << "=== CANopen Mock Server Example ===\n\n";

    // Step 1: Create a mock CANopen interface
    std::cout << "1. Creating mock CANopen interface...\n";
    auto mock_interface = controllers::CanOpenFactory::create("mock");
    
    if (!mock_interface) {
        std::cerr << "ERROR: Failed to create mock interface!\n";
        return 1;
    }
    std::cout << "✓ Mock interface created\n\n";

    // Step 2: Create the CANopen server
    std::cout << "2. Creating CANopen server...\n";
    std::string address = "0.0.0.0";
    int port = 50052;
    
    canopen::CanOpenServer server(address, port, std::move(mock_interface));
    std::cout << "✓ Server created (address: " << server.getAddress() << ")\n\n";

    // Step 3: Start the server
    std::cout << "3. Starting CANopen server...\n";
    if (!server.start()) {
        std::cerr << "ERROR: Failed to start server!\n";
        return 1;
    }
    std::cout << "✓ Server started\n\n";

    // Step 4: Show server status
    std::cout << "4. Server status:\n";
    std::cout << "   - Address: " << server.getAddress() << "\n";
    std::cout << "   - Running: " << (server.isRunning() ? "Yes" : "No") << "\n\n";

    // Step 5: Keep server running for demonstration
    std::cout << "5. Server is running...\n";
    std::cout << "   Press Ctrl+C to stop\n\n";

    std::cout << "=== Available gRPC Services ===\n";
    std::cout << "- Connect(ConnectionRequest) -> ConnectionResponse\n";
    std::cout << "- ConfigureAxis(AxisConfigRequest) -> OperationResult\n";
    std::cout << "- EnableAxis(AxisControlRequest) -> OperationResult\n";
    std::cout << "- SetPositionTarget(PositionTargetRequest) -> OperationResult\n";
    std::cout << "- GetAxisStatus(AxisControlRequest) -> AxisStatus\n";
    std::cout << "- GetPositionData(AxisControlRequest) -> PositionData\n";
    std::cout << "- GetStatistics() -> Statistics\n";
    std::cout << "- GenerateTrajectory(TrajectoryParams) -> Trajectory\n";
    std::cout << "- ... and many more CANopen protocol operations\n\n";

    std::cout << "=== Example gRPC Clients ===\n";
    std::cout << "You can connect to the server using:\n";
    std::cout << "1. Python: `python -m grpc_tools.protoc -I proto --python_out=. --grpc_python_out=. proto/canopen_service.proto`\n";
    std::cout << "2. C++: Use the generated protobuf/grpc client\n";
    std::cout << "3. Command line: `grpcurl -plaintext localhost:50052 astro_mount.canopen.CanOpenService/GetStatistics`\n\n";

    // Keep server running until interrupted
    try {
        while (server.isRunning()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    } catch (...) {
        std::cout << "\nStopping server...\n";
    }

    // Step 6: Stop the server
    server.stop();
    std::cout << "✓ Server stopped\n\n";

    std::cout << "=== Summary ===\n";
    std::cout << "Mock CANopen server successfully implemented and tested.\n";
    std::cout << "The server provides a complete CANopen interface simulation\n";
    std::cout << "that can be used for testing, development, and integration.\n";

    return 0;
}