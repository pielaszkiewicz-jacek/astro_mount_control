#ifndef IMGW_SOURCE_H
#define IMGW_SOURCE_H

#include "weather/sources/weather_api_source.h"
#include <string>
#include <vector>

namespace astro_mount {
namespace weather {

/**
 * @brief IMGW (Poland) API integration
 *
 * Fetches weather data from IMGW public API (Instytut Meteorologii
 * i Gospodarki Wodnej - dane publiczne).
 *
 * API endpoints (free, no key required):
 * - Synoptic stations: https://danepubliczne.imgw.pl/api/data/synop/
 * - Hydro stations:    https://danepubliczne.imgw.pl/api/data/hydro/
 * - Meteorological:    https://api.imgw.pl/v1/meteo/ (requires registration)
 *
 * Maps IMGW synoptic data to WeatherData:
 * - Temperatura → temperature_c
 * - Wilgotność_względna → humidity_percent
 * - Ciśnienie → pressure_hpa
 * - Predkosc_wiatru → wind_speed_ms
 * - Kierunek_wiatru → wind_direction_deg
 * - Suma_opadu → rain_total_mm
 * - Zachmurzenie → cloud_cover_percent
 *
 * Note: IMGW data is updated every hour. Stations report at :50 past each hour.
 * The synoptic data covers ~60 Polish stations. Use 'id_stacji' or 'stacja' field.
 */
class ImgwSource : public WeatherApiSource {
public:
    struct Station {
        std::string id;
        std::string name;
        double latitude{0.0};
        double longitude{0.0};
    };

    explicit ImgwSource(WeatherApiConfig config = {});
    ~ImgwSource() override;

    std::string name() const override { return "imgw"; }
    bool initialize() override;
    bool fetchCurrent(WeatherData& data) override;
    bool isOperational() const override;
    WeatherApiConfig getConfig() const override;
    void setConfig(const WeatherApiConfig& config) override;
    void shutdown() override;

    /**
     * @brief Find the nearest IMGW station to configured coordinates
     * @return Nearest station info
     */
    Station findNearestStation() const;

    /**
     * @brief Get list of all available IMGW stations
     */
    std::vector<Station> getStations() const;

private:
    /// @brief Fetch synoptic data from IMGW API
    bool fetchSynopticData(std::string& response);

    /// @brief Parse JSON array of station data
    bool parseSynopticResponse(const std::string& json, WeatherData& data);

    /// @brief Find station data for the configured location
    bool findStationData(const std::string& json, Station& station, std::string& station_json);

    /// @brief Perform HTTP GET request
    bool httpGet(const std::string& url, std::string& response);

    WeatherApiConfig config_;
    bool initialized_{false};
    std::string synop_url_{"https://danepubliczne.imgw.pl/api/data/synop"};
    std::string stations_url_{"https://danepubliczne.imgw.pl/api/data/synop/station"};

    // Cached station list
    mutable std::vector<Station> cached_stations_;
};

} // namespace weather
} // namespace astro_mount

#endif // IMGW_SOURCE_H
