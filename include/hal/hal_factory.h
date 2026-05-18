#pragma once
#include "hal/hal_interface.h"
#include "hal/hal_config.h"
#include <memory>
#include <vector>
#include <string>

namespace astro_mount {
namespace hal {

class HALFactory {
public:
    HALFactory() = default;
    ~HALFactory() = default;
    
    // Tworzenie instancji HAL na podstawie konfiguracji
    static std::unique_ptr<HALInterface> create(const HALConfig& config);
    
    // Tworzenie instancji HAL na podstawie typu
    static std::unique_ptr<HALInterface> create(HALType type);
    
    // Tworzenie instancji HAL na podstawie nazwy typu
    static std::unique_ptr<HALInterface> create(const std::string& type_name);
    
    // Pobieranie dostępnych typów HAL
    static std::vector<HALType> getAvailableTypes();
    
    // Pobieranie nazw dostępnych typów HAL
    static std::vector<std::string> getAvailableTypeNames();
    
    // Sprawdzanie dostępności typu HAL
    static bool isTypeAvailable(HALType type);
    
    // Pobieranie domyślnego typu HAL
    static HALType getDefaultType();
    
    // Pobieranie domyślnej konfiguracji dla typu
    static HALConfig getDefaultConfig(HALType type);
    
    // Ładowanie konfiguracji z pliku JSON
    static HALConfig loadConfigFromFile(const std::string& filename);
    
    // Zapis konfiguracji do pliku JSON
    static bool saveConfigToFile(const HALConfig& config, const std::string& filename);
    
private:
    // Metody pomocnicze
    static bool checkCANOpenSupport();
    static bool checkSerialSupport();
    static bool checkEthernetSupport();
    
    // Tworzenie konkretnych implementacji
    static std::unique_ptr<HALInterface> createSimulatedHAL(const HALConfig& config);
    static std::unique_ptr<HALInterface> createCanOpenHAL(const HALConfig& config);
    static std::unique_ptr<HALInterface> createSerialHAL(const HALConfig& config);
    static std::unique_ptr<HALInterface> createEthernetHAL(const HALConfig& config);
    static std::unique_ptr<HALInterface> createGamepadHAL(const HALConfig& config);
};

} // namespace hal
} // namespace astro_mount