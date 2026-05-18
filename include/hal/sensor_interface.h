#pragma once
#include <functional>
#include <memory>
#include <string>
#include <chrono>
#include <vector>
#include <array>

namespace astro_mount {
namespace hal {

// Forward declaration
struct SensorConfig;

// Typy sensorów
enum class SensorType {
    TEMPERATURE,        // Czujnik temperatury
    HUMIDITY,           // Czujnik wilgotności
    PRESSURE,           // Czujnik ciśnienia
    CURRENT,            // Czujnik prądu
    VOLTAGE,            // Czujnik napięcia
    VIBRATION,          // Czujnik wibracji
    POSITION,           // Czujnik pozycji
    VELOCITY,           // Czujnik prędkości
    ACCELERATION,       // Czujnik przyspieszenia
    PROXIMITY,          // Czujnik zbliżeniowy
    LIMIT_SWITCH,       // Czujnik krańcowy
    ENCODER,            // Enkoder
    CUSTOM              // Sensor niestandardowy
};

// Typy interfejsów sensorów
enum class SensorInterfaceType {
    ANALOG,             // Sygnał analogowy (0-10V, 4-20mA)
    DIGITAL,            // Sygnał cyfrowy (GPIO, TTL)
    I2C,                // Interfejs I2C
    SPI,                // Interfejs SPI
    CANOPEN,            // Interfejs CANopen
    MODBUS,             // Interfejs Modbus
    ETHERNET,           // Interfejs Ethernet
    SERIAL              // Interfejs szeregowy
};

// Odczyt z sensora
struct SensorReading {
    int sensor_id{0};
    SensorType type{SensorType::TEMPERATURE};
    double value{0.0};
    std::string unit{"unknown"};
    std::chrono::system_clock::time_point timestamp;
    bool valid{true};
    double accuracy{0.0};       // Dokładność pomiaru
    double confidence{1.0};     // Poziom pewności (0-1)
    
    // Dodatkowe informacje
    double min_range{0.0};
    double max_range{100.0};
    double resolution{0.1};
    uint32_t sample_rate{1};    // Hz
};

// Callback types
using ReadingCallback = std::function<void(const SensorReading& reading)>;
using ErrorCallback = std::function<void(int sensor_id, const std::string& error_message)>;

// Sensor interface
class SensorInterface {
public:
    virtual ~SensorInterface() = default;
    
    // Inicjalizacja i zarządzanie
    virtual bool initialize(const SensorConfig& config) = 0;
    virtual void shutdown() = 0;
    virtual bool isInitialized() const = 0;
    
    // Odczyt danych
    virtual SensorReading read(int sensor_id) const = 0;
    virtual std::vector<SensorReading> readAll() const = 0;
    
    // Kalibracja
    virtual bool calibrate(int sensor_id, double reference_value) = 0;
    virtual bool autoCalibrate(int sensor_id) = 0;
    
    // Callbacks
    virtual void setReadingCallback(ReadingCallback callback) = 0;
    virtual void setErrorCallback(ErrorCallback callback) = 0;
    
    // Diagnostyka
    virtual std::string getDiagnostics() const = 0;
};

// Konfiguracja sensora
struct SensorConfig {
    struct SensorDefinition {
        int id{0};
        std::string name{"Unnamed_Sensor"};
        SensorType type{SensorType::TEMPERATURE};
        SensorInterfaceType interface{SensorInterfaceType::ANALOG};
        
        // Zakresy i parametry
        double min_range{0.0};
        double max_range{100.0};
        double resolution{0.1};
        uint32_t update_rate_hz{1};     // Częstotliwość odczytu
        double accuracy{0.5};           // Dokładność w jednostkach
        double noise_threshold{0.1};    // Próg szumu
        
        // Adres/identyfikator
        std::string address{"0"};
        uint8_t bus_id{0};
        uint8_t device_id{0};
        
        // Kalibracja
        struct Calibration {
            double offset{0.0};
            double gain{1.0};
            std::array<double, 3> coefficients{1.0, 0.0, 0.0}; // Wielomian kalibracyjny
            std::chrono::system_clock::time_point calibration_date;
            std::string calibration_author{"unknown"};
        } calibration;
    };
    
    std::vector<SensorDefinition> sensors;
    
    // Parametry systemowe
    uint32_t system_update_rate_hz{10};      // Całkowita częstotliwość odczytu
    bool enable_auto_calibration{false};
    uint32_t auto_calibration_interval_min{60}; // Interwał auto-kalibracji w minutach
    
    // Konfiguracja interfejsów
    struct {
        std::string i2c_bus{"/dev/i2c-1"};
        uint32_t i2c_speed{400000};          // 400kHz
        std::string spi_bus{"/dev/spidev0.0"};
        uint32_t spi_speed{1000000};         // 1MHz
        std::string canopen_interface{"can0"};
        uint32_t canopen_bitrate{125000};
        std::string serial_port{"/dev/ttyUSB0"};
        uint32_t serial_baud{115200};
    } interface_config;
    
    // Logging
    bool enable_logging{true};
    std::string log_directory{"/var/log/astro_mount/sensors"};
    uint32_t log_rotation_days{7};
    bool log_raw_data{false};
    bool log_calibrated_data{true};
};

} // namespace hal
} // namespace astro_mount