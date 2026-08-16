#include "weather/sources/weathergov_source.h"
#include "http_client.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cctype>

namespace astro_mount {
namespace weather {

namespace {
// Map a cardinal wind direction ("SW") to degrees.
double cardinalToDeg(const std::string& dir) {
    if (dir.empty()) return 0.0;
    std::string d = dir;
    std::transform(d.begin(), d.end(), d.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    struct { const char* name; double deg; } map[] = {
        {"N",0},{"NNE",22.5},{"NE",45},{"ENE",67.5},{"E",90},{"ESE",112.5},
        {"SE",135},{"SSE",157.5},{"S",180},{"SSW",202.5},{"SW",225},{"WSW",247.5},
        {"W",270},{"WNW",292.5},{"NW",315},{"NNW",337.5}
    };
    for (const auto& m : map) { if (d == m.name) return m.deg; }
    try { return std::stod(dir); } catch (...) { return 0.0; }
}

// Extract the first numeric value from a weather.gov "values" time-series.
double firstSeriesValue(const nlohmann::json& obj, const std::string& key) {
    try {
        if (obj.contains(key) && obj[key].contains("values") &&
            obj[key]["values"].is_array() && !obj[key]["values"].empty()) {
            return obj[key]["values"][0].value("value", 0.0);
        }
    } catch (...) {}
    return 0.0;
}
} // anonymous namespace

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
    // P8: parse JSON response to extract properties.cwa / gridX / gridY.
    std::string response;
    if (!httpGet(url, response)) return false;
    try {
        auto j = nlohmann::json::parse(response);
        auto& props = j["properties"];
        office = props.value("cwa", "");
        gridX = props.value("gridX", 0);
        gridY = props.value("gridY", 0);
        return !office.empty() && gridX > 0 && gridY > 0;
    } catch (const std::exception&) {
        return false;
    }
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
    // P8: parse the hourly forecast JSON.
    try {
        auto j = nlohmann::json::parse(json);
        auto& props = j["properties"];
        if (!props.contains("periods") || props["periods"].empty()) return false;
        const auto& p = props["periods"][0];

        double temp = p.value("temperature", 0.0);
        std::string unit = p.value("temperatureUnit", "C");
        data.temperature_c = (unit == "F") ? (temp - 32.0) * 5.0 / 9.0 : temp;

        if (p.contains("relativeHumidity") && p["relativeHumidity"].contains("value")) {
            auto hv = p["relativeHumidity"]["value"];
            if (hv.is_number()) data.humidity_percent = hv.get<double>();
        }

        // windSpeed: "10 mph" / "15 km/h" → m/s.
        std::string wind_speed = p.value("windSpeed", "0");
        std::istringstream ws(wind_speed);
        double w; std::string unit2;
        ws >> w >> unit2;
        if (unit2.find("mph") != std::string::npos) data.wind_speed_ms = w * 0.44704;
        else if (unit2.find("km") != std::string::npos) data.wind_speed_ms = w / 3.6;
        else data.wind_speed_ms = w * 0.44704;

        data.wind_direction_deg = cardinalToDeg(p.value("windDirection", ""));

        if (p.contains("probabilityOfPrecipitation") && p["probabilityOfPrecipitation"].contains("value")) {
            auto pop = p["probabilityOfPrecipitation"]["value"];
            if (pop.is_number()) {
                double precip_pct = pop.get<double>();
                // Estimate rain rate from probability (0–100% → 0–10 mm/h).
                data.rain_rate_mmh = precip_pct * 0.1;
                data.rain_detected = precip_pct > 0.0;
            }
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool WeatherGovSource::parseGridpointData(const std::string& json, WeatherData& data) {
    // P8: parse gridpoint data for sky cover and dew point.
    try {
        auto j = nlohmann::json::parse(json);
        auto& props = j["properties"];
        data.cloud_cover_percent = firstSeriesValue(props, "skyCover");
        data.dew_point_c = firstSeriesValue(props, "dewpoint");
        double temp = firstSeriesValue(props, "temperature");
        if (temp != 0.0) data.temperature_c = temp;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool WeatherGovSource::httpGet(const std::string& url, std::string& response, bool json_api) {
    // P8: real HTTP GET via libcurl; Weather.gov requires a User-Agent.
    std::vector<std::string> headers;
    if (json_api) headers.emplace_back("Accept: application/json");
    return astro_mount::http::get(url, response, config_.timeout_seconds,
                                  "astro-mount-controller/1.0", headers);
}

} // namespace weather
} // namespace astro_mount
