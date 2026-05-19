# Przewodnik dla dewelopera

## Spis treści
1. [Przegląd projektu](#1-przegląd-projektu)
2. [Konfiguracja środowiska deweloperskiego](#2-konfiguracja-środowiska-deweloperskiego)
3. [Przegląd kodu źródłowego](#3-przegląd-kodu-źródłowego)
4. [Architektura](#4-architektura)
5. [System konfiguracji](#5-system-konfiguracji)
6. [System budowania](#6-system-budowania)
7. [Testowanie](#7-testowanie)
8. [Standardy kodowania](#8-standardy-kodowania)
9. [Przepływ pracy przy zgłaszaniu zmian](#9-przepływ-pracy-przy-zgłaszaniu-zmian)
10. [Najczęstsze zadania](#10-najczęstsze-zadania)
11. [Rozwiązywanie problemów](#11-rozwiązywanie-problemów)
12. [Indeks plików](#12-indeks-plików)

---

## 1. Przegląd projektu

[`astro-mount-controller`](../../) to program sterujący montażem astronomicznym napisanym w **C++17**. Zapewnia kompleksowe sterowanie montażami teleskopów, w tym:

- **Sterowanie osią**: Przesuwanie (slew), śledzenie (tracking), parkowanie
- **Modelowanie korekcyjne**: Kalibracja TPOINT i bootstrap w celu osiągnięcia dokładności wskazywania poniżej 1 sekundy kątowej
- **Efemerydy**: Śledzenie obiektów ruchomych (asteroidy, komety, satelity) poprzez interpolację danych efemerydalnych
- **Korekcja guiderem**: Wsparcie dla korekcji w czasie rzeczywistym z autoguiderów (PHD2)
- **Filtracja Kalmana**: Fuzja czujników i estymacja stanu dla płynnego śledzenia
- **API gRPC**: W pełni funkcjonalne API dla klientów zdalnych
- **Warstwa HAL**: Warstwa abstrakcji sprzętowej wspierająca wiele backendów (CANopen, symulowany, szeregowy)
- **Biblioteka SOFA**: Astronomiczne transformacje współrzędnych (IAU SOFA)

### Kluczowe metryki

| Metryka | Wartość |
|---------|---------|
| Linie kodu (C++) | ~12 000 (w tym testy) |
| Pliki nagłówkowe | ~25 (`include/`) |
| Pliki implementacyjne | ~15 (`src/`) |
| Pliki testowe | 15 (`tests/`) |
| Definicje protobuf | ~1115 linii |
| Modele obliczeniowe | TPOINT (40+ parametrów), Kalman (6D), Efemerydy |
| Biblioteki | SOFA (IAU), gRPC, Protobuf, nlohmann/json |
| Standard C++ | C++17 |

---

## 2. Konfiguracja środowiska deweloperskiego

### Minimalne wymagania

- **System operacyjny**: Ubuntu 22.04+ / Debian 12+ (zalecany), inne dystrybucje Linux, WSL2
- **Kompilator**: GCC 11+ lub Clang 14+
- **CMake**: 3.20+
- **gRPC**: 1.50+ (z włączonym protobuf)
- **Git**: Do kontroli wersji

### Instalacja zależności

```bash
# Podstawowe narzędzia deweloperskie
sudo apt update
sudo apt install build-essential cmake git

# gRPC i Protobuf
sudo apt install libgrpc-dev libgrpc++-dev protobuf-compiler-grpc \
                 libprotobuf-dev libprotoc-dev

# Biblioteki pomocnicze
sudo apt install nlohmann-json3-dev libeigen3-dev

# Zainstaluj dodatkowe narzędzia CAN (opcjonalnie, do testów sprzętowych)
sudo apt install can-utils
```

### Klonowanie i budowanie

```bash
# Sklonuj repozytorium
git clone https://github.com/twoja-organizacja/astro-mount-controller.git
cd astro-mount-controller

# Skonfiguruj build debug z testami
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON

# Zbuduj projekt
cmake --build build -j$(nproc)

# Uruchom wszystkie testy
cd build && ctest --output-on-failure
```

### Zalecane narzędzia

- **Visual Studio Code** — z zalecaną konfiguracją
- **CLion** — IDE specyficzne dla CMake
- **Valgrind** — do wykrywania wycieków pamięci
- **Helgrind/DRD** — do wykrywania data races
- **GDB** — debugger

### Konfiguracja VS Code

W katalogu projektu znajduje się konfiguracja VS (`.vscode/`), która zapewnia:

```json
{
    "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
    "cmake.configureSettings": {
        "BUILD_TESTING": "ON",
        "CMAKE_BUILD_TYPE": "Debug"
    },
    "editor.formatOnSave": true,
    "files.associations": {
        "*.proto": "proto3"
    },
    "editor.rulers": [100]
}
```

**Zalecane rozszerzenia VS Code:**

| Rozszerzenie | ID | Cel |
|-------------|-----|------|
| C/C++ | ms-vscode.cpptools | IntelliSense, debugowanie |
| CMake Tools | ms-vscode.cmake-tools | Integracja CMake |
| protobuf | zxh404.vscode-proto3 | Podświetlanie składni Proto |
| clangd | llvm-vs-code.clangd | Linting (opcjonalnie) |
| Test Explorer | hbenl.vscode-test-explorer | Uruchamianie testów |

---

## 3. Przegląd kodu źródłowego

### Struktura katalogów

```
astro-mount-controller/
├── config/                 # Pliki konfiguracyjne JSON
├── docs/                   # Dokumentacja
│   ├── en/                 #   Dokumentacja w języku angielskim
│   └── pl/                 #   Dokumentacja w języku polskim
├── include/                # Pliki nagłówkowe
│   ├── config/             #   System konfiguracji
│   ├── controllers/        #   Sterowniki (MountController)
│   ├── core/               #   Obliczenia astronomiczne
│   ├── hal/                #   Interfejsy warstwy abstrakcji sprzętowej
│   └── models/             #   Modele (TPOINT, Kalman, Efemerydy)
├── proto/                  # Definicje protobuf
├── scripts/                # Skrypty pomocnicze
├── sofa/                   # Źródła biblioteki SOFA (IAU)
├── src/                    # Pliki źródłowe
│   ├── api/                #   Serwer gRPC i implementacja usług
│   ├── config/             #   Ładowanie konfiguracji
│   ├── controllers/        #   Implementacja sterowników
│   ├── core/               #   Obliczenia astronomiczne
│   ├── hal/                #   Implementacje HAL
│   │   ├── canopen_hal/    #     Implementacja CANopen
│   │   ├── ethernet_hal/   #     Implementacja Ethernet
│   │   ├── gamepad_hal/    #     Implementacja gamepada
│   │   ├── serial_hal/     #     Implementacja szeregowa
│   │   └── simulated_hal/  #     Implementacja symulowana
│   ├── logging/            #   System logowania
│   └── models/             #   Implementacje modeli
├── tests/                  # Testy jednostkowe
├── web/                    # Interfejs webowy i proxy
└── CMakeLists.txt          # Główny plik CMake
```

### Kluczowe pliki do zapoznania

| Plik | Linie | Opis |
|------|-------|------|
| [`include/controllers/mount_controller.h`](../../include/controllers/mount_controller.h) | ~200 | Główna klasa kontrolera (interfejs publiczny) |
| [`src/controllers/mount_controller.cpp`](../../src/controllers/mount_controller.cpp) | 5195 | Logika kontrolera (najważniejszy plik) |
| [`include/controllers/icanopen_interface.h`](../../include/controllers/icanopen_interface.h) | ~150 | Interfejs komunikacji CANopen |
| [`include/config/configuration.h`](../../include/config/configuration.h) | ~150 | Struktury konfiguracyjne |
| [`include/models/tpoint_model.h`](../../include/models/tpoint_model.h) | ~100 | Model korekcji TPOINT |
| [`include/models/kalman_filter.h`](../../include/models/kalman_filter.h) | ~80 | Filtr Kalmana |
| [`include/models/ephemeris_tracker.h`](../../include/models/ephemeris_tracker.h) | ~120 | Śledzenie efemeryd |
| [`proto/mount_controller.proto`](../../proto/mount_controller.proto) | 1115 | Definicja API gRPC |
| [`include/hal/hal_interface.h`](../../include/hal/hal_interface.h) | ~60 | Główny interfejs HAL |
| [`src/main.cpp`](../../src/main.cpp) | ~50 | Punkt wejścia aplikacji |

---

## 4. Architektura

### Architektura trójwarstwowa

System jest zorganizowany w trzech głównych warstwach:

```
┌──────────────────────────────────────────────┐
│            Warstwa Aplikacji                  │
│  MountController, gRPC API, Zarządzanie      │
│  konfiguracją, Logger                        │
│  (współrzędne, śledzenie, kalibracja, MAS)   │
├──────────────────────────────────────────────┤
│         Warstwa Modeli Obliczeniowych         │
│  TPOINT (40 parametrów), Kalman Filter (6D), │
│  Efemerydy (splajny kubiczne), Bootstrap     │
├──────────────────────────────────────────────┤
│     Warstwa Abstrakcji Sprzętu (HAL)          │
│  CANopen (CiA 402), Symulowany, Szeregowy,   │
│  Ethernet (planowany), Gamepad               │
└──────────────────────────────────────────────┘
```

### Odpowiedzialności komponentów

| Komponent | Odpowiedzialność |
|-----------|-----------------|
| `MountController` | Koordynacja wysokiego poziomu: stany, transformacje, kalibracja |
| `TPointModel` | Modelowanie błędów geometrycznych i korekcja (do 40+ parametrów) |
| `KalmanFilter` | Estymacja stanu, redukcja szumów, fuzja czujników |
| `EphemerisTracker` | Interpolacja efemeryd i śledzenie obiektów ruchomych |
| `ICanOpenInterface` | Abstrakcja komunikacji z napędami CANopen (CiA 402) |
| `HALInterface` | Abstrakcja sprzętu niskiego poziomu (silniki, enkodery, bezpieczeństwo) |
| `Configuration` | Ładowanie, walidacja i udostępnianie konfiguracji |
| `AstronomicalCalculations` | Transformacje współrzędnych przez SOFA |

### Maszyna stanów

```
        ┌──────────────────────────────────────────┐
        │                                          │
        ▼                                          │
    ┌───────┐     slew()     ┌──────────┐          │
    │       │ ──────────────► │          │          │
    │ IDLE  │                 │ SLEWING  │          │
    │       │ ◄────────────── │          │          │
    └───┬───┘   (target      └──────────┘          │
        │        reached)                           │
        │                                          │
        │ track()               ┌────────────┐     │
        ├──────────────────────►│            │     │
        │                       │ TRACKING   │     │
        │ ◄──────────────────── │            │     │
        │   stop()              └────────────┘     │
        │                                          │
        │ park()               ┌──────────┐        │
        ├─────────────────────►│          │        │
        │                      │ PARKED   │        │
        │ ◄─────────────────── │          │        │
        │   unpark()           └──────────┘        │
        │                                          │
        │                     ┌───────────┐        │
        └────────────────────►│           │────────┘
                              │  ERROR    │
                              │           │
                              └───────────┘
                                    ▲
                                    │
                              clearErrors()
```

### Kluczowy przepływ danych (Slew do Track)

```
SlewToEquatorial(ra=10.5h, dec=41.3°)
    │
    ▼
1. Weryfikacja stanu (ERROR → odrzuć)
2. Transformacja współrzędnych: RA/Dec → mount (przez TPOINT + bootstrap)
3. Ustaw cel osi
4. Stan → SLEWING
5. Uruchom wątek monitorowania (tło)
6. Return (natychmiastowy)

    │
    ▼
Wątek monitorowania (20 Hz):
- Sprawdź pozycję przez HAL / symulację
- Gdy cel osiągnięty → Stan → IDLE

    │
    ▼
TrackObject(ra=10.5h, dec=41.3°)
    │
    ▼
1. Stan → TRACKING
2. Uruchom wątek śledzenia (100 Hz):
   - Oblicz prędkość śledzenia (15.041067 "/s × cos(dec))
   - Aktualizuj pozycję
   - Zastosuj korekcję guidera (jeśli aktywny)
   - Wyślij do HAL
```

---

## 5. System konfiguracji

### Konfiguracja trójwarstwowa

```
┌─────────────────────────────────────────────┐
│  Pliki JSON (config/*.json)                  │
│  Przykład:                                   │
│  {                                           │
│    "location": {"latitude": 52.0, ...},      │
│    "mount": {"max_slew_rate": 5.0, ...},     │
│    "calibration": {"tpoint_enabled_terms": 12}│
│  }                                           │
├─────────────────────────────────────────────┤
│  Struktury C++ (Configuration struct)         │
│  Walidacja podczas ładowania                 │
│  Wartości domyślne dla brakujących pól       │
├─────────────────────────────────────────────┤
│  ControllerConfig (MountController::Impl)    │
│  Używane w czasie rzeczywistym               │
│  Ochrona przez shared_mutex (R/W)           │
└─────────────────────────────────────────────┘
```

### Dodawanie nowego parametru konfiguracji

```cpp
// 1. Dodaj do MountConfig w include/config/configuration.h
struct MountConfig {
    // ... istniejące pola ...
    double new_parameter{default_value};  // Twój nowy parametr
};

// 2. Dodaj ładowanie JSON w src/config/configuration.cpp
void from_json(const nlohmann::json& j, MountConfig& c) {
    // ... istniejący kod ...
    j.at("mount").value("new_parameter", c.new_parameter, 42.0);
}

// 3. Użyj w MountController::Impl
void someMethod() {
    std::shared_lock lock(config_mutex_);
    double val = config_.new_parameter;
    // ...
}
```

### Przykład walidacji

```cpp
// W src/config/configuration.cpp
bool validateMountConfig(const MountConfig& config) {
    if (config.latitude < -90.0 || config.latitude > 90.0) {
        logger_->error("Latitude out of range: {}", config.latitude);
        return false;
    }
    if (config.max_slew_rate <= 0.0 || config.max_slew_rate > 10.0) {
        logger_->error("Invalid max slew rate: {}", config.max_slew_rate);
        return false;
    }
    // ... więcej walidacji ...
    return true;
}
```

---

## 6. System budowania

### Cele budowania

| Cel CMake | Opis |
|-----------|------|
| `astro_mount_controller` | Główny plik wykonywalny |
| `test_*` | Cele testowe (jednostkowe) |
| `grpc_generate` | Generowanie kodu gRPC/protobuf |

### Typowe komendy

```bash
# Pełny build debug z testami
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j$(nproc)

# Zbuduj i uruchom konkretny test
cmake --build build --target test_mount_controller -j$(nproc)
./build/tests/test_mount_controller

# Build release do testów wydajnościowych
cmake -B build_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_release -j$(nproc)
```

### Struktura CMakeLists.txt

Główny [`CMakeLists.txt`](../../CMakeLists.txt) definiuje:
1. Wymagany standard C++ (C++17)
2. Znajdowanie pakietów (gRPC, Protobuf, nlohmann-json, Eigen3)
3. Generator kodu protobuf
4. Cele bibliotek dla komponentów
5. Główny cel wykonywalny
6. Cele testowe (jeśli BUILD_TESTING=ON)

---

## 7. Testowanie

### Architektura testów

- **Framework**: Google Test (gtest)
- **Katalog**: [`tests/`](../../tests/)
- **Pokrycie**: Testy jednostkowe dla każdego głównego komponentu

```cpp
// Przykład testu (tests/test_mount_controller.cpp)
TEST(MountControllerTest, SlewToEquatorial) {
    auto controller = createTestController();
    
    bool result = controller->SlewToEquatorial(10.5, 41.3);
    EXPECT_TRUE(result);
    
    auto state = controller->GetState();
    EXPECT_EQ(state.status(), MountStatus::SLEWING);
    
    // Symuluj zakończenie ruchu
    simulateSlewCompletion(controller);
    
    state = controller->GetState();
    EXPECT_EQ(state.status(), MountStatus::IDLE);
}
```

### Uruchamianie testów

```bash
# Uruchom wszystkie testy
cd build && ctest --output-on-failure

# Uruchom z verbose output
cd build && ctest -V

# Uruchom konkretny test
cd build && ./tests/test_mount_controller
```

### Kategorie testów

| Kategoria | Pliki | Cel |
|-----------|-------|------|
| Jednostkowe | `test_*.cpp` | Izolowane testy komponentów |
| Integracyjne | `test_hal_integration.cpp` | Współdziałanie komponentów |
| gRPC | `test_grpc_integration.cpp` | Testy API |
| Dokładność | `test_subarcsecond_accuracy.cpp` | Weryfikacja precyzji |

### Pisanie testów

```cpp
// 1. Użyj TEST() dla prostych przypadków
TEST(ConfigurationTest, LoadValidConfig) {
    Configuration config;
    EXPECT_TRUE(config.loadFromFile("config/test_config.json"));
    EXPECT_DOUBLE_EQ(config.getMountConfig().latitude, 52.0);
}

// 2. Użyj TEST_F() dla stanu współdzielonego
class MountControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        controller_ = createTestController();
    }
    std::unique_ptr<MountController> controller_;
};

TEST_F(MountControllerTest, StateTransitions) {
    EXPECT_EQ(controller_->GetState().status(), MountStatus::IDLE);
    controller_->SlewToEquatorial(10.5, 41.3);
    EXPECT_EQ(controller_->GetState().status(), MountStatus::SLEWING);
}
```

---

## 8. Standardy kodowania

### Styl C++

- **Standard**: C++17
- **Nazewnictwo**: `camelCase` dla metod, `snake_case` dla zmiennych, `PascalCase` dla klas
- **Nagłówki**: `#pragma once` zamiast include guards
- **Stałe**: `constexpr` tam, gdzie to możliwe
- **Interfejsy**: Klasy wirtualne z `= default` destruktorem
- **Wskaźniki**: `std::unique_ptr` jako domyślny, `std::shared_ptr` dla własności współdzielonej

### Organizacja plików

```cpp
// Przykład: include/controllers/mount_controller.h
#pragma once

#include <memory>
#include <string>

namespace astro_mount {
namespace controllers {

class MountController {
public:
    MountController();
    ~MountController();
    
    // Metody publiczne (API)
    bool initialize(const std::string& config_path);
    bool slewToEquatorial(double ra_hours, double dec_degrees);
    // ...
    
private:
    class Impl;                          // PIMPL
    std::unique_ptr<Impl> pimpl_;        // Wskaźnik do implementacji
};

} // namespace controllers
} // namespace astro_mount
```

### Obsługa błędów

- Użyj wartości zwracanych `bool` dla operacji, które mogą się nie powieść
- Rzucaj wyjątki tylko dla błędów krytycznych (konstruktory, inicjalizacja)
- Loguj błędy przez centralny `Logger`
- Użyj `std::optional` dla wartości, które mogą nie istnieć

### Wzorzec guardów NaN/Inf

```cpp
// Wzorzec do ochrony przed NaN/Inf w obliczeniach
double safeDivide(double numerator, double denominator) {
    if (std::abs(denominator) < 1e-15) {
        logger_->warn("Division by near-zero in safeDivide: {}", denominator);
        return 0.0;
    }
    double result = numerator / denominator;
    if (!std::isfinite(result)) {
        logger_->error("Non-finite result in safeDivide: {} / {}", numerator, denominator);
        return 0.0;
    }
    return result;
}

// Użycie w obliczeniach
double rate = safeDivide(tracking_error, cos(dec_radians));
```

### Bezpieczeństwo wątkowe

- Użyj `std::mutex` + `std::lock_guard` dla krótkich sekcji krytycznych
- Użyj `std::shared_mutex` dla częstych odczytów / rzadkich zapisów
- Użyj `std::atomic` dla prostych flag
- NIGDY nie trzymaj muteksu podczas wywoływania callbacków

---

## 9. Przepływ pracy przy zgłaszaniu zmian

### Krok 1: Zrozum kod

1. Przeczytaj odpowiednie pliki dokumentacji w [`docs/`](../../docs/)
2. Przejrzyj pliki nagłówkowe w [`include/`](../../include/)
3. Zapoznaj się z implementacją w [`src/`](../../src/)
4. Przejrzyj istniejące testy w [`tests/`](../../tests/)

### Krok 2: Sklonuj i zbuduj

```bash
# Fork i clone
git clone https://github.com/twoja-organizacja/astro-mount-controller.git
cd astro-mount-controller

# Build
cmake -B build -DBUILD_TESTING=ON
cmake --build build -j$(nproc)

# Zweryfikuj, że testy przechodzą
cd build && ctest --output-on-failure
```

### Krok 3: Wprowadź zmiany

1. Utwórz gałąź funkcji: `git checkout -b feature/twoja-funkcja`
2. Wprowadź zmiany, przestrzegając standardów kodowania
3. Dodaj/aktualizuj testy
4. Uruchom testy lokalnie
5. Zaktualizuj dokumentację

### Krok 4: Commit i PR

```bash
# Commit z opisowym komunikatem
git add .
git commit -m "feat: dodaj wsparcie dla nowego typu montażu

- Zaimplementowano obsługę montażu alt-azymutalnego
- Dodano transformacje współrzędnych dla alt-az
- Zaktualizowano dokumentację API
- Dodano testy dla nowego typu montażu

Closes #123"
```

```bash
# Push i utwórz PR
git push origin feature/twoja-funkcja
```

### Konwencja komunikatów commit

```
<type>: <krótki opis>

<szczegółowy opis (opcjonalnie)>

<referencje do issue (opcjonalnie)>
```

Typy: `feat`, `fix`, `docs`, `test`, `refactor`, `perf`, `chore`

---

## 10. Najczęstsze zadania

### Dodawanie nowego RPC gRPC

1. **Dodaj definicję protobuf** w [`proto/mount_controller.proto`](../../proto/mount_controller.proto):
   ```protobuf
   rpc MyNewRPC(MyRequest) returns (MyResponse);
   message MyRequest { string param = 1; }
   message MyResponse { bool success = 1; }
   ```
2. **Zgeneruj kod**: `cmake --build build`
3. **Zaimplementuj RPC** w [`src/api/service_impl.cpp`](../../src/api/service_impl.cpp):
   ```cpp
   grpc::Status MountControllerServiceImpl::MyNewRPC(
       grpc::ServerContext* context,
       const MyRequest* request,
       MyResponse* response) {
       // logika
       return grpc::Status::OK;
   }
   ```
4. **Dodaj test** w [`tests/test_grpc_integration.cpp`](../../tests/test_grpc_integration.cpp)

### Dodawanie nowej implementacji HAL

1. **Utwórz nowy katalog**: `src/hal/new_hal/`
2. **Zaimplementuj interfejsy**: `HALInterface`, `MotorControl`, `EncoderReader`
3. **Dodaj do fabryki HAL**: [`src/hal/hal_factory.cpp`](../../src/hal/hal_factory.cpp)
4. **Dodaj testy**: np. `tests/test_new_hal.cpp`

### Modyfikacja konfiguracji

1. **Dodaj pole** do struktury w [`include/config/configuration.h`](../../include/config/configuration.h)
2. **Dodaj ładowanie JSON** w [`src/config/configuration.cpp`](../../src/config/configuration.cpp)
3. **Dodaj walidację** w funkcji walidującej
4. **Użyj** w `MountController::Impl` z ochroną `shared_mutex`

### Dodawanie guardów NaN/Inf

Podczas dodawania nowych obliczeń matematycznych:

```cpp
double calculateSomething(double input) {
    // Guard na wejściu
    if (!std::isfinite(input)) {
        logger_->error("Invalid input in calculateSomething: {}", input);
        return 0.0;
    }
    
    double result = /* ... obliczenia ... */;
    
    // Guard na wyjściu
    if (!std::isfinite(result)) {
        logger_->error("Non-finite result in calculateSomething");
        return 0.0;
    }
    
    return result;
}
```

---

## 11. Rozwiązywanie problemów

### Problemy z budowaniem

| Problem | Rozwiązanie |
|---------|-------------|
| Brak gRPC/protobuf | `sudo apt install libgrpc-dev libprotobuf-dev protobuf-compiler-grpc` |
| Błąd: `nlohmann/json.hpp` not found | `sudo apt install nlohmann-json3-dev` |
| Błąd linkowania z SOFA | Sprawdź, czy katalog `sofa/` zawiera wszystkie pliki `.c` |
| Błąd: `Eigen3 not found` | `sudo apt install libeigen3-dev` |

### Problemy runtime

| Problem | Diagnoza | Rozwiązanie |
|---------|----------|-------------|
| Kontroler nie startuje | Sprawdź logi: `./build/astro_mount_controller --log-level debug` | Popraw konfigurację |
| Błąd kalibracji TPOINT | Sprawdź liczbę pomiarów | Minimum N pomiarów dla N parametrów |
| Śledzenie nie działa | Sprawdź, czy kontroler jest w stanie TRACKING | Wywołaj `TrackObject()` po `SlewToEquatorial()` |
| gRPC connection refused | Sprawdź, czy serwer jest uruchomiony | `grpcurl -plaintext localhost:50051 list` |

### Logowanie

```cpp
// Poziomy logowania: trace, debug, info, warn, error

Logger::getInstance().info("MountController initialized");
Logger::getInstance().debug("Position: RA={}, Dec={}", ra, dec);
Logger::getInstance().error("Calibration failed: {}", error_message);
```

```bash
# Uruchom z verbose logging
./build/astro_mount_controller --log-level debug

# Zobacz logi
tail -f /var/log/astro_mount_controller.log
```

---

## 12. Indeks plików

### Kluczowe pliki nagłówkowe

| Plik | Opis |
|------|------|
| [`include/controllers/mount_controller.h`](../../include/controllers/mount_controller.h) | Główna klasa kontrolera |
| [`include/controllers/icanopen_interface.h`](../../include/controllers/icanopen_interface.h) | Interfejs CANopen |
| [`include/config/configuration.h`](../../include/config/configuration.h) | System konfiguracji |
| [`include/core/astronomical_calculations.h`](../../include/core/astronomical_calculations.h) | Obliczenia astronomiczne |
| [`include/models/tpoint_model.h`](../../include/models/tpoint_model.h) | Model TPOINT |
| [`include/models/kalman_filter.h`](../../include/models/kalman_filter.h) | Filtr Kalmana |
| [`include/models/ephemeris_tracker.h`](../../include/models/ephemeris_tracker.h) | Śledzenie efemeryd |
| [`include/hal/hal_interface.h`](../../include/hal/hal_interface.h) | Główny interfejs HAL |
| [`include/hal/motor_control.h`](../../include/hal/motor_control.h) | Interfejs sterowania silnikiem |
| [`include/hal/encoder_reader.h`](../../include/hal/encoder_reader.h) | Interfejs odczytu enkodera |
| [`include/hal/safety_monitor.h`](../../include/hal/safety_monitor.h) | Monitor bezpieczeństwa |
| [`include/hal/sensor_interface.h`](../../include/hal/sensor_interface.h) | Interfejs czujników |
| [`include/hal/hal_factory.h`](../../include/hal/hal_factory.h) | Fabryka HAL |
| [`include/hal/hal_config.h`](../../include/hal/hal_config.h) | Konfiguracja HAL |

### Kluczowe pliki źródłowe

| Plik | Linie | Opis |
|------|-------|------|
| [`src/main.cpp`](../../src/main.cpp) | ~50 | Punkt wejścia |
| [`src/controllers/mount_controller.cpp`](../../src/controllers/mount_controller.cpp) | 5195 | Logika kontrolera |
| [`src/controllers/canopen_interface.cpp`](../../src/controllers/canopen_interface.cpp) | ~800 | Komunikacja CANopen |
| [`src/controllers/canopen_factory.cpp`](../../src/controllers/canopen_factory.cpp) | ~100 | Fabryka CANopen |
| [`src/config/configuration.cpp`](../../src/config/configuration.cpp) | ~300 | Ładowanie konfiguracji |
| [`src/core/astronomical_calculations.cpp`](../../src/core/astronomical_calculations.cpp) | ~500 | Obliczenia astronomiczne |
| [`src/models/tpoint_model.cpp`](../../src/models/tpoint_model.cpp) | ~400 | Implementacja TPOINT |
| [`src/models/kalman_filter.cpp`](../../src/models/kalman_filter.cpp) | ~300 | Implementacja filtru Kalmana |
| [`src/models/ephemeris_tracker.cpp`](../../src/models/ephemeris_tracker.cpp) | ~400 | Implementacja śledzenia efemeryd |
| [`src/api/service_impl.cpp`](../../src/api/service_impl.cpp) | ~800 | Implementacja usług gRPC |
| [`src/api/grpc_server.cpp`](../../src/api/grpc_server.cpp) | ~100 | Serwer gRPC |
| [`src/hal/hal_factory.cpp`](../../src/hal/hal_factory.cpp) | ~100 | Fabryka HAL |
| [`src/hal/canopen_hal/canopen_hal.cpp`](../../src/hal/canopen_hal/canopen_hal.cpp) | ~600 | Implementacja CANopenHAL |
| [`src/hal/simulated_hal/simulated_hal.cpp`](../../src/hal/simulated_hal/simulated_hal.cpp) | ~400 | Implementacja SimulatedHAL |

### Kluczowe komunikaty protobuf

| Komunikat | Linia w proto | Opis |
|-----------|---------------|------|
| `MountPosition` | 88 | Pozycja montażu (RA/Dec, Alt/Az) |
| `RotationMatrix` | 98 | Macierz rotacji (kwaternion) |
| `TPointParameters` | 107 | Parametry modelu TPOINT |
| `MountStatus` | 122 | Stan montażu (SLEWING, TRACKING, etc.) |
| `TrackedObject` | 141 | Śledzony obiekt |
| `ControllerState` | 198 | Pełny stan kontrolera |
| `PolePosition` | 240 | Pozycja bieguna |
| `AxisControlRequest` | 268 | Żądanie sterowania osią |
| `AxisStatus` | 289 | Status osi |
| `EphemerisData` | 309 | Dane efemeryd |
| `Measurement` | 340 | Pomiar kalibracyjny |
| `GuiderConfig` | 370 | Konfiguracja guidera |
| `HALConfigRequest` | 416 | Żądanie konfiguracji HAL |
| `PoleDeterminationRequest` | 447 | Żądanie wyznaczenia bieguna |
| `AxisPhysicalParameters` | 479 | Parametry fizyczne osi |
| `Configuration` | 520 | Konfiguracja |
| `HealthCheckRequest` | 580 | Żądanie health check |
| `SystemMetrics` | 600 | Metryki systemowe |
