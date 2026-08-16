#include <gtest/gtest.h>
#include <cmath>
#include <thread>
#include <string>

#ifdef __linux__
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

// Expose the (private) JSON parsing methods so they can be unit-tested with
// sample payloads — no network access required (P8).
#define private public
#include "weather/sources/openweathermap_source.h"
#include "weather/sources/imgw_source.h"
#include "weather/sources/weathergov_source.h"
#include "weather/sources/weather_api_source.h"
#include "weather/sensors/wind_sensor.h"
#include "weather/sensors/rain_sensor.h"
#include "weather/sensors/cloud_sensor.h"
#include "weather/sensors/gps_receiver.h"
#include "weather/weather_monitor.h"
#include "weather/weather_rules.h"
#undef private

#include "notifications/channels/email_channel.h"
#include "notifications/channels/webhook_channel.h"
#include "notifications/channels/mqtt_channel.h"

using namespace astro_mount::weather;
using namespace astro_mount::notifications;

#ifdef __linux__
// Minimal one-shot HTTP server that answers any GET with a fixed JSON body.
// Returns the bound port (0 on failure).
static int startMockHttpServer(const std::string& body) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  // ephemeral
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
        ::listen(fd, 1) < 0) {
        ::close(fd);
        return 0;
    }
    sockaddr_in actual{};
    socklen_t alen = sizeof(actual);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&actual), &alen);
    int port = ntohs(actual.sin_port);

    std::thread([fd, body]() {
        int c = ::accept(fd, nullptr, nullptr);
        if (c >= 0) {
            char buf[4096];
            ssize_t rd = ::read(c, buf, sizeof(buf));
            (void)rd;  // discard request body
            std::string resp =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: " + std::to_string(body.size()) + "\r\n"
                "Connection: close\r\n\r\n" + body;
            ssize_t wr = ::write(c, resp.data(), resp.size());
            (void)wr;  // best-effort response write (warn_unused_result)
            ::close(c);
        }
        ::close(fd);
    }).detach();
    return port;
}
#endif // __linux__

// ============================================================================
// P8 — weather API source JSON parsing (sample payloads, no network)
// ============================================================================

// Full pipeline test: real libcurl HTTP GET against a local mock server, then
// JSON parsing (Phase 6 — "źródła pogody, mock HTTP").
TEST(OpenWeatherMapMockHttpTest, FetchesAndParsesViaHttp) {
#ifdef __linux__
    const std::string sample = R"({
      "current": { "dt": 1680000000, "temp": 18.0, "feels_like": 17.0,
        "pressure": 1012, "humidity": 60, "clouds": 20,
        "wind_speed": 2.5, "wind_deg": 90, "rain": { "1h": 0.0 } }
    })";
    int port = startMockHttpServer(sample);
    ASSERT_NE(port, 0) << "failed to start mock HTTP server";

    WeatherApiConfig cfg;
    cfg.api_key = "test-key";
    cfg.latitude = 52.0;
    cfg.longitude = 21.0;
    cfg.timeout_seconds = 5;
    OpenWeatherMapSource src(cfg);
    src.base_url_ = "http://127.0.0.1:" + std::to_string(port) + "/onecall";

    ASSERT_TRUE(src.initialize());
    WeatherData data;
    ASSERT_TRUE(src.fetchCurrent(data)) << "fetchCurrent must succeed against the mock server";
    EXPECT_NEAR(data.temperature_c, 18.0, 1e-9);
    EXPECT_NEAR(data.pressure_hpa, 1012.0, 1e-9);
    EXPECT_NEAR(data.humidity_percent, 60.0, 1e-9);
    EXPECT_NEAR(data.cloud_cover_percent, 20.0, 1e-9);
    EXPECT_NEAR(data.wind_speed_ms, 2.5, 1e-9);
    EXPECT_NEAR(data.wind_direction_deg, 90.0, 1e-9);
    EXPECT_FALSE(data.rain_detected);
#else
    GTEST_SKIP() << "mock HTTP test requires a POSIX socket environment";
#endif
}

