# Propozycje eliminacji zależności CANopen z warstwy HAL

## 1. Wprowadzenie

Obecna architektura projektu zawiera **wyciek abstrakcji** — typy i interfejsy specyficzne dla CANopen (zdefiniowane w [`controllers/icanopen_interface.h`](../../include/controllers/icanopen_interface.h)) są bezpośrednio używane w warstwie HAL (`hal/hal_config.h`, `hal/hal_interface.h`) oraz w kontrolerach (`mount_controller.h`, `derotator_controller.h`).

Poniższy dokument identyfikuje wszystkie punkty sprzężenia i proponuje konkretne rozwiązania ich eliminacji.

---

## 2. Identyfikacja punktów sprzężenia

### 2.1 `hal/hal_config.h` — bezpośrednie include interfejsu CANopen

**Plik:** [`include/hal/hal_config.h:9`](../../include/hal/hal_config.h#9)

```cpp
#include "controllers/icanopen_interface.h"
```

**Problem:**  
`HALConfig` to struktura konfiguracyjna całej warstwy HAL, która powinna być niezależna od konkretnego protokołu transportowego. Include `icanopen_interface.h` powoduje, że każda jednostka kompilacji używająca `HALConfig` (w tym kontrolery, serwery gRPC, testy) ma wciągnięty cały interfejs CANopen.

**Miejsce użycia:** [`hal/hal_config.h:52`](../../include/hal/hal_config.h#52)
```cpp
struct {
    controllers::ICanOpenInterface::Config canopen_config;
    // ...
} canopen;
```

### 2.2 `hal/hal_interface.h` — enum `HALFeature::CANOPEN_SUPPORT`

**Plik:** [`include/hal/hal_interface.h:19`](../../include/hal/hal_interface.h#19)

```cpp
enum class HALFeature {
    CANOPEN_SUPPORT,      // Wsparcie CANopen/CiA 402
    // ...
};
```

**Problem:**  
Abstrakcyjny interfejs `HALInterface` definiuje cechę `CANOPEN_SUPPORT`, która jest nazwą konkretnego protokołu. Jeśli w przyszłości dodamy nowy protokół (np. EtherCAT), musielibyśmy rozszerzać ten enum, co narusza zasadę Open/Closed.

### 2.3 `controllers/mount_controller.h` — bezpośrednia zależność od `ICanOpenInterface`

**Plik:** [`include/controllers/mount_controller.h:12`](../../include/controllers/mount_controller.h#12)

```cpp
#include "controllers/icanopen_interface.h"
```

**Miejsca użycia:**
- [`mount_controller.h:241-242`](../../include/controllers/mount_controller.h#241) — `ICanOpenInterface::ServoInitEntry`
- [`mount_controller.h:841`](../../include/controllers/mount_controller.h#841) — metoda `getCanOpenInterface()` zwracająca `std::shared_ptr<ICanOpenInterface>`
- Liczne pola konfiguracyjne z prefiksem `canopen_` (`canopen_interface`, `canopen_node_id`, `canopen_bitrate`, itd.)

**Problem:**  
`MountController` to główny kontroler montażu, który powinien operować wyłącznie na abstrakcji `HALInterface`. Bezpośrednie odwołania do `ICanOpenInterface` czynią go zależnym od konkretnej implementacji transportowej.

### 2.4 `controllers/derotator_controller.h` — wstrzykiwanie `ICanOpenInterface*`

**Plik:** [`include/controllers/derotator_controller.h:12`](../../include/controllers/derotator_controller.h#12)

```cpp
#include "controllers/icanopen_interface.h"
```

**Miejsce użycia:** [`derotator_controller.h:67-68`](../../include/controllers/derotator_controller.h#67)
```cpp
DerotatorController(
    ICanOpenInterface* canopen,   // <-- wyciek CANopen do derotatora
    std::unique_ptr<hal::MotorControl> motor,
    // ...
);
```

**Problem:**  
Derotator otrzymuje bezpośredni wskaźnik do `ICanOpenInterface`, co oznacza, że wie o istnieniu CANopen. Powinien operować wyłącznie przez `MotorControl` i `EncoderReader` z HAL.

### 2.5 `hal/hal_factory.cpp` — bezpośrednie tworzenie `CanOpenFactory`

**Plik:** [`src/hal/hal_factory.cpp:7`](../../src/hal/hal_factory.cpp#7)

```cpp
#include "controllers/canopen_factory.h"
```

**Miejsce użycia:** [`hal_factory.cpp:284`](../../src/hal/hal_factory.cpp#284)
```cpp
auto canopen_interface = CanOpenFactory::create(canopen_config);
```

**Problem:**  
Fabryka HAL (`HALFactory`) bezpośrednio używa `CanOpenFactory`, co wprowadza zależność kompilacyjną na cały stos CANopen w momencie tworzenia HAL.

### 2.6 `api/canopen_server.h` — serwer gRPC specyficzny dla CANopen

**Plik:** [`include/api/canopen_server.h:7`](../../include/api/canopen_server.h#7)

```cpp
#include "controllers/icanopen_interface.h"
```

**Problem:**  
To mniej istotny problem, ponieważ `CanOpenServer` jest z założenia serwerem CANopen. Jednak sama architektura zakłada, że istnieje osobny serwer gRPC dla CANopen, co wymusza na klientach znajomość tego protokołu.

### 2.7 `hal/hal_config.h` — `DerotatorType::CANOPEN`

**Plik:** [`include/hal/hal_config.h:25`](../../include/hal/hal_config.h#25)

```cpp
enum class DerotatorType {
    CANOPEN = 0,
    STEPPER = 1,
    SERVO = 2,
    CUSTOM = 3
};
```

**Problem:**  
Typ derotatora zawiera wartość `CANOPEN`, co sugeruje, że sposób podłączenia derotatora jest tożsamy z protokołem transportowym. To powinno być rozdzielone.

---

## 3. Propozycje eliminacji

### 3.1 Eliminacja `#include "controllers/icanopen_interface.h"` z `HALConfig`

**Cel:** Usunięcie zależności typu `ICanOpenInterface` z `HALConfig`.

**Rozwiązanie A — własna struktura konfiguracyjna (zalecane):**

Zdefiniować w [`include/hal/hal_config.h`](../../include/hal/hal_config.h) osobną, płaską strukturę `CanOpenConfig` która nie zależy od `controllers::ICanOpenInterface::Config`:

```cpp
// Zamiast:
// #include "controllers/icanopen_interface.h"
// ...
// controllers::ICanOpenInterface::Config canopen_config;

// Definiujemy własną strukturę:
struct CanOpenConfig {
    std::string library{"mock"};
    std::string interface_name{"can0"};
    uint32_t bitrate{125000};
    uint8_t node_id{1};
    bool use_sync{true};
    uint32_t sync_period_ms{100};
    // ... pozostałe pola
};
```

**Konsekwencje:**
- Konwersja między `hal::CanOpenConfig` a `ICanOpenInterface::Config` odbywa się w [`src/hal/hal_factory.cpp`](../../src/hal/hal_factory.cpp) — tylko tam, gdzie faktycznie tworzymy `CanOpenFactory`.
- `HALConfig` przestaje zależeć od `controllers/icanopen_interface.h`.
- Wszystkie kontrolery (mount, derotator) które includują `HALConfig` nie widzą już interfejsu CANopen.

### 3.2 Eliminacja `HALFeature::CANOPEN_SUPPORT`

**Cel:** Usunięcie nazwy konkretnego protokołu z abstrakcyjnego interfejsu.

**Rozwiązanie — generyczny mechanizm capabilities:**

```cpp
enum class HALFeature {
    FIELD_BUS_SUPPORT,    // Zamiast CANOPEN_SUPPORT
    SERIAL_SUPPORT,
    ETHERNET_SUPPORT,
    PID_CONTROL,
    // ... reszta bez zmian
};
```

Alternatywnie, zastąpić enum `HALFeature` systemem string-kluczy:
```cpp
virtual bool supportsFeature(const std::string& feature_name) const = 0;
// Użycie: hal->supportsFeature("canopen")  — tylko gdy faktycznie pytamy o CANopen
```

### 3.3 Eliminacja zależności `ICanOpenInterface` z `MountController`

**Cel:** `MountController` nie powinien includować ani używać typów z `icanopen_interface.h`.

**Rozwiązanie A — całkowite ukrycie CANopen za HAL:**

1. Usunąć `#include "controllers/icanopen_interface.h"` z [`mount_controller.h`](../../include/controllers/mount_controller.h).
2. Usunąć metodę `getCanOpenInterface()` — nikt poza kontrolerem nie powinien mieć dostępu do surowego interfejsu CANopen.
3. Zastąpić `ICanOpenInterface::ServoInitEntry` własną strukturą w `HALConfig` lub w `mount_controller.h`:

```cpp
// W mount_controller.h lub nowym hal/types.h
struct ServoInitEntry {
    int axis = 0;
    uint16_t index = 0;
    uint8_t subindex = 0;
    int32_t value = 0;
    std::string description;
    uint8_t data_size = 4;
};
```

4. Przenieść logikę CANopen position rewind do implementacji `CanOpenHAL` (tam gdzie jest dostęp do `ICanOpenInterface`), a do `MountController` wystawić tylko prosty interfejs, np.:

```cpp
// W HALInterface:
virtual bool setActualPosition(int axis_id, double position) { return false; }
virtual bool isFieldBusConnected() const { return false; }
```

**Rozwiązanie B — opóźnione wiązanie przez callback:**

Zamiast wstrzykiwać `ICanOpenInterface`, kontroler przekazuje callbacki:
```cpp
using PositionRewindCallback = std::function<bool(int axis, double pos)>;
// MountController ma tylko callback, nie wie o CANopen
```

### 3.4 Eliminacja `ICanOpenInterface*` z `DerotatorController`

**Cel:** Derotator nie powinien znać CANopen.

**Rozwiązanie:**

Obecnie `DerotatorController` przyjmuje `ICanOpenInterface*` w konstruktorze ([`derotator_controller.h:67`](../../include/controllers/derotator_controller.h#67)). Jest to używane w metodach `runCANopenDerotatorCalibration()`, `calibrateAbsoluteEncoder()` itp.

1. Przenieść logikę kalibracji specyficzną dla CANopen do `CanOpenHAL` lub do osobnego dedykowanego serwisu.
2. `DerotatorController` powinien operować wyłącznie przez `hal::MotorControl` i `hal::EncoderReader`.
3. Jeśli derotator potrzebuje wysłać SDO, powinno to być opakowane w metodę `MotorControl`, np. `configure(const std::string& key, const std::vector<uint8_t>& value)`.

```cpp
// Docelowy konstruktor DerotatorController:
DerotatorController(
    std::unique_ptr<hal::MotorControl> motor,
    std::unique_ptr<hal::EncoderReader> encoder,
    MountStateProvider state_provider,
    const Config& config);
```

### 3.5 Eliminacja `CanOpenFactory` z `HALFactory`

**Cel:** `HALFactory` nie powinna bezpośrednio wołać `CanOpenFactory`.

**Rozwiązanie — odwrócenie zależności (DIP):**

```cpp
// W hal_factory.h, zamiast bezpośredniego include:
// forward declare
class CanOpenHAL;

// W hal_factory.cpp, CanOpenHAL sam zarządza swoim ICanOpenInterface:
std::unique_ptr<HALInterface> HALFactory::createCanOpenHAL(const HALConfig& config) {
    // CanOpenHAL sam tworzy ICanOpenInterface w swoim initialize()
    auto hal = std::make_unique<CanOpenHAL>();
    if (!hal->initialize(config)) {
        throw std::runtime_error("Failed to initialize CanOpenHAL");
    }
    return hal;
}
```

Lub alternatywnie, użyć PIMPL lub fabryki rejestracyjnej:
```cpp
// CanOpenHAL.cpp — sam rejestruje się w HALFactory
// bool registered = HALFactory::registerType("canopen", [](const HALConfig& cfg) {
//     return std::make_unique<CanOpenHAL>();
// });
```

### 3.6 Eliminacja `DerotatorType::CANOPEN`

**Cel:** Rozdzielenie typu urządzenia od protokołu transportowego.

**Rozwiązanie:**

```cpp
// Zamiast:
enum class DerotatorType {
    CANOPEN = 0,  // ← łączy typ z protokołem
    STEPPER = 1,
    SERVO = 2,
    CUSTOM = 3
};

// Proponowane:
enum class DerotatorType {
    STEPPER = 0,
    SERVO = 1,
    DIRECT_DRIVE = 2,
    CUSTOM = 3
};
// Sposób podłączenia (CANopen, Step/Dir, EtherCAT) jest konfiguracją transportową,
// niezależną od typu silnika.
```

### 3.7 Refaktoryzacja serwera gRPC

**Cel:** Umożliwienie korzystania z gRPC bez znajomości CANopen.

**Rozwiązanie:**

Obecny [`canopen_server.h`](../../include/api/canopen_server.h) jest serwerem gRPC dla CANopen. Proponuje się:

1. Zachować `CanOpenServer` jako implementację szczegółową.
2. Dodać ogólny `MountControlServer`, który operuje na `HALInterface` i `MountController`, a nie na `ICanOpenInterface`.
3. Klienci gRPC powinni komunikować się z `MountControlServer`, który wewnętrznie deleguje do odpowiedniej implementacji HAL.

---

## 4. Plan migracji (kroki)

| Krok | Opis | Pliki do modyfikacji | Ryzyko |
|------|------|---------------------|--------|
| 1 | Wydzielenie `CanOpenConfig` z `HALConfig` | `hal_config.h`, `hal_factory.cpp` | Niskie |
| 2 | Zmiana `HALFeature::CANOPEN_SUPPORT` na `FIELD_BUS_SUPPORT` | `hal_interface.h`, implementacje HAL | Niskie |
| 3 | Dodanie brakujących metod do `HALInterface` (setActualPosition itp.) | `hal_interface.h`, wszystkie implementacje HAL | Średnie |
| 4 | Usunięcie `ICanOpenInterface*` z `DerotatorController` | `derotator_controller.h/.cpp`, `mount_controller_impl.cpp` | Wysokie |
| 5 | Usunięcie `#include icanopen_interface.h` z `mount_controller.h` | `mount_controller.h/.cpp`, mount_controller_impl.cpp | Wysokie |
| 6 | Ukrycie `CanOpenFactory` za `CanOpenHAL` | `hal_factory.cpp`, `canopen_hal.cpp` | Średnie |
| 7 | Zmiana `DerotatorType::CANOPEN` na generyczny typ | `hal_config.h`, `derotator_controller.cpp` | Niskie |
| 8 | Dodanie `MountControlServer` (gRPC) niezależnego od CANopen | Nowy plik + `service_impl.cpp` | Średnie |

### Priorytety

1. **Krok 1 i 2** — natychmiastowe, bezpieczne zmiany które znacząco redukują wyciek.
2. **Krok 3 i 6** — średni priorytet, wymagają dodania nowych metod do interfejsu.
3. **Krok 4 i 5** — najtrudniejsze, wymagają przeniesienia logiki CANopen z kontrolerów do HAL.
4. **Krok 7 i 8** — kosmetyczne/docelowe, mogą być wykonane później.

---

## 5. Podsumowanie

Obecna architektura ma 7 głównych punktów wycieku CANopen poza warstwę implementacyjną. Proponowane zmiany eliminują te zależności poprzez:

1. **Wydzielenie konfiguracji CANopen** z `HALConfig` do niezależnej struktury.
2. **Zastąpienie nazw protokołów** w enumach generycznymi odpowiednikami.
3. **Rozszerzenie `HALInterface`** o brakujące metody, tak aby kontrolery nie musiały znać CANopen.
4. **Ukrycie fabryki CANopen** za implementacją `CanOpenHAL`.
5. **Oddzielenie typu urządzenia od protokołu transportowego** w konfiguracji derotatora.

Po wprowadzeniu tych zmian, jedynymi plikami które będą świadome istnienia CANopen będą:
- [`include/hal/canopen_hal/canopen_hal.h`](../../include/hal/canopen_hal/canopen_hal.h) — implementacja
- [`src/hal/canopen_hal/`](../../src/hal/canopen_hal/) — pliki źródłowe implementacji
- [`lib/canopen_wrapper/`](../../lib/canopen_wrapper/) — wrapper biblioteki CANopen
- [`include/api/canopen_server.h`](../../include/api/canopen_server.h) — serwer gRPC (opcjonalnie)

Wszystkie pozostałe komponenty (HALInterface, HALConfig, MountController, DerotatorController, HALFactory) będą operować wyłącznie na abstrakcjach niezależnych od konkretnego protokołu transportowego.

---

*Dokument utworzony: 13 lipca 2026*
