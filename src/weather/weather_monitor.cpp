#include "weather/weather_monitor.h"
#include "weather/sensors/rain_sensor.h"
#include "weather/sensors/wind_sensor.h"
#include "weather/sensors/cloud_sensor.h"
#include "weather/sensors/gps_receiver.h"
#include "weather/sources/weather_api_source.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace astro_mount {
namespace weather {

WeatherMonitor::WeatherMonitor() {
    history_.reserve(history_max_size_);
}

WeatherMonitor::~WeatherMonitor() {
    stop();
}

void WeatherMonitor::setRainSensor(std::unique_ptr<RainSensor> sensor) {
    std::lock_guard<std::mutex> lock(mutex_);
    rain_sensor_ = std::move(sensor);
}

void WeatherMonitor::setWindSensor(std::unique_ptr<WindSensor> sensor) {
    std::lock_guard<std::mutex> lock(mutex_);
    wind_sensor_ = std::move(sensor);
}

void WeatherMonitor::setCloudSensor(std::unique_ptr<CloudSensor> sensor) {
    std::lock_guard<std::mutex> lock(mutex_);
    cloud_sensor_ = std::move(sensor);
}

void WeatherMonitor::setGpsReceiver(std::unique_ptr<GpsReceiver> gps) {
    std::lock_guard<std::mutex> lock(mutex_);
    gps_receiver_ = std::move(gps);
}

void WeatherMonitor::setWeatherApiSource(std::unique_ptr<WeatherApiSource> api_source) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (api_source) {
        api_source->initialize();
        // Capture the configured poll interval (minutes) so the monitor polls
        // the API on its own schedule instead of every monitoring cycle.
        auto cfg = api_source->getConfig();
        api_fetch_interval_ = std::chrono::minutes(
            cfg.update_interval_minutes > 0 ? cfg.update_interval_minutes : 10);
    }
    api_source_ = std::move(api_source);
}

bool WeatherMonitor::start(int interval_seconds) {
    if (running_) return true;

    poll_interval_ = interval_seconds;
    running_ = true;
    monitor_thread_ = std::make_unique<std::thread>(&WeatherMonitor::monitoringLoop, this);
    return true;
}

void WeatherMonitor::stop() {
    if (!running_) return;
    running_ = false;

    if (monitor_thread_ && monitor_thread_->joinable()) {
        monitor_thread_->join();
    }
    monitor_thread_.reset();
}

WeatherData WeatherMonitor::getCurrentWeather() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_;
}

astro_mount::WeatherStatus WeatherMonitor::getStatusProto() const {
    std::lock_guard<std::mutex> lock(mutex_);

    astro_mount::WeatherStatus status;
    status.set_temperature_c(current_.temperature_c);
    status.set_temperature_trend(current_.temperature_trend);
    status.set_dew_point_c(current_.dew_point_c);
    status.set_wind_chill_c(current_.wind_chill_c);
    status.set_humidity_percent(current_.humidity_percent);
    status.set_pressure_hpa(current_.pressure_hpa);
    status.set_pressure_trend(current_.pressure_trend);
    status.set_wind_speed_ms(current_.wind_speed_ms);
    status.set_wind_gust_ms(current_.wind_gust_ms);
    status.set_wind_direction_deg(current_.wind_direction_deg);
    status.set_rain_detected(current_.rain_detected);
    status.set_rain_rate_mmh(current_.rain_rate_mmh);
    status.set_rain_total_mm(current_.rain_total_mm);
    status.set_cloud_cover_percent(current_.cloud_cover_percent);
    status.set_sky_brightness_mpsas(current_.sky_brightness_mpsas);
    status.set_ambient_light_lux(current_.ambient_light_lux);
    status.set_current_condition(current_.condition);
    status.set_alert_level(current_.alert_level);
    status.set_safe_to_observe(current_.safe_to_observe);

    auto proto_time = status.mutable_timestamp();
    auto duration = current_.timestamp.time_since_epoch();
    proto_time->set_seconds(std::chrono::duration_cast<std::chrono::seconds>(duration).count());
    proto_time->set_nanos(0);

    return status;
}

void WeatherMonitor::setRules(const WeatherRulesConfig& rules) {
    std::lock_guard<std::mutex> lock(mutex_);
    rules_ = rules;
}

WeatherRulesConfig WeatherMonitor::getRules() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rules_;
}

std::vector<WeatherData> WeatherMonitor::getHistory(
    std::chrono::system_clock::time_point since, int max_points) const {
    std::lock_guard<std::mutex> lock(history_mutex_);

    std::vector<WeatherData> result;
    for (const auto& reading : history_) {
        if (reading.timestamp >= since) {
            result.push_back(reading);
            if (static_cast<int>(result.size()) >= max_points) break;
        }
    }
    return result;
}

void WeatherMonitor::setAlertCallback(std::function<void(const WeatherAlertEvent&)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    alert_callback_ = std::move(callback);
}

void WeatherMonitor::forceRead() {
    readAllSensors();
    calculateDerived();
    evaluateRules();
}

astro_mount::GpsPosition WeatherMonitor::getGpsPosition() const {
    std::lock_guard<std::mutex> lock(mutex_);

    astro_mount::GpsPosition pos;
    if (gps_receiver_) {
        auto gps_data = gps_receiver_->readPosition();
        pos.set_latitude(gps_data.latitude);
        pos.set_longitude(gps_data.longitude);
        pos.set_altitude_m(gps_data.altitude_m);
        pos.set_satellites_visible(gps_data.satellites_visible);
        pos.set_hdop(gps_data.hdop);
        pos.set_fix_valid(gps_data.fix_valid);
    }
    return pos;
}