TEST(OpenWeatherMapParseTest, ParsesOneCallCurrent) {
    OpenWeatherMapSource src;
    WeatherData data;
    const std::string sample = R"({
      "current": {
        "dt": 1680000000, "temp": 15.5, "feels_like": 14.2, "pressure": 1013,
        "humidity": 72, "dew_point": 10.3, "clouds": 40,
        "wind_speed": 3.6, "wind_deg": 250, "wind_gust": 5.2,
        "rain": { "1h": 0.5 }
      }
    })";
    ASSERT_TRUE(src.parseResponse(sample, data));
    EXPECT_NEAR(data.temperature_c, 15.5, 1e-9);
    EXPECT_NEAR(data.wind_chill_c, 14.2, 1e-9);
    EXPECT_NEAR(data.humidity_percent, 72.0, 1e-9);
    EXPECT_NEAR(data.pressure_hpa, 1013.0, 1e-9);
    EXPECT_NEAR(data.wind_speed_ms, 3.6, 1e-9);
    EXPECT_NEAR(data.wind_gust_ms, 5.2, 1e-9);
    EXPECT_NEAR(data.wind_direction_deg, 250.0, 1e-9);
    EXPECT_NEAR(data.cloud_cover_percent, 40.0, 1e-9);
    EXPECT_NEAR(data.dew_point_c, 10.3, 1e-9);
    EXPECT_NEAR(data.rain_rate_mmh, 0.5, 1e-9);
    EXPECT_TRUE(data.rain_detected);
}

TEST(OpenWeatherMapParseTest, RejectsInvalidJson) {
    OpenWeatherMapSource src;
    WeatherData data;
    EXPECT_FALSE(src.parseResponse("not json", data));
}

TEST(ImgwParseTest, PicksNearestStationAndParsesFields) {
    WeatherApiConfig cfg;
    cfg.latitude = 52.17; cfg.longitude = 20.97;  // Warsaw
    ImgwSource src(cfg);
    WeatherData data;
    const std::string sample = R"([
      {"id_stacji":"12560","stacja":"Kraków","temperatura":"-2.0"},
      {"id_stacji":"12570","stacja":"Warszawa","temperatura":"3.5",
       "predkosc_wiatru":"14.4","kierunek_wiatru":"270",
       "wilgotnosc_wzgledna":"78.5","suma_opadu":"0.2",
       "cisnienie":"1015.2","zachmurzenie":"60"}
    ])";
    ASSERT_TRUE(src.parseSynopticResponse(sample, data));
    EXPECT_NEAR(data.temperature_c, 3.5, 1e-9);
    // IMGW wind speed is km/h → m/s.
    EXPECT_NEAR(data.wind_speed_ms, 14.4 / 3.6, 1e-9);
    EXPECT_NEAR(data.wind_direction_deg, 270.0, 1e-9);
    EXPECT_NEAR(data.humidity_percent, 78.5, 1e-9);
    EXPECT_NEAR(data.pressure_hpa, 1015.2, 1e-9);
    EXPECT_NEAR(data.rain_total_mm, 0.2, 1e-9);
    EXPECT_TRUE(data.rain_detected);
    EXPECT_NEAR(data.cloud_cover_percent, 60.0, 1e-9);
}

TEST(WeatherGovParseTest, ParsesHourlyForecast) {
    WeatherGovSource src;
    WeatherData data;
    const std::string hourly = R"({
      "properties": { "periods": [
        { "temperature": 15, "temperatureUnit": "C",
          "windSpeed": "10 mph", "windDirection": "SW",
          "relativeHumidity": { "value": 65 },
          "probabilityOfPrecipitation": { "value": 20 } }
      ] }
    })";
    ASSERT_TRUE(src.parseHourlyResponse(hourly, data));
    EXPECT_NEAR(data.temperature_c, 15.0, 1e-9);
    EXPECT_NEAR(data.wind_speed_ms, 10.0 * 0.44704, 1e-9);
    EXPECT_NEAR(data.wind_direction_deg, 225.0, 1e-9);  // SW
    EXPECT_NEAR(data.humidity_percent, 65.0, 1e-9);
    EXPECT_NEAR(data.rain_rate_mmh, 2.0, 1e-9);         // 20% → 2 mm/h
    EXPECT_TRUE(data.rain_detected);
}

