/**
 * Dome Control Server - Standalone process
 *
 * Provides gRPC API for dome control (shutter open/close, rotation, park,
 * auto-sync with mount). Operates as an independent process communicating
 * via gRPC on a configurable port.
 */

#include "dome/include/dome_server.h"
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <unistd.h>

volatile sig_atomic_t stop_flag = 0;

static void signal_handler(int signum) {
    stop_flag = 1;
}

int main(int argc, char* argv[]) {
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Default configuration
    std::string server_address = "0.0.0.0:50053";
    std::string dome_config = "config/dome_config.json";
    bool enable_ssl = false;
    std::string ssl_cert;
    std::string ssl_key;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--address" && i + 1 < argc) {
            server_address = argv[++i];
        } else if (arg == "--config" && i + 1 < argc) {
            dome_config = argv[++i];
        } else if (arg == "--ssl" && i + 1 < argc) {
            enable_ssl = true;
            ssl_cert = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                ssl_key = argv[++i];
            }
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --address HOST:PORT  Server address (default: 0.0.0.0:50053)\n";
            std::cout << "  --config PATH        Dome HAL config file (default: config/dome_config.json)\n";
            std::cout << "  --ssl CERT KEY       Enable TLS with cert and key files\n";
            std::cout << "  --help               Show this help message\n";
            return 0;
        }
    }

    std::cout << "Starting Dome Control Server\n";
    std::cout << "  Server address: " << server_address << "\n";
    std::cout << "  Dome config:    " << dome_config << "\n";

    try {
        astro_dome::DomeServer server(server_address, dome_config,
                                      enable_ssl, ssl_cert, ssl_key);

        if (!server.Start()) {
            std::cerr << "Failed to start dome server\n";
            return 1;
        }

        std::cout << "Dome server started successfully\n";
        std::cout << "Press Ctrl+C to stop\n";

        // Wait for stop signal
        while (!stop_flag) {
            sleep(1);
        }

        std::cout << "\nShutting down dome server...\n";
        server.Stop();
        std::cout << "Dome server stopped\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