// ─── Private Methods ─────────────────────────────────────────────────────────

void WeatherMonitor::monitoringLoop() {
    while (running_) {
        readAllSensors();
        calculateDerived();
        auto severity = evaluateRules();

        // Update current data
        {
            std::lock_guard<std::mutex> lock(mutex_);
            current_.timestamp = std::chrono::system_clock::now();
            current_.alert_level = severity;
            current_.condition = current_.rain_detected
                ? WeatherCondition::CONDITION_RAIN
                : (current_.cloud_cover_percent > 70.0
                    ? WeatherCondition::CONDITION_CLOUDY
                    : WeatherCondition::CONDITION_CLEAR);
            current_.safe_to_observe = (severity <= WeatherAlertSeverity::WEATHER_CAUTION);

            addToHistory(current_);
        }

        // Sleep for polling interval
        std::this_thread::sleep_for(std::chrono::seconds(poll_interval_));
    }
}

void WeatherMonitor::readAllSensors() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Poll the external API source first (throttled to its update interval).
    // It fills fields that have no physical sensor (humidity, pressure,
    // temperature, wind, cloud, rain); physical sensors below then override.
    if (api_source_ && api_source_->isOperational()) {
        auto now = std::chrono::steady_clock::now();
        if (now - last_api_fetch_ >= api_fetch_interval_) {
            last_api_fetch_ = now;
            WeatherData api;
            if (api_source_->fetchCurrent(api)) {
                // Apply API fields that physical sensors do not cover.
                if (api.humidity_percent > 0.0)   current_.humidity_percent = api.humidity_percent;
                if (api.pressure_hpa > 0.0)       current_.pressure_hpa = api.pressure_hpa;
                if (std::isfinite(api.temperature_c) && api.temperature_c != 0.0)
                    current_.temperature_c = api.temperature_c;
                if (api.wind_speed_ms > 0.0)      current_.wind_speed_ms = api.wind_speed_ms;
                if (api.wind_gust_ms > 0.0)       current_.wind_gust_ms = api.wind_gust_ms;
                if (api.wind_direction_deg > 0.0) current_.wind_direction_deg = api.wind_direction_deg;
                if (api.cloud_cover_percent > 0.0) current_.cloud_cover_percent = api.cloud_cover_percent;
                if (api.rain_detected) {
                    current_.rain_detected = true;
                    if (api.rain_rate_mmh > 0.0) current_.rain_rate_mmh = api.rain_rate_mmh;
                }
            }
        }
    }

    // Read rain sensor
    if (rain_sensor_ && rain_sensor_->isOperational()) {
        current_.rain_detected = rain_sensor_->readRainDetected();
        current_.rain_rate_mmh = rain_sensor_->readRainRate();
        current_.rain_total_mm = rain_sensor_->readTotalRainfall();
    }

    // Read wind sensor
    if (wind_sensor_ && wind_sensor_->isOperational()) {
        current_.wind_speed_ms = wind_sensor_->readWindSpeed();
        current_.wind_gust_ms = wind_sensor_->readWindGust();
        current_.wind_direction_deg = wind_sensor_->readWindDirection();
    }

    // Read cloud/sky sensor
    if (cloud_sensor_ && cloud_sensor_->isOperational()) {
        current_.cloud_cover_percent = cloud_sensor_->readCloudCover();
        current_.sky_brightness_mpsas = cloud_sensor_->readSkyBrightness();
        current_.ambient_light_lux = cloud_sensor_->readAmbientLight();
        current_.temperature_c = cloud_sensor_->readAmbientTemperature();
    }
}

void WeatherMonitor::calculateDerived() {
    // Calculate dew point (Magnus formula)
    double a = 17.27, b = 237.7;
    double alpha = (a * current_.temperature_c) / (b + current_.temperature_c) +
                   std::log(current_.humidity_percent / 100.0);
    current_.dew_point_c = (b * alpha) / (a - alpha);

    // Calculate wind chill (if wind > 0.5 m/s and temp < 10°C)
    if (current_.wind_speed_ms > 0.5 && current_.temperature_c < 10.0) {
        double v = current_.wind_speed_ms * 3.6; // convert to km/h
        current_.wind_chill_c = 13.12 + 0.6215 * current_.temperature_c -
                                11.37 * std::pow(v, 0.16) +
                                0.3965 * current_.temperature_c * std::pow(v, 0.16);
    } else {
        current_.wind_chill_c = current_.temperature_c;
    }
}

WeatherAlertSeverity WeatherMonitor::evaluateRules() {
    auto severity = WeatherRules::evaluate(
        current_.temperature_c, current_.humidity_percent,
        current_.wind_speed_ms, current_.wind_gust_ms,
        current_.rain_detected, current_.cloud_cover_percent,
        current_.dew_point_c, rules_);

    // Check if auto-park should be triggered
    if (rules_.auto_park_enabled && WeatherRules::shouldAutoPark(severity, rules_)) {
        WeatherAlertEvent alert;
        alert.timestamp = std::chrono::system_clock::now();
        alert.severity = severity;
        alert.auto_action_taken = true;
        alert.suggested_action = "park_mount";

        if (alert_callback_) {
            alert_callback_(alert);
        }
    }

    return severity;
}

void WeatherMonitor::addToHistory(const WeatherData& data) {
    std::lock_guard<std::mutex> lock(history_mutex_);
    history_.push_back(data);
    if (history_.size() > history_max_size_) {
        history_.erase(history_.begin());
    }
}

} // namespace weather
} // namespace astro_mount
