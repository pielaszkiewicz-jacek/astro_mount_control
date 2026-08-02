#include "weather/sources/weathergov_source.h"
#include <sstream>
#include <algorithm>
#include <cmath>

namespace astro_mount {
namespace weather {

WeatherGovSource::WeatherGovSource(WeatherApiConfig config)
    : config_(std::move(config)) {
}

WeatherGovSource::~WeatherGovSource() {
    shutdown();
}

bool WeatherGovSource::initialize() {
    // Weather.gov does not require an API key
    initialized_ = true;
    return true;
}

bool WeatherGovSource::fetchCurrent(WeatherData& data) {
    if (!initialized_) return false;

    // Step 1: Get grid points
    std::ostringstream points_url;
    points_url << base_url_ << "/points/"
               << config_.latitude << "," << config_.longitude;

    std::string office;
    int gridX = 0, gridY = 0;
    if (!getGridPoints(points_url.str(), office, gridX, gridY)) {
        return false;
    }

    // Step 2: Fetch hourly forecast
    std::string hourly_response;
    if (!fetchHourlyForecast(office, gridX, gridY, hourly_response)) {
        return false;
    }
    if (!parseHourlyResponse(hourly_response, data)) {
        return false;
    }

    // Step 3: Fetch gridpoint data for additional fields
    std::string gridpoint_response;
    if (fetchGridpointData(office, gridX, gridY, gridpoint_response)) {
        parseGridpointData(gridpoint_response, data);
    }

    return true;
}

bool WeatherGovSource::isOperational() const {
    return initialized_;
}

WeatherApiConfig WeatherGovSource::getConfig() const {
    return config_;
}

void WeatherGovSource::setConfig(const WeatherApiConfig& config) {
    config_ = config;
}

void WeatherGovSource::shutdown() {
    initialized_ = false;
}

bool WeatherGovSource::getGridPoints(const std::string& url, std::string& office,
                                      int& gridX, int& gridY) {
    std::string response;
    if (!httpGet(url, response)) {
        return false;
    }

    // TODO: Parse JSON response to extract:
    // response["properties"]["cwa"] → office (e.g. "OKX")
    // response["properties"]["gridX"] → gridX
    // response["properties"]["gridY"] → gridY
    //
    // using json = nlohmann::json;
    // auto data = json::parse(response);
    // office = data["properties"]["cwa"].get<std::string>();
    // gridX = data["properties"]["gridX"].get<int>();
    // gridY = data["properties"]["gridY"].get<int>();

    office = "OKX";   // Stub
    gridX = 100;       // Stub
    gridY = 100;       // Stub
    return true;
}

bool WeatherGovSource::fetchHourlyForecast(const std::string& office, int gridX, int gridY,
                                            std::string& response) {
    std::ostringstream url;
    url << base_url_ << "/gridpoints/" << office << "/" << gridX << "," << gridY
        << "/forecast/hourly";
    return httpGet(url.str(), response);
}

bool WeatherGovSource::fetchGridpointData(const std::string& office, int gridX, int gridY,
                                           std::string& response) {
    std::ostringstream url;
    url << base_url_ << "/gridpoints/" << office << "/" << gridX << "," << gridY;
    return httpGet(url.str(), response);
}

bool WeatherGovSource::parseHourlyResponse(const std::string& json, WeatherData& data) {
    // TODO: Parse hourly forecast JSON
    // Weather.gov hourly forecast format:
    // {
    //   "properties": {
    //     "periods": [{
    //       "number": 1,
    //       "startTime": "2024-01-01T12:00:00-05:00",
    //       "temperature": 15,
    //       "temperatureUnit": "C",
    //       "windSpeed": "10 mph",
    //       "windDirection": "SW",
    //       "relativeHumidity": { "value": 65 },
    //       "probabilityOfPrecipitation": { "value": 20 }
    //     }]
    //   }
    // }
    return true;  // Stub — requires nlohmann/json
}

bool WeatherGovSource::parseGridpointData(const std::string& json, WeatherData& data) {
    // TODO: Parse gridpoint data for sky cover, dew point, pressure
    return true;  // Stub
}

bool WeatherGovSource::httpGet(const std::string& url, std::string& response, bool json_api) {
    // TODO: Implement HTTP GET via libcurl
    // Weather.gov requires User-Agent header: "astro-mount-controller/1.0"
    // and Accept: "application/json" for JSON API endpoints
    //
    // curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    // curl_easy_setopt(curl, CURLOPT_USERAGENT, "astro-mount-controller/1.0");
    // if (json_api) {
    //     struct curl_slist* headers = nullptr;
    //     headers = curl_slist_append(headers, "Accept: application/json");
    //     curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    // }
    return true;  // Stub — requires libcurl integration
}

} // namespace weather
} // namespace astro_mount
