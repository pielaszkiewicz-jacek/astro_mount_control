#include "object_database_service.h"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

volatile sig_atomic_t stop = 0;

void signal_handler(int signum) {
    stop = 1;
}

int main(int argc, char* argv[]) {
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Default configuration
    std::string server_address = "0.0.0.0:50051";
    std::string db_path = "astronomy_objects.db";
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--address" && i + 1 < argc) {
            server_address = argv[++i];
        } else if (arg == "--db" && i + 1 < argc) {
            db_path = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --address HOST:PORT  Server address (default: 0.0.0.0:50051)\n";
            std::cout << "  --db PATH            Database file path (default: astronomy_objects.db)\n";
            std::cout << "  --help               Show this help message\n";
            return 0;
        }
    }
    
    std::cout << "Starting Astronomical Object Database Server\n";
    std::cout << "Server address: " << server_address << "\n";
    std::cout << "Database path: " << db_path << "\n";
    
    try {
        astro_objects::ObjectDatabaseServer server(server_address, db_path);
        
        if (!server.Start()) {
            std::cerr << "Failed to start server\n";
            return 1;
        }
        
        std::cout << "Server started successfully\n";
        std::cout << "Press Ctrl+C to stop\n";
        
        // Wait for stop signal
        while (!stop) {
            sleep(1);
        }
        
        std::cout << "\nShutting down server...\n";
        server.Stop();
        std::cout << "Server stopped\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}