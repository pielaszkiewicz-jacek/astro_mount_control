#ifndef WEATHERGOV_SOURCE_H
#define WEATHERGOV_SOURCE_H

#include "weather/sources/weather_api_source.h"
#include <string>

namespace astro_mount {
namespace weather {

/**
 * @brief Weather.gov API integration (USA)
 *
 * Fetches current weather data from the National Weather Service API.
 * Completely free, no API key required (rate limited to ~10 calls/sec).
 *
 * API: https://api.weather.gov
 *
 * Uses two-step process:
 * 1. GET https://api.weather.gov/points/{lat},{lon} → gets forecast office + grid
 * 2. GET https://api.weather.gov/gridpoints/{office}/{gridX},{gridY}/forecast/hourly
 *
 * Maps Weather.gov response to WeatherData:
 * - temperature → temperature_c
 * - relativeHumidity.value → humidity_percent
 * - windSpeed → wind_speed_ms (converted from mph/kmh)
 * - windDirection → wind_direction_deg
 * - windGust → wind_gust_ms
 * - skyCover → cloud_cover_percent (from gridpoint data)
 * - probabilityOfPrecipitation → rain_rate estimate
 * - dewpoint → dew_point_c (from gridpoint data)
 *
 * Note: Weather.gov does NOT provide:
 * - Rain rate (only probability of precipitation)
 * - Pressure (requires station data)
 * - UV index
 */
class WeatherGovSource : public WeatherApiSource {
public:
    explicit WeatherGovSource(WeatherApiConfig config = {});
    ~WeatherGovSource() override;

    std::string name() const override { return "weathergov"; }
    bool initialize() override;
    bool fetchCurrent(WeatherData& data) override;
    bool isOperational() const override;
    WeatherApiConfig getConfig() const override;
    void setConfig(const WeatherApiConfig& config) override;
    void shutdown() override;

private:
    /// @brief Step 1: Get grid points for location
    bool getGridPoints(const std::string& url, std::string& office, int& gridX, int& gridY);

    /// @brief Step 2: Fetch hourly forecast
    bool fetchHourlyForecast(const std::string& office, int gridX, int gridY, std::string& response);

    /// @brief Step 3: Fetch gridpoint data (for sky cover, dew point)
    bool fetchGridpointData(const std::string& office, int gridX, int gridY, std::string& response);

    /// @brief Parse JSON response into WeatherData
    bool parseHourlyResponse(const std::string& json, WeatherData& data);
    bool parseGridpointData(const std::string& json, WeatherData& data);

    /// @brief Perform HTTP GET request
    bool httpGet(const std::string& url, std::string& response, bool json_api = true);

    WeatherApiConfig config_;
    bool initialized_{false};
    std::string base_url_{"https://api.weather.gov"};
};

} // namespace weather
} // namespace astro_mount

#endif // WEATHERGOV_SOURCE_H
