#include "weather/include/weather_service_impl.h"
#include "weather/weather_monitor.h"
#include "weather/weather_rules.h"
#include "weather/sources/weather_api_source.h"
#include "weather/sources/openweathermap_source.h"
#include "weather/sources/weathergov_source.h"
#include "weather/sources/imgw_source.h"
#include <chrono>
#include <thread>
#include <fstream>
#include <nlohmann/json.hpp>
#include <google/protobuf/util/time_util.h>

namespace astro_weather {

using WeatherMonitor = astro_mount::weather::WeatherMonitor;
using WeatherRulesConfig = astro_mount::weather::WeatherRulesConfig;
using WeatherApiConfig = astro_mount::weather::WeatherApiConfig;
using WeatherApiSource = astro_mount::weather::WeatherApiSource;
using WeatherData = astro_mount::weather::WeatherData;

// ============================================
// Construction / Destruction
// ============================================

WeatherServiceImpl::WeatherServiceImpl(const std::string& config_path)
    : config_path_(config_path) {
    try {
        if (!configureFromJson(config_path)) {
            std::cerr << "[WeatherServiceImpl] Config load failed, running in degraded mode\n";
        }
        if (monitor_) {
            monitor_->start();
            initialized_ = true;
        }
    } catch (const std::exception& e) {
        std::cerr << "[WeatherServiceImpl] Init error: " << e.what() << "\n";
    }
}

WeatherServiceImpl::~WeatherServiceImpl() {
    // N7: subscription streams are per-client and self-terminating — no shared
    // watcher thread to join. Only the monitor lifecycle needs stopping here.
    if (monitor_) monitor_->stop();
}

// ============================================
// Configuration
// ============================================

bool WeatherServiceImpl::configureFromJson(const std::string& config_path) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        std::cerr << "[WeatherServiceImpl] Cannot open config: " << config_path << "\n";
        monitor_ = std::make_unique<WeatherMonitor>();
        return false;
    }

    nlohmann::json config;
    file >> config;

    auto monitor = std::make_unique<WeatherMonitor>();

    if (config.contains("rules")) {
        auto& r = config["rules"];
        WeatherRulesConfig rules;
        if (r.contains("min_temperature_c")) rules.min_temperature_c = r["min_temperature_c"].get<double>();
        if (r.contains("max_temperature_c")) rules.max_temperature_c = r["max_temperature_c"].get<double>();
        if (r.contains("max_wind_speed_ms")) rules.max_wind_speed_ms = r["max_wind_speed_ms"].get<double>();
        if (r.contains("max_wind_gust_ms")) rules.max_wind_gust_ms = r["max_wind_gust_ms"].get<double>();
        if (r.contains("auto_park_on_rain")) rules.auto_park_on_rain = r["auto_park_on_rain"].get<bool>();
        if (r.contains("max_rain_rate_mmh")) rules.max_rain_rate_mmh = r["max_rain_rate_mmh"].get<double>();
        if (r.contains("max_cloud_cover_percent")) rules.max_cloud_cover_percent = r["max_cloud_cover_percent"].get<double>();
        if (r.contains("max_humidity_percent")) rules.max_humidity_percent = r["max_humidity_percent"].get<double>();
        if (r.contains("dew_point_tolerance_c")) rules.dew_point_tolerance_c = r["dew_point_tolerance_c"].get<double>();
        if (r.contains("auto_park_enabled")) rules.auto_park_enabled = r["auto_park_enabled"].get<bool>();
        if (r.contains("check_interval_seconds")) rules.check_interval_seconds = r["check_interval_seconds"].get<int32_t>();
        if (r.contains("debounce_seconds")) rules.debounce_seconds = r["debounce_seconds"].get<int32_t>();
        if (r.contains("notify_on_alert")) rules.notify_on_alert = r["notify_on_alert"].get<bool>();
        if (r.contains("close_dome_on_danger")) rules.close_dome_on_danger = r["close_dome_on_danger"].get<bool>();
        monitor->setRules(rules);
    }

    if (config.contains("api_source")) {
        auto& ac = config["api_source"];
        WeatherApiConfig api_cfg;
        api_cfg.api_key = ac.value("api_key", "");
        api_cfg.latitude = ac.value("latitude", 52.0);
        api_cfg.longitude = ac.value("longitude", 21.0);
        api_cfg.location_query = ac.value("location_query", "");
        api_cfg.units = "metric";

        std::string provider = ac.value("provider", "");
        std::unique_ptr<WeatherApiSource> source;
        if (provider == "openweathermap")
            source = std::make_unique<astro_mount::weather::OpenWeatherMapSource>(api_cfg);
        else if (provider == "weathergov")
            source = std::make_unique<astro_mount::weather::WeatherGovSource>(api_cfg);
        else if (provider == "imgw")
            source = std::make_unique<astro_mount::weather::ImgwSource>(api_cfg);
        if (source) monitor->setWeatherApiSource(std::move(source));
    }

    monitor_ = std::move(monitor);
    return true;
}