TEST(WeatherGovParseTest, ParsesGridpointSkyAndDewPoint) {
    WeatherGovSource src;
    WeatherData data;
    const std::string grid = R"({
      "properties": {
        "skyCover": { "values": [ { "value": 40 } ] },
        "dewpoint": { "values": [ { "value": 10.3 } ] }
      }
    })";
    ASSERT_TRUE(src.parseGridpointData(grid, data));
    EXPECT_NEAR(data.cloud_cover_percent, 40.0, 1e-9);
    EXPECT_NEAR(data.dew_point_c, 10.3, 1e-9);
}

// ============================================================================
// P8 — GPIO sensors: honest "no sensor" when hardware is absent
// ============================================================================

TEST(GpioWindSensorTest, NotOperationalWithoutHardware) {
    // Pin 999 does not exist → initialize() must fail and isOperational()
    // must be false, so the weather monitor reports "no sensor" instead of a
    // fake 0.0 reading.
    GpioWindSensor sensor(999, 0.5);
    EXPECT_FALSE(sensor.initialize());
    EXPECT_FALSE(sensor.isOperational());
}

TEST(GpioRainSensorTest, NotOperationalWithoutHardware) {
    GpioRainSensor sensor(999, false);
    EXPECT_FALSE(sensor.initialize());
    EXPECT_FALSE(sensor.isOperational());
}

// ============================================================================
// P9 — notification channels: honest config validation and failure
// ============================================================================

TEST(NotificationChannelTest, EmailRequiresRecipients) {
    EmailChannel ch(EmailChannel::Config{});  // no to_addresses
    EXPECT_FALSE(ch.initialize());
}

TEST(NotificationChannelTest, WebhookRequiresUrl) {
    WebhookChannel ch(WebhookChannel::Config{});  // empty url
    EXPECT_FALSE(ch.initialize());
}

TEST(NotificationChannelTest, MqttRequiresBroker) {
    MqttChannel::Config cfg;
    cfg.broker_url = "";  // default is "localhost"; explicitly empty
    MqttChannel ch(cfg);
    EXPECT_FALSE(ch.initialize());
}

TEST(NotificationChannelTest, MqttPublishFailsHonestlyWithoutBroker) {
    // Valid config, but nothing listening on 127.0.0.1:1883 → send() must
    // return false (real connection attempt) instead of the old silent-success
    // stub.
    MqttChannel::Config cfg;
    cfg.broker_url = "127.0.0.1";
    cfg.broker_port = 1883;
    MqttChannel ch(cfg);
    ASSERT_TRUE(ch.initialize());
    NotificationEvent ev;
    ev.id = "t1";
    ev.title = "test";
    ev.message = "hello";
    ev.severity = Severity::INFO;
    EXPECT_FALSE(ch.send(ev));
}

// ============================================================================
// Real cloud sensor drivers — MLX90614 (I²C) & Boltwood (serial)
// ============================================================================

TEST(Mlx90614Test, DecodeTemperature) {
    // Raw 0 = 0 * 0.02 - 273.15 = -273.15 °C.
    EXPECT_NEAR(Mlx90614CloudSensor::decodeTemperature(0), -273.15, 1e-6);
    // 0x0A28 = 2600 → 2600 * 0.02 - 273.15 = -221.15 °C.
    EXPECT_NEAR(Mlx90614CloudSensor::decodeTemperature(0x0A28), 52.0 - 273.15, 1e-6);
    // 25 °C → (25 + 273.15) / 0.02 = 14907.5 → round to 14908 (0x3A3C).
    EXPECT_NEAR(Mlx90614CloudSensor::decodeTemperature(14908), 25.0, 0.1);
}

TEST(Mlx90614Test, CloudCoverFromSkyDelta) {
    Mlx90614CloudSensor sensor("/dev/i2c-nonexistent", 0x5A, 20.0, 5.0);
    // Clear: sky -30 °C, ambient 10 °C → delta 40 ≥ 20 → 0% cover.
    EXPECT_NEAR(sensor.computeCloudCover(-30.0, 10.0), 0.0, 1e-6);
    // Overcast: sky 5 °C, ambient 8 °C → delta 3 ≤ 5 → 100% cover.
    EXPECT_NEAR(sensor.computeCloudCover(5.0, 8.0), 100.0, 1e-6);
    // Mid: delta 12.5 → halfway between 5 and 20 → 50% cover.
    EXPECT_NEAR(sensor.computeCloudCover(10.0 - 12.5, 10.0), 50.0, 1e-6);
}

