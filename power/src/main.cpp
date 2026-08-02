/**
 * Power Management Server - Standalone process
 *
 * Provides gRPC API for power monitoring (voltage, current, battery status)
 * and output switching. Operates as an independent process on a configurable port.
 */

#include "power/include/power_server.h"
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <unistd.h>

volatile sig_atomic_t stop_flag = 0;

static void signal_handler(int signum) { stop_flag = 1; }

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::string server_address = "0.0.0.0:50056";
    std::string power_config = "config/power_config.json";
    bool enable_ssl = false;
    std::string ssl_cert, ssl_key;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--address" && i + 1 < argc) server_address = argv[++i];
        else if (arg == "--config" && i + 1 < argc) power_config = argv[++i];
        else if (arg == "--ssl" && i + 1 < argc) {
            enable_ssl = true;
            ssl_cert = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') ssl_key = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "  --address HOST:PORT  (default: 0.0.0.0:50056)\n";
            std::cout << "  --config PATH        (default: config/power_config.json)\n";
            std::cout << "  --ssl CERT KEY\n";
            return 0;
        }
    }

    std::cout << "Starting Power Management Server\n";
    std::cout << "  Address: " << server_address << "\n";
    std::cout << "  Config:  " << power_config << "\n";

    try {
        astro_power::PowerServer server(server_address, power_config, enable_ssl, ssl_cert, ssl_key);
        if (!server.Start()) { std::cerr << "Failed to start power server\n"; return 1; }
        std::cout << "Power server started. Press Ctrl+C to stop.\n";
        while (!stop_flag) sleep(1);
        std::cout << "\nShutting down...\n";
        server.Stop();
        std::cout << "Power server stopped.\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
