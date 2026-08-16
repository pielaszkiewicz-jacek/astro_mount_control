#include "weather/sources/openweathermap_source.h"
#include "http_client.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <ctime>

namespace astro_mount {
namespace weather {

OpenWeatherMapSource::OpenWeatherMapSource(WeatherApiConfig config)
    : config_(std::move(config)) {
}

OpenWeatherMapSource::~OpenWeatherMapSource() {
    shutdown();
}

bool OpenWeatherMapSource::initialize() {
    if (config_.api_key.empty()) {
        return false;
    }
    initialized_ = true;
    return true;
}

bool OpenWeatherMapSource::fetchCurrent(WeatherData& data) {
    if (!initialized_) return false;

    std::string url = buildUrl();
    std::string response;

    if (!httpGet(url, response)) {
        return false;
    }

    return parseResponse(response, data);
}

bool OpenWeatherMapSource::isOperational() const {
    return initialized_;
}

WeatherApiConfig OpenWeatherMapSource::getConfig() const {
    return config_;
}

void OpenWeatherMapSource::setConfig(const WeatherApiConfig& config) {
    config_ = config;
}

void OpenWeatherMapSource::shutdown() {
    initialized_ = false;
}

std::string OpenWeatherMapSource::buildUrl() const {
    std::ostringstream url;
    url << base_url_
        << "?lat=" << config_.latitude
        << "&lon=" << config_.longitude
        << "&appid=" << config_.api_key
        << "&units=" << config_.units
        << "&lang=" << config_.language
        << "&exclude=minutely,daily,alerts";  // Only current + hourly
    return url.str();
}

bool OpenWeatherMapSource::parseResponse(const std::string& json, WeatherData& data) {
    // P8: real JSON parsing (nlohmann/json) — OpenWeatherMap One Call API 3.0.
    try {
        auto j = nlohmann::json::parse(json);
        if (!j.contains("current")) return false;
        const auto& current = j["current"];

        data.temperature_c = current.value("temp", 0.0);
        data.wind_chill_c = current.value("feels_like", data.temperature_c);
        data.pressure_hpa = current.value("pressure", 1013.25);
        data.humidity_percent = current.value("humidity", 0.0);
        data.dew_point_c = current.value("dew_point", 0.0);
        data.wind_speed_ms = current.value("wind_speed", 0.0);
        data.wind_gust_ms = current.value("wind_gust", 0.0);
        data.wind_direction_deg = current.value("wind_deg", 0.0);
        data.cloud_cover_percent = current.value("clouds", 0.0);

        if (current.contains("rain") && current["rain"].is_object()) {
            data.rain_rate_mmh = current["rain"].value("1h", 0.0);
            data.rain_detected = data.rain_rate_mmh > 0.0;
        }

        if (current.contains("dt") && current["dt"].is_number()) {
            data.timestamp = std::chrono::system_clock::from_time_t(
                current["dt"].get<std::time_t>());
        }

        // UV index / visibility stored in metadata-like fields (not part of
        // the normalized struct) — kept for completeness in a comment.
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool OpenWeatherMapSource::httpGet(const std::string& url, std::string& response) {
    // P8: real HTTP GET via libcurl.
    return astro_mount::http::get(url, response, config_.timeout_seconds);
}

} // namespace weather
} // namespace astro_mount