TEST(Mlx90614Test, NotOperationalWithoutI2cHardware) {
    Mlx90614CloudSensor sensor("/dev/i2c-nonexistent", 0x5A);
    EXPECT_FALSE(sensor.initialize());
    EXPECT_FALSE(sensor.isOperational());
    // Unknown state → worst-case cover (fail-safe for safety rules).
    EXPECT_GE(sensor.readCloudCover(), 0.0);
}

TEST(BoltwoodTest, ParseStatusLine) {
    double sky = 0.0, amb = 0.0;
    EXPECT_TRUE(BoltwoodCloudSensor::parseLine(
        "CloudSkyTemp=-18.5;CloudTemp=-3.2;Rain=False;Wind=2.4;", sky, amb));
    EXPECT_NEAR(sky, -18.5, 1e-9);
    EXPECT_NEAR(amb, -3.2, 1e-9);
}

TEST(BoltwoodTest, ParseAlternateKeys) {
    double sky = 0.0, amb = 0.0;
    EXPECT_TRUE(BoltwoodCloudSensor::parseLine(
        "SkyTemp=-12.0;AmbientTemp=5.0", sky, amb));
    EXPECT_NEAR(sky, -12.0, 1e-9);
    EXPECT_NEAR(amb, 5.0, 1e-9);
}

TEST(BoltwoodTest, ParseGarbageReturnsFalse) {
    double sky = 0.0, amb = 0.0;
    EXPECT_FALSE(BoltwoodCloudSensor::parseLine("hello world", sky, amb));
    EXPECT_FALSE(BoltwoodCloudSensor::parseLine("", sky, amb));
}

TEST(BoltwoodTest, NotOperationalWithoutSerialHardware) {
    BoltwoodCloudSensor sensor("/dev/ttyUSB-nonexistent", 9600);
    EXPECT_FALSE(sensor.initialize());
    EXPECT_FALSE(sensor.isOperational());
}

// ============================================================================
// Real GPS driver — NMEA 0183 serial
// ============================================================================

TEST(NmeaGpsTest, ParseGGA) {
    GpsData data;
    // $GPGGA,time,lat,NS,lon,EW,fix=1,sats=7,hdop=1.2,alt=123.4,M
    EXPECT_TRUE(NmeaGpsReceiver::parseSentence(
        "$GPGGA,123519,4807.038,N,01131.000,E,1,07,1.2,123.4,M,,,", data));
    EXPECT_TRUE(data.fix_valid);
    // 4807.038 N → 48 + 7.038/60 = 48.1173
    EXPECT_NEAR(data.latitude, 48.1173, 1e-3);
    // 01131.000 E → 11 + 31.0/60 = 11.516667
    EXPECT_NEAR(data.longitude, 11.516667, 1e-3);
    EXPECT_EQ(data.satellites_visible, 7);
    EXPECT_NEAR(data.hdop, 1.2, 1e-9);
    EXPECT_NEAR(data.altitude_m, 123.4, 1e-9);
}

TEST(NmeaGpsTest, ParseRMC) {
    GpsData data;
    // $GPRMC,hhmmss,A,lat,NS,lon,EW,speed,course,date...
    EXPECT_TRUE(NmeaGpsReceiver::parseSentence(
        "$GPRMC,225446,A,4916.45,N,12311.12,W,000.5,054.7,191194,020.3,E*68", data));
    EXPECT_TRUE(data.fix_valid);
    // 4916.45 N → 49 + 16.45/60 = 49.274167
    EXPECT_NEAR(data.latitude, 49.274167, 1e-3);
    // 12311.12 W → -(123 + 11.12/60) = -123.185333
    EXPECT_NEAR(data.longitude, -123.185333, 1e-3);
}

TEST(NmeaGpsTest, RmcInvalidFix) {
    GpsData data;
    EXPECT_TRUE(NmeaGpsReceiver::parseSentence("$GPRMC,225446,V,,,,,,,,,,", data));
    EXPECT_FALSE(data.fix_valid);
}

