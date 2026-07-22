#include "weather/sources/openweathermap_source.h"
#include <sstream>
#include <algorithm>
#include <cmath>

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
    // TODO: Implement JSON parsing using nlohmann/json
    // The OpenWeatherMap One Call API 3.0 response looks like:
    // {
    //   "current": {
    //     "dt": 1680000000,
    //     "temp": 15.5,
    //     "feels_like": 14.2,
    //     "pressure": 1013,
    //     "humidity": 72,
    //     "dew_point": 10.3,
    //     "uvi": 0.5,
    //     "clouds": 40,
    //     "visibility": 10000,
    //     "wind_speed": 3.6,
    //     "wind_deg": 250,
    //     "wind_gust": 5.2,
    //     "rain": { "1h": 0.5 }
    //   }
    // }

    // Placeholder — actual parsing requires nlohmann/json
    // auto json_data = nlohmann::json::parse(json);
    // auto& current = json_data["current"];
    // data.temperature_c = current["temp"].get<double>();
    // data.humidity_percent = current["humidity"].get<double>();
    // data.pressure_hpa = current["pressure"].get<double>();
    // data.wind_speed_ms = current["wind_speed"].get<double>();
    // data.wind_gust_ms = current.value("wind_gust", 0.0);
    // data.wind_direction_deg = current["wind_deg"].get<double>();
    // data.cloud_cover_percent = current["clouds"].get<double>();
    // data.dew_point_c = current["dew_point"].get<double>();
    // if (current.contains("rain") && current["rain"].contains("1h")) {
    //     data.rain_rate_mmh = current["rain"]["1h"].get<double>();
    //     data.rain_detected = data.rain_rate_mmh > 0.0;
    // }
    // data.timestamp = std::chrono::system_clock::from_time_t(current["dt"].get<time_t>());

    return true;  // Stub — requires nlohmann/json integration
}

bool OpenWeatherMapSource::httpGet(const std::string& url, std::string& response) {
    // TODO: Implement HTTP GET via libcurl
    // CURL* curl = curl_easy_init();
    // curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    // curl_easy_setopt(curl, CURLOPT_TIMEOUT, config_.timeout_seconds);
    // curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    // curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    // CURLcode res = curl_easy_perform(curl);
    // curl_easy_cleanup(curl);
    // return res == CURLE_OK;
    return true;  // Stub — requires libcurl integration
}

} // namespace weather
} // namespace astro_mount
