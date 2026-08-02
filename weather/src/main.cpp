/**
 * Weather Monitoring Server - Standalone process
 *
 * Provides gRPC API for environmental monitoring, weather alerts,
 * and automatic safety actions (auto-park notification).
 * Operates as an independent process communicating via gRPC on a configurable port.
 *
 * Integrates with the mount controller via gRPC — the mount controller
 * polls this service for weather status and can trigger auto-park when
 * dangerous conditions are detected. All integration is configurable
 * and disabled by default (see ExternalIntegrationConfig::weather_enabled).
 */

#include "weather/include/weather_server.h"
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
    std::string server_address = "0.0.0.0:50055";
    std::string weather_config = "config/weather_config.json";
    bool enable_ssl = false;
    std::string ssl_cert;
    std::string ssl_key;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--address" && i + 1 < argc) {
            server_address = argv[++i];
        } else if (arg == "--config" && i + 1 < argc) {
            weather_config = argv[++i];
        } else if (arg == "--ssl" && i + 1 < argc) {
            enable_ssl = true;
            ssl_cert = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                ssl_key = argv[++i];
            }
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --address HOST:PORT  Server address (default: 0.0.0.0:50055)\n";
            std::cout << "  --config PATH        Weather config file (default: config/weather_config.json)\n";
            std::cout << "  --ssl CERT KEY       Enable TLS with cert and key files\n";
            std::cout << "  --help               Show this help message\n";
            return 0;
        }
    }

    std::cout << "Starting Weather Monitoring Server\n";
    std::cout << "  Server address: " << server_address << "\n";
    std::cout << "  Weather config: " << weather_config << "\n";

    try {
        astro_weather::WeatherServer server(server_address, weather_config,
                                            enable_ssl, ssl_cert, ssl_key);

        if (!server.Start()) {
            std::cerr << "Failed to start weather server\n";
            return 1;
        }

        std::cout << "Weather server started successfully\n";
        std::cout << "Press Ctrl+C to stop\n";

        // Wait for stop signal
        while (!stop_flag) {
            sleep(1);
        }

        std::cout << "\nShutting down weather server...\n";
        server.Stop();
        std::cout << "Weather server stopped\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