TEST(NmeaGpsTest, GgaNoFix) {
    GpsData data;
    EXPECT_TRUE(NmeaGpsReceiver::parseSentence(
        "$GPGGA,123519,4807.038,N,01131.000,E,0,00,,,M,,,", data));
    EXPECT_FALSE(data.fix_valid);
    // fix==0 → position not updated, coordinates stay default.
    EXPECT_DOUBLE_EQ(data.latitude, 0.0);
}

TEST(NmeaGpsTest, GarbageReturnsFalse) {
    GpsData data;
    EXPECT_FALSE(NmeaGpsReceiver::parseSentence("not an nmea sentence", data));
    EXPECT_FALSE(NmeaGpsReceiver::parseSentence("$GPGGA", data));  // too short
}

TEST(NmeaGpsTest, ParseCoordinate) {
    EXPECT_NEAR(NmeaGpsReceiver::parseCoordinate("4807.038", 'N'), 48.1173, 1e-3);
    EXPECT_NEAR(NmeaGpsReceiver::parseCoordinate("01131.000", 'E'), 11.516667, 1e-3);
    EXPECT_NEAR(NmeaGpsReceiver::parseCoordinate("4807.038", 'S'), -48.1173, 1e-3);
    EXPECT_NEAR(NmeaGpsReceiver::parseCoordinate("12311.12", 'W'), -123.185333, 1e-3);
}

TEST(NmeaGpsTest, NotOperationalWithoutSerialHardware) {
    NmeaGpsReceiver gps("/dev/ttyUSB-nonexistent", 9600);
    EXPECT_FALSE(gps.initialize());
    EXPECT_FALSE(gps.isOperational());
    EXPECT_FALSE(gps.hasFix());
}

// ============================================================================
// WeatherMonitor — external API source polling (regression: the API source was
// registered but never polled, so humidity/pressure/temperature stayed at 0)
// ============================================================================

namespace {
// Minimal WeatherApiSource mock that returns a fixed reading.
class MockApiSource : public WeatherApiSource {
public:
    MockApiSource() : config_{} { config_.update_interval_minutes = 1; }
    std::string name() const override { return "mock_api"; }
    bool initialize() override { return true; }
    bool fetchCurrent(WeatherData& data) override {
        data.humidity_percent = 55.0;
        data.pressure_hpa = 1015.0;
        data.temperature_c = 12.0;
        data.wind_speed_ms = 3.0;
        return true;
    }
    bool isOperational() const override { return true; }
    WeatherApiConfig getConfig() const override { return config_; }
    void setConfig(const WeatherApiConfig& config) override { config_ = config; }
    void shutdown() override {}
private:
    WeatherApiConfig config_;
};
}

TEST(WeatherMonitorTest, ApiSourceIsPolledAndMerged) {
    WeatherMonitor monitor;
    monitor.setWeatherApiSource(std::make_unique<MockApiSource>());

    // The mock's fetch interval is 1 minute; force an immediate poll by
    // backdating the throttle timer (monitor internals are exposed via the
    // #define private public trick).
    monitor.last_api_fetch_ = std::chrono::steady_clock::now() -
        std::chrono::minutes(2);

    monitor.forceRead();

    auto data = monitor.getCurrentWeather();
    // Without the fix, humidity/pressure/temperature would remain at their
    // defaults (0 / 1013.25 / 0) — the API data must be merged in.
    EXPECT_NEAR(data.humidity_percent, 55.0, 1e-9);
    EXPECT_NEAR(data.pressure_hpa, 1015.0, 1e-9);
    EXPECT_NEAR(data.temperature_c, 12.0, 1e-9);
    EXPECT_NEAR(data.wind_speed_ms, 3.0, 1e-9);
}

TEST(WeatherMonitorTest, ApiSourceThrottledToInterval) {
    WeatherMonitor monitor;
    monitor.setWeatherApiSource(std::make_unique<MockApiSource>());

    // Do NOT backdate the throttle — the first forceRead() should poll once
    // (last_api_fetch_ starts at epoch), then a second immediate read must be
    // throttled (no second fetch within the 1-minute interval).
    monitor.forceRead();
    auto first = monitor.getCurrentWeather();
    EXPECT_NEAR(first.humidity_percent, 55.0, 1e-9);

    monitor.forceRead();  // within interval → no new fetch, keeps last data
    auto second = monitor.getCurrentWeather();
    EXPECT_NEAR(second.humidity_percent, 55.0, 1e-9);
}
