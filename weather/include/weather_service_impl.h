#ifndef WEATHER_SERVICE_IMPL_H
#define WEATHER_SERVICE_IMPL_H

#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <string>
#include <grpcpp/grpcpp.h>
#include <nlohmann/json.hpp>
#include "proto/weather.grpc.pb.h"

namespace astro_mount {
namespace weather {
class WeatherMonitor;
class WeatherRulesConfig;
} // namespace weather
} // namespace astro_mount

namespace astro_weather {

/**
 * @brief Implementation of the WeatherService gRPC interface
 *
 * Owns and manages a WeatherMonitor instance, translating gRPC
 * requests into sensor reads and safety evaluations.
 */
class WeatherServiceImpl final : public astro_mount::WeatherService::Service {
public:
    explicit WeatherServiceImpl(const std::string& config_path);
    ~WeatherServiceImpl() override;

    grpc::Status GetWeatherStatus(grpc::ServerContext* context,
                                  const google::protobuf::Empty* request,
                                  astro_mount::WeatherStatus* response) override;

    grpc::Status GetWeatherHistory(grpc::ServerContext* context,
                                   const astro_mount::WeatherHistoryRequest* request,
                                   astro_mount::WeatherHistoryResponse* response) override;

    grpc::Status SetWeatherRules(grpc::ServerContext* context,
                                 const astro_mount::WeatherRules* request,
                                 google::protobuf::Empty* response) override;

    grpc::Status SubscribeWeatherAlerts(grpc::ServerContext* context,
                                        const google::protobuf::Empty* request,
                                        grpc::ServerWriter<astro_mount::WeatherAlert>* writer) override;

private:
    void populateWeatherStatus(astro_mount::WeatherStatus* status) const;
    bool configureFromJson(const std::string& config_path);

    std::unique_ptr<astro_mount::weather::WeatherMonitor> monitor_;
    std::string config_path_;
    bool initialized_{false};

    mutable std::mutex monitor_mutex_;
};

} // namespace astro_weather

#endif // WEATHER_SERVICE_IMPL_H
