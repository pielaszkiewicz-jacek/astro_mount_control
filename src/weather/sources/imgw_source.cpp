#include "weather/sources/imgw_source.h"
#include "http_client.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace astro_mount {
namespace weather {

namespace {
// Haversine distance [km] between two lat/lon points.
double haversineKm(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6371.0;
    const double d2r = M_PI / 180.0;
    double dlat = (lat2 - lat1) * d2r;
    double dlon = (lon2 - lon1) * d2r;
    double a = std::sin(dlat / 2.0) * std::sin(dlat / 2.0) +
               std::cos(lat1 * d2r) * std::cos(lat2 * d2r) *
               std::sin(dlon / 2.0) * std::sin(dlon / 2.0);
    return 2.0 * R * std::asin(std::min(1.0, std::sqrt(a)));
}

// Safe parse of a (possibly string-typed) JSON value into double.
double jsonToDouble(const nlohmann::json& v, double fallback = 0.0) {
    if (v.is_number()) return v.get<double>();
    if (v.is_string()) {
        try { return std::stod(v.get<std::string>()); }
        catch (...) { return fallback; }
    }
    return fallback;
}
} // anonymous namespace

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
    // P8: find the station nearest to the configured coordinates.
    Station best;
    double best_dist = std::numeric_limits<double>::max();
    for (const auto& s : getStations()) {
        double d = haversineKm(config_.latitude, config_.longitude,
                               s.latitude, s.longitude);
        if (d < best_dist) { best_dist = d; best = s; }
    }
    return best;
}

std::vector<ImgwSource::Station> ImgwSource::getStations() const {
    if (cached_stations_.empty()) {
        // Built-in station table (IMGW synop stations). The full list is
        // fetched from the API; this table covers the major stations and is
        // used for nearest-station selection by coordinates.
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
    return httpGet(synop_url_, response);
}

bool ImgwSource::parseSynopticResponse(const std::string& json, WeatherData& data) {
    // P8: parse the JSON array and pick the station nearest to the configured
    // coordinates (fields are strings in the IMGW API).
    try {
        auto stations = nlohmann::json::parse(json);
        if (!stations.is_array()) return false;

        Station nearest = findNearestStation();
        nlohmann::json best;
        bool found = false;
        for (const auto& st : stations) {
            std::string id = st.value("id_stacji", "");
            std::string name = st.value("stacja", "");
            if ((!nearest.id.empty() && id == nearest.id) ||
                (!nearest.name.empty() && name == nearest.name)) {
                best = st; found = true; break;
            }
        }
        // Fall back to the first entry if no name/id match.
        if (!found && !stations.empty()) { best = stations[0]; found = true; }
        if (!found) return false;

        data.temperature_c = jsonToDouble(best.value("temperatura", nlohmann::json()), 0.0);
        data.humidity_percent = jsonToDouble(best.value("wilgotnosc_wzgledna", nlohmann::json()), 0.0);
        data.pressure_hpa = jsonToDouble(best.value("cisnienie", nlohmann::json()), 1013.25);
        // IMGW wind speed is in km/h → convert to m/s.
        data.wind_speed_ms = jsonToDouble(best.value("predkosc_wiatru", nlohmann::json()), 0.0) / 3.6;
        data.wind_direction_deg = jsonToDouble(best.value("kierunek_wiatru", nlohmann::json()), 0.0);
        data.rain_total_mm = jsonToDouble(best.value("suma_opadu", nlohmann::json()), 0.0);
        data.rain_detected = data.rain_total_mm > 0.0;
        data.cloud_cover_percent = jsonToDouble(best.value("zachmurzenie", nlohmann::json()), 0.0);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool ImgwSource::findStationData(const std::string& json, Station& station, std::string& station_json) {
    try {
        auto stations = nlohmann::json::parse(json);
        if (!stations.is_array()) return false;
        for (const auto& st : stations) {
            if (st.value("id_stacji", "") == station.id ||
                st.value("stacja", "") == station.name) {
                station_json = st.dump();
                return true;
            }
        }
    } catch (...) {}
    return false;
}

bool ImgwSource::httpGet(const std::string& url, std::string& response) {
    // P8: real HTTP GET via libcurl.
    return astro_mount::http::get(url, response, config_.timeout_seconds);
}

} // namespace weather
} // namespace astro_mount