// ============================================
// Helper
// ============================================

void WeatherServiceImpl::populateWeatherStatus(astro_mount::WeatherStatus* status) const {
    if (!initialized_ || !monitor_) {
        status->set_current_condition(astro_mount::CONDITION_UNKNOWN);
        status->set_alert_level(astro_mount::WEATHER_DANGER);
        status->set_safe_to_observe(false);
        status->set_safety_message("Weather monitor not initialised");
        return;
    }

    auto data = monitor_->getCurrentWeather();

    status->set_temperature_c(data.temperature_c);
    status->set_temperature_trend(data.temperature_trend);
    status->set_dew_point_c(data.dew_point_c);
    status->set_wind_chill_c(data.wind_chill_c);
    status->set_humidity_percent(data.humidity_percent);
    status->set_pressure_hpa(data.pressure_hpa);
    status->set_pressure_trend(data.pressure_trend);
    status->set_wind_speed_ms(data.wind_speed_ms);
    status->set_wind_gust_ms(data.wind_gust_ms);
    status->set_wind_direction_deg(data.wind_direction_deg);
    status->set_rain_detected(data.rain_detected);
    status->set_rain_rate_mmh(data.rain_rate_mmh);
    status->set_rain_total_mm(data.rain_total_mm);
    status->set_cloud_cover_percent(data.cloud_cover_percent);
    status->set_sky_brightness_mpsas(data.sky_brightness_mpsas);
    status->set_ambient_light_lux(data.ambient_light_lux);

    switch (data.condition) {
        case astro_mount::WeatherCondition::CONDITION_CLEAR: status->set_current_condition(astro_mount::CONDITION_CLEAR); break;
        case astro_mount::WeatherCondition::CONDITION_CLOUDY: status->set_current_condition(astro_mount::CONDITION_CLOUDY); break;
        case astro_mount::WeatherCondition::CONDITION_RAIN: status->set_current_condition(astro_mount::CONDITION_RAIN); break;
        case astro_mount::WeatherCondition::CONDITION_SNOW: status->set_current_condition(astro_mount::CONDITION_SNOW); break;
        case astro_mount::WeatherCondition::CONDITION_FOG: status->set_current_condition(astro_mount::CONDITION_FOG); break;
        case astro_mount::WeatherCondition::CONDITION_WINDY: status->set_current_condition(astro_mount::CONDITION_WINDY); break;
        case astro_mount::WeatherCondition::CONDITION_STORM: status->set_current_condition(astro_mount::CONDITION_STORM); break;
        default: status->set_current_condition(astro_mount::CONDITION_UNKNOWN); break;
    }

    switch (data.alert_level) {
        case astro_mount::WeatherAlertSeverity::WEATHER_CAUTION: status->set_alert_level(astro_mount::WEATHER_CAUTION); break;
        case astro_mount::WeatherAlertSeverity::WEATHER_WARNING: status->set_alert_level(astro_mount::WEATHER_WARNING); break;
        case astro_mount::WeatherAlertSeverity::WEATHER_DANGER: status->set_alert_level(astro_mount::WEATHER_DANGER); break;
        default: status->set_alert_level(astro_mount::WEATHER_CLEAR); break;
    }

    status->set_safe_to_observe(data.safe_to_observe);

    auto ts = google::protobuf::util::TimeUtil::SecondsToTimestamp(
        std::chrono::duration_cast<std::chrono::seconds>(
            data.timestamp.time_since_epoch()).count());
    *status->mutable_timestamp() = ts;
}

// ============================================
// gRPC methods
// ============================================

grpc::Status WeatherServiceImpl::GetWeatherStatus(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    astro_mount::WeatherStatus* response) {
    std::lock_guard<std::mutex> lock(monitor_mutex_);
    if (monitor_) monitor_->forceRead();
    populateWeatherStatus(response);
    return grpc::Status::OK;
}

