/**
 * Derotator Control Server - Standalone process
 *
 * Provides gRPC API for field derotator control (mode, angle, rate,
 * homing, status, field rotation calculation).
 * Operates as an independent process on port 50054.
 */

#include "derotator/include/derotator_server.h"
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <unistd.h>

volatile sig_atomic_t stop_flag = 0;

static void signal_handler(int signum) {
    stop_flag = 1;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::string server_address = "0.0.0.0:50054";
    std::string config_path = "config/derotator_config.json";
    bool enable_ssl = false;
    std::string ssl_cert;
    std::string ssl_key;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--address" && i + 1 < argc) {
            server_address = argv[++i];
        } else if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--ssl" && i + 1 < argc) {
            enable_ssl = true;
            ssl_cert = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                ssl_key = argv[++i];
            }
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "  --address HOST:PORT  Server address (default: 0.0.0.0:50054)\n";
            std::cout << "  --config PATH        Derotator config file\n";
            std::cout << "  --ssl CERT KEY       Enable TLS\n";
            std::cout << "  --help               Show this message\n";
            return 0;
        }
    }

    std::cout << "Starting Derotator Control Server\n";
    std::cout << "  Server address: " << server_address << "\n";
    std::cout << "  Config:         " << config_path << "\n";

    try {
        astro_derotator::DerotatorServer server(server_address, config_path,
                                                 enable_ssl, ssl_cert, ssl_key);
        if (!server.Start()) {
            std::cerr << "Failed to start derotator server\n";
            return 1;
        }
        std::cout << "Derotator server started successfully\n";
        std::cout << "Press Ctrl+C to stop\n";

        while (!stop_flag) sleep(1);

        std::cout << "\nShutting down derotator server...\n";
        server.Stop();
        std::cout << "Derotator server stopped\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
