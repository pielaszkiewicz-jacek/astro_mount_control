#pragma once
#include <chrono>
#include <string>
#include <functional>

namespace astro_mount {
namespace hal {

enum class EncoderType {
    INCREMENTAL,    // Enkoder inkrementalny
    ABSOLUTE,       // Enkoder absolutny
    RESOLVER,       // Rezolver
    HALL_SENSOR,    // Czujniki Halla
    VIRTUAL         // Wirtualny (do testów)
};

enum class EncoderInterface {
    QUADRATURE,     // Interfejs kwadraturowy (A, B, Z)
    SSI,            // Synchronous Serial Interface
    BISS,           // Bidirectional Synchronous Serial
    ENDAT,          // EnDat 2.2
    CANOPEN,        // CANopen
    ANALOG          // Analogowy (0-10V, 4-20mA)
};

struct EncoderConfig {
    EncoderType type{EncoderType::ABSOLUTE};
    EncoderInterface interface{EncoderInterface::SSI};
    uint32_t resolution{16384};          // Counts per revolution
    double counts_per_degree{10000.0};   // Counts per degree
    bool use_index_pulse{true};          // Czy używać impulsu indeksu
    bool use_direction_signal{true};     // Czy używać sygnału kierunku
    double max_velocity{1000.0};         // Maksymalna prędkość (counts/s)
    uint32_t update_rate_hz{100};        // Częstotliwość odświeżania PDO (Hz)
    bool enable_error_detection{true};   // Wykrywanie błędów
    uint32_t error_threshold{10};        // Próg błędów
    double calibration_offset{0.0};      // Offset kalibracji w stopniach
    
    // Metody pomocnicze
    double countsToDegrees(int32_t counts) const {
        return static_cast<double>(counts) / counts_per_degree;
    }
    
    int32_t degreesToCounts(double degrees) const {
        return static_cast<int32_t>(degrees * counts_per_degree);
    }
    
    double velocityCountsToDegrees(int32_t counts_per_sec) const {
        return static_cast<double>(counts_per_sec) / counts_per_degree;
    }
};

struct EncoderReading {
    double position_deg{0.0};           // Pozycja w stopniach
    double velocity_deg_s{0.0};         // Prędkość w stopniach/s
    int32_t raw_counts{0};              // Surowa wartość licznika
    bool index_pulse{false};            // Impuls indeksu
    bool direction{true};               // Kierunek (true = forward)
    bool data_valid{true};              // Czy dane są poprawne
    uint32_t error_count{0};            // Licznik błędów
    std::chrono::steady_clock::time_point timestamp;
    
    // Metody pomocnicze
    bool isStable() const {
        return data_valid && error_count == 0;
    }
    
    double getTimestampSeconds() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now - timestamp).count();
    }
};

class EncoderReader {
public:
    virtual ~EncoderReader() = default;
    
    // Inicjalizacja
    virtual bool initialize(const EncoderConfig& config) = 0;
    virtual void shutdown() = 0;
    virtual bool isInitialized() const = 0;
    
    // Odczyt
    virtual EncoderReading read() const = 0;
    virtual bool isDataValid() const = 0;
    virtual double getUpdateRate() const = 0; // Hz
    
    // Kalibracja
    virtual bool calibrate(double reference_position_deg) = 0;
    virtual bool autoCalibrate() = 0;
    virtual double getCalibrationOffset() const = 0;
    virtual void setCalibrationOffset(double offset_deg) = 0;
    virtual bool saveCalibration() = 0;
    virtual bool loadCalibration() = 0;
    
    // Informacje
    virtual EncoderType getType() const = 0;
    virtual EncoderInterface getInterface() const = 0;
    virtual uint32_t getResolution() const = 0;
    virtual double getCountsPerDegree() const = 0;
    
    // Callbacki
    using ReadingCallback = std::function<void(const EncoderReading& reading)>;
    using ErrorCallback = std::function<void(const std::string& error, uint32_t error_code)>;
    
    virtual void setReadingCallback(ReadingCallback callback) = 0;
    virtual void setErrorCallback(ErrorCallback callback) = 0;
    
    // Diagnostyka
    virtual uint32_t getTotalReadings() const = 0;
    virtual uint32_t getErrorCount() const = 0;
    virtual double getUptime() const = 0; // w sekundach
    virtual std::string getDiagnostics() const = 0;
    
    // Synchronizacja
    virtual bool synchronize() = 0;
    virtual bool isSynchronized() const = 0;
};

} // namespace hal
} // namespace astro_mount