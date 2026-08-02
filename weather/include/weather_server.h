#ifndef WEATHER_SERVER_H
#define WEATHER_SERVER_H

#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>

namespace astro_weather {

class WeatherServiceImpl;

/**
 * @brief gRPC server for the weather monitoring subsystem
 *
 * Runs as a standalone process, exposing weather monitoring operations
 * via gRPC on a configurable address:port. Provides current weather
 * conditions, history, alert rules, and a streaming alert channel.
 */
class WeatherServer {
public:
    /**
     * @brief Construct a new WeatherServer
     * @param server_address Address to bind (e.g. "0.0.0.0:50055")
     * @param weather_config_path Path to JSON configuration for weather sensors/sources
     * @param enable_ssl Enable TLS encryption
     * @param ssl_cert_path Path to PEM certificate file
     * @param ssl_key_path Path to PEM private key file
     */
    WeatherServer(const std::string& server_address,
                  const std::string& weather_config_path,
                  bool enable_ssl = false,
                  const std::string& ssl_cert_path = "",
                  const std::string& ssl_key_path = "");
    ~WeatherServer();

    /// Start the gRPC server (blocks until Stop is called)
    bool Start();

    /// Stop the gRPC server gracefully
    void Stop();

    /// Wait for the server to shut down
    void Wait();

    // Non-copyable
    WeatherServer(const WeatherServer&) = delete;
    WeatherServer& operator=(const WeatherServer&) = delete;

    // Non-movable
    WeatherServer(WeatherServer&&) = delete;
    WeatherServer& operator=(WeatherServer&&) = delete;

private:
    std::string server_address_;
    std::string weather_config_path_;
    bool enable_ssl_;
    std::string ssl_cert_path_;
    std::string ssl_key_path_;
    std::unique_ptr<WeatherServiceImpl> service_;
    std::unique_ptr<grpc::Server> server_;
};

} // namespace astro_weather

#endif // WEATHER_SERVER_H
