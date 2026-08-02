#include "weather/sources/imgw_source.h"
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace astro_mount {
namespace weather {

ImgwSource::ImgwSource(WeatherApiConfig config)
    : config_(std::move(config)) {
}

ImgwSource::~ImgwSource() {
    shutdown();
}

bool ImgwSource::initialize() {
    // IMGW public API does not require an API key
    initialized_ = true;
    return true;
}

bool ImgwSource::fetchCurrent(WeatherData& data) {
    if (!initialized_) return false;

    std::string response;
    if (!fetchSynopticData(response)) {
        return false;
    }

    return parseSynopticResponse(response, data);
}

bool ImgwSource::isOperational() const {
    return initialized_;
}

WeatherApiConfig ImgwSource::getConfig() const {
    return config_;
}

void ImgwSource::setConfig(const WeatherApiConfig& config) {
    config_ = config;
}

void ImgwSource::shutdown() {
    initialized_ = false;
}

ImgwSource::Station ImgwSource::findNearestStation() const {
    // TODO: Load station list and find nearest by Haversine distance
    // For now, return a default station
    Station s;
    s.id = "12570";
    s.name = "Warszawa";
    s.latitude = 52.1667;
    s.longitude = 20.9667;
    return s;
}

std::vector<ImgwSource::Station> ImgwSource::getStations() const {
    if (cached_stations_.empty()) {
        // TODO: Fetch and parse station list from IMGW API
        // The synop endpoint returns all stations with their data
        // Station name is in "stacja" field, ID in "id_stacji"
        cached_stations_.push_back({"12570", "Warszawa", 52.1667, 20.9667});
        cached_stations_.push_back({"12560", "Kraków", 50.0667, 19.9333});
        cached_stations_.push_back({"12424", "Wrocław", 51.1000, 16.8833});
        cached_stations_.push_back({"12375", "Poznań", 52.4167, 16.8333});
        cached_stations_.push_back({"12150", "Gdańsk", 54.3667, 18.6333});
        cached_stations_.push_back({"12500", "Łódź", 51.7333, 19.4000});
        cached_stations_.push_back({"12520", "Lublin", 51.2167, 22.4000});
        cached_stations_.push_back({"12160", "Olsztyn", 53.7667, 20.4167});
        cached_stations_.push_back({"12400", "Zielona Góra", 51.9333, 15.5333});
        cached_stations_.push_back({"12580", "Rzeszów", 50.1000, 22.0500});
    }
    return cached_stations_;
}

bool ImgwSource::fetchSynopticData(std::string& response) {
    // IMGW synoptic data API:
    // GET https://danepubliczne.imgw.pl/api/data/synop
    // Returns JSON array of all stations with current readings
    //
    // Example response:
    // [{
    //   "id_stacji": "12570",
    //   "stacja": "Warszawa",
    //   "data_pomiaru": "2024-01-15",
    //   "godzina_pomiaru": "13",
    //   "temperatura": "3.5",
    //   "predkosc_wiatru": "4",
    //   "kierunek_wiatru": "270",
    //   "wilgotnosc_wzgledna": "78.5",
    //   "suma_opadu": "0.2",
    //   "cisnienie": "1015.2",
    //   "zachmurzenie": "60"
    // }]

    return httpGet(synop_url_, response);
}

bool ImgwSource::parseSynopticResponse(const std::string& json, WeatherData& data) {
    // TODO: Parse JSON array to find the nearest station
    // using json = nlohmann::json;
    // auto stations = json::parse(json);
    // double min_dist = std::numeric_limits<double>::max();
    // json best;
    // for (const auto& st : stations) {
    //     double lat = std::stod(st["stacja"].get<std::string>()); // would need coords mapping
    //     ...
    // }

    // Placeholder parsing for known station
    // data.temperature_c = std::stod(station["temperatura"].get<std::string>());
    // data.humidity_percent = std::stod(station["wilgotnosc_wzgledna"].get<std::string>());
    // data.pressure_hpa = std::stod(station["cisnienie"].get<std::string>());
    // data.wind_speed_ms = std::stod(station["predkosc_wiatru"].get<std::string>()) / 3.6;
    // data.wind_direction_deg = std::stod(station["kierunek_wiatru"].get<std::string>());
    // data.rain_total_mm = std::stod(station["suma_opadu"].get<std::string>());
    // data.rain_detected = data.rain_total_mm > 0.0;
    // data.cloud_cover_percent = std::stod(station["zachmurzenie"].get<std::string>());

    return true;  // Stub — requires nlohmann/json integration
}

bool ImgwSource::findStationData(const std::string& json, Station& station, std::string& station_json) {
    // Find station data in JSON array matching nearest station
    return true;  // Stub
}

bool ImgwSource::httpGet(const std::string& url, std::string& response) {
    // TODO: Implement HTTP GET via libcurl
    // CURL* curl = curl_easy_init();
    // curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    // curl_easy_setopt(curl, CURLOPT_TIMEOUT, config_.timeout_seconds);
    // CURLcode res = curl_easy_perform(curl);
    // curl_easy_cleanup(curl);
    // return res == CURLE_OK;
    return true;  // Stub — requires libcurl integration
}

} // namespace weather
} // namespace astro_mount