grpc::Status WeatherServiceImpl::GetWeatherHistory(
    grpc::ServerContext* context,
    const astro_mount::WeatherHistoryRequest* request,
    astro_mount::WeatherHistoryResponse* response) {
    std::lock_guard<std::mutex> lock(monitor_mutex_);
    if (!initialized_ || !monitor_)
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Weather monitor not initialised");

    auto since = std::chrono::system_clock::time_point(
        std::chrono::seconds(request->start_time().seconds()));
    int max_points = request->max_points() > 0 ? request->max_points() : 100;
    auto history = monitor_->getHistory(since, max_points);

    for (const auto& h : history) {
        auto* reading = response->add_readings();
        auto ts = google::protobuf::util::TimeUtil::SecondsToTimestamp(
            std::chrono::duration_cast<std::chrono::seconds>(
                h.timestamp.time_since_epoch()).count());
        *reading->mutable_timestamp() = ts;
        reading->set_temperature_c(h.temperature_c);
        reading->set_humidity_percent(h.humidity_percent);
        reading->set_pressure_hpa(h.pressure_hpa);
        reading->set_wind_speed_ms(h.wind_speed_ms);
        reading->set_rain_rate_mmh(h.rain_rate_mmh);
        reading->set_cloud_cover_percent(h.cloud_cover_percent);
    }
    response->set_total_points(history.size());
    return grpc::Status::OK;
}

grpc::Status WeatherServiceImpl::SetWeatherRules(
    grpc::ServerContext* context,
    const astro_mount::WeatherRules* request,
    google::protobuf::Empty* response) {
    std::lock_guard<std::mutex> lock(monitor_mutex_);
    if (!initialized_ || !monitor_)
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Weather monitor not initialised");

    WeatherRulesConfig rules;
    rules.min_temperature_c = request->min_temperature_c();
    rules.max_temperature_c = request->max_temperature_c();
    rules.max_wind_speed_ms = request->max_wind_speed_ms();
    rules.max_wind_gust_ms = request->max_wind_gust_ms();
    rules.auto_park_on_rain = request->auto_park_on_rain();
    rules.max_rain_rate_mmh = request->max_rain_rate_mmh();
    rules.max_cloud_cover_percent = request->max_cloud_cover_percent();
    rules.max_humidity_percent = request->max_humidity_percent();
    rules.dew_point_tolerance_c = request->dew_point_tolerance_c();
    rules.auto_park_enabled = request->auto_park_enabled();
    rules.check_interval_seconds = request->check_interval_seconds();
    rules.debounce_seconds = request->debounce_seconds();
    rules.notify_on_alert = request->notify_on_alert();
    rules.close_dome_on_danger = request->close_dome_on_danger();
    monitor_->setRules(rules);
    return grpc::Status::OK;
}

grpc::Status WeatherServiceImpl::SubscribeWeatherAlerts(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    grpc::ServerWriter<astro_mount::WeatherAlert>* writer) {
    // N7: per-client loop — only the client's own cancellation terminates this
    // stream. The previous implementation used the shared `watching_` flag, so
    // one client disconnecting killed every subscriber's stream (the same class
    // of bug P13 fixed for the derotator).
    while (!context->IsCancelled()) {
        {
            std::lock_guard<std::mutex> lock(monitor_mutex_);
            if (!initialized_ || !monitor_) {
                return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Not initialised");
            }
            auto data = monitor_->getCurrentWeather();
            astro_mount::WeatherAlert alert;
            alert.set_id(std::to_string(
                std::chrono::duration_cast<std::chrono::seconds>(
                    data.timestamp.time_since_epoch()).count()));
            auto ts = google::protobuf::util::TimeUtil::SecondsToTimestamp(
                std::chrono::duration_cast<std::chrono::seconds>(
                    data.timestamp.time_since_epoch()).count());
            *alert.mutable_timestamp() = ts;

            switch (data.alert_level) {
                case astro_mount::WeatherAlertSeverity::WEATHER_CAUTION:
                    alert.set_severity(astro_mount::WEATHER_CAUTION);
                    alert.set_message("Caution: weather conditions deteriorating"); break;
                case astro_mount::WeatherAlertSeverity::WEATHER_WARNING:
                    alert.set_severity(astro_mount::WEATHER_WARNING);
                    alert.set_message("Warning: unsafe weather conditions detected"); break;
                case astro_mount::WeatherAlertSeverity::WEATHER_DANGER:
                    alert.set_severity(astro_mount::WEATHER_DANGER);
                    alert.set_message("DANGER: automatic safety actions triggered"); break;
                default:
                    alert.set_severity(astro_mount::WEATHER_CLEAR);
                    alert.set_message("Weather conditions are normal"); break;
            }
            if (!writer->Write(alert)) break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
    return grpc::Status::OK;
}

} // namespace astro_weather
