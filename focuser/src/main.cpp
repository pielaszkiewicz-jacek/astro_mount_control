#include "focuser/include/focuser_server.h"
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <unistd.h>

volatile sig_atomic_t stop_flag = 0;
static void signal_handler(int signum) { stop_flag = 1; }

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::string server_address = "0.0.0.0:50058";
    std::string focuser_config = "config/focuser_config.json";
    bool enable_ssl = false;
    std::string ssl_cert, ssl_key;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--address" && i + 1 < argc) server_address = argv[++i];
        else if (arg == "--config" && i + 1 < argc) focuser_config = argv[++i];
        else if (arg == "--ssl" && i + 1 < argc) {
            enable_ssl = true; ssl_cert = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') ssl_key = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "  --address HOST:PORT  (default: 0.0.0.0:50058)\n";
            std::cout << "  --config PATH        (default: config/focuser_config.json)\n";
            std::cout << "  --ssl CERT KEY\n"; return 0;
        }
    }

    std::cout << "Starting Focuser Server\n  Address: " << server_address
              << "\n  Config: " << focuser_config << "\n";
    try {
        astro_focuser::FocuserServer s(server_address, focuser_config, enable_ssl, ssl_cert, ssl_key);
        if (!s.Start()) { std::cerr << "Failed to start focuser server\n"; return 1; }
        std::cout << "Focuser server started. Press Ctrl+C to stop.\n";
        while (!stop_flag) sleep(1);
        std::cout << "\nShutting down...\n"; s.Stop();
        std::cout << "Focuser server stopped.\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n"; return 1;
    }
    return 0;
}
