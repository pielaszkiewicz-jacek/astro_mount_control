# Analiza możliwości dodania interfejsu ST4

## 1. Wprowadzenie

Port ST4 (Standard ST-4, RJ12, 6-pin) to standardowy interfejs stosowany w astronomii do
przekazywania impulsów korekcyjnych z autoguidéra (np. PHD2, ASIAIR, MetaGuide) bezpośrednio
do montażu. Sygnał składa się z 4 linii optoizolowanych: **North**, **South**, **East**, **West**,
aktywowanych stanem niskim (GND).

Niniejszy dokument opisuje analizę możliwości dodania natywnej obsługi interfejsu ST4
w projekcie [`astro_mount_control`](../README.md).

---

## 2. Stan obecny projektu

### 2.1 Architektura warstwy HAL

Projekt posiada czystą abstrakcję warstwy sprzętowej ([`include/hal/hal_interface.h`](../../include/hal/hal_interface.h:32)):
- [`HALInterface`](../../include/hal/hal_interface.h:32) — klasa bazowa z fabrykami komponentów
- [`HALType`](../../include/hal/hal_config.h:12) — enum typów: `SIMULATED`, `CANOPEN`, `MF7025V2`, `SERIAL`, `ETHERNET`, `GAMEPAD`, `CUSTOM`
- [`HALFactory`](../../include/hal/hal_factory.h:11) — rejestracja i tworzenie implementacji HAL
- [`HALFeature`](../../include/hal/hal_interface.h:18) — możliwości: `MANUAL_CONTROL`, `SERIAL_SUPPORT`, itp.

### 2.2 Istniejące implementacje HAL

| Implementacja | Plik główny | Status |
|--------------|-------------|--------|
| `SimulatedHAL` | [`src/hal/simulated_hal/simulated_hal.cpp`](../../src/hal/simulated_hal/simulated_hal.cpp) | Pełna symulacja |
| `CanOpenHAL` | [`src/hal/canopen_hal/canopen_hal.cpp`](../../src/hal/canopen_hal/canopen_hal.cpp) | CANopen/CiA 402 |
| `Mf7025v2Hal` | [`src/hal/mf7025v2_hal/mf7025v2_hal.cpp`](../../src/hal/mf7025v2_hal/mf7025v2_hal.cpp) | LingKong BLDC |
| `SerialHAL` | [`src/hal/serial_hal/serial_hal.cpp`](../../src/hal/serial_hal/serial_hal.cpp) | RS-232/485 Modbus RTU |
| `EthernetHAL` | [`src/hal/ethernet_hal/ethernet_hal.cpp`](../../src/hal/ethernet_hal/ethernet_hal.cpp) | Ethernet Modbus TCP |
| `GamepadHAL` | [`src/hal/gamepad_hal/gamepad_hal.cpp`](../../src/hal/gamepad_hal/gamepad_hal.cpp) | Manualne sterowanie |

### 2.3 Istniejący mechanizm guidingu

Projekt **posiada już w pełni działający mechanizm korekcji guidingu**:

1. **Warstwa kontrolera** ([`MountController::applyGuiderCorrection()`](../../src/controllers/mount_controller.cpp:4429)):
   - Przyjmuje korekcje RA/Dec w arcsekundach
   - Stosuje clamp, aggression, cos(Dec) dla RA
   - Przelicza na offset pozycji (servo degrees) przez gear_ratio
   - Akumuluje w `guider_delta_axis1_` / `guider_delta_axis2_`
   - Pętla trackingu odczytuje i zeruje delty przy każdej iteracji

2. **Warstwa ASCOM** ([`AstroMountTelescope.PulseGuide()`](../../ascom/AstroMountTelescope.cs:401)):
   - Implementuje ASCOM `PulseGuide(GuideDirection, int durationMs)`
   - Konwertuje kierunek ST4 + czas trwania na korekcję RA/Dec (arcsec)
   - Wysyła przez gRPC jako `GuiderCorrection{ra_correction, dec_correction}`
   - Domyślna prędkość guidingu: **0.5× sidereal = 7.5 arcsec/s**

3. **Warstwa gRPC** ([`MountControllerServiceImpl::SendGuiderCorrection()`](../../src/api/service_impl.cpp:800)):
   - Akceptuje `GuiderCorrection` przez protobuf
   - Wywołuje `controller_.applyGuiderCorrection()`

4. **Konfiguracja** ([`config/test_no_hardware.json`](../../config/test_no_hardware.json:63)):
   ```json
   "guider": {
     "enabled": false,
     "connection_string": "",
     "max_correction_arcsec": 5.0
   }
   ```

### 2.4 Istniejąca architektura SerialHAL

[`SerialHAL`](../../src/hal/serial_hal/serial_hal.h:18) jest w pełni funkcjonalną implementacją HAL dla portów szeregowych:

- **Protokół bazowy**: Modbus RTU przez RS-232/485
- **Porty**: `/dev/ttyUSB0`, `/dev/ttyAMA0`, itp.
- **Prędkości**: 9600–230400 bps
- **Format ramki**: Modbus RTU z CRC16
- **Funkcje Modbus**: Read Holding Registers (0x03), Write Single Register (0x06), Write Multiple Registers (0x10)
- **Wątek monitorujący**: Sprawdzanie połączenia co 5s

Ponadto projekt udokumentował rozszerzenie protokołu LX200 przez RS-485
([`docs/pl/rs485_lx200_integracja.md`](../../docs/pl/rs485_lx200_integracja.md)) jako alternatywę
dla CANopen, z własnymi komendami tekstowymi do sterowania osiami.

---

## 3. Opcje implementacji ST4

### Opcja A: Pełna implementacja GPIO (dedykowany St4HAL)

Nowy typ HAL `ST4` jako osobna warstwa sprzętowa monitorująca piny GPIO przez `libgpiod`.
Szczegółowo opisana w sekcji 4.

### Opcja B: Implementacja przez SerialHAL

Wykorzystanie istniejącego [`SerialHAL`](../../src/hal/serial_hal/serial_hal.cpp) do obsługi ST4
poprzez różne konfiguracje sprzętowe opisane w sekcji 5.

### Opcja C: Rozszerzenie istniejącej implementacji ASCOM

Obecna implementacja ASCOM już obsługuje `PulseGuide()`. Jest to jednak rozwiązanie
**programowe** — wymaga działającego klienta ASCOM (Windows) i połączenia gRPC.
Nie zapewnia natywnego ST4 na poziomie kontrolera (Raspberry Pi).

---

## 4. Opcja A — Dedykowany St4HAL (GPIO)

### 4.1 Nowe typy wyliczeniowe

```cpp
// W include/hal/hal_config.h (linia 12-20)
enum class HALType {
    // ... istniejące ...
    ST4,  // NOWY: ST4 guiding port (GPIO)
};

// W include/hal/hal_interface.h (linia 18-29)
enum class HALFeature {
    // ... istniejące ...
    ST4_GUIDING,       // NOWY: obsługa portu ST4
};
```

### 4.2 Nowa konfiguracja ST4 w HALConfig

```cpp
// W include/hal/hal_config.h (po linii 124)
struct {
    // Mapowanie pinów GPIO (Raspberry Pi)
    struct {
        int north{17};   // GPIO 17 = pin 11
        int south{27};   // GPIO 27 = pin 13
        int east{22};    // GPIO 22 = pin 15
        int west{23};    // GPIO 23 = pin 16
    } gpio_pins;
    
    // Parametry guidingu
    double guide_rate_arcsec_per_sec{7.5};   // 0.5× sidereal
    int debounce_ms{5};                       // Debounce w ms
    int min_pulse_ms{8};                     // Minimalny czas impulsu
    int max_pulse_ms{3000};                  // Maksymalny czas impulsu
    double aggression{0.8};                  // Agresja korekcji (0-1)
    
    // Typ backendu
    std::string backend{"gpio"};             // "gpio", "libgpiod"
} st4;
```

### 4.3 Implementacja St4HAL

Główna klasa [`St4HAL`](../../src/hal/st4_hal/st4_hal.h) implementująca [`HALInterface`](../../include/hal/hal_interface.h:32):

```cpp
class St4HAL : public HALInterface {
public:
    bool initialize(const HALConfig& config) override;
    void shutdown() override;
    bool isInitialized() const override;
    
    // Fabryki — delegują do HAL skonfigurowanego dla silników
    std::unique_ptr<MotorControl> createMotorControl(int axis_id) override;
    std::unique_ptr<EncoderReader> createEncoderReader(int axis_id) override;
    std::unique_ptr<SafetyMonitor> createSafetyMonitor() override;
    std::unique_ptr<SensorInterface> createSensorInterface() override;
    
    std::vector<HALFeature> getSupportedFeatures() const override;
    bool supportsFeature(HALFeature feature) const override;
    
    bool start() override;   // Uruchamia wątek monitorowania ST4
    bool stop() override;    // Zatrzymuje wątek
    bool isRunning() const override;
    
    // Rejestruje callback do korekcji guidingu
    using GuideCorrectionCallback = std::function<
        void(double ra_correction_arcsec, double dec_correction_arcsec)>;
    void setGuideCorrectionCallback(GuideCorrectionCallback callback);
    
private:
    void monitorLoop();     // Główna pętla monitorowania pinów
    void handlePulse(int direction, int duration_ms);
    
    // Delegowany HAL dla silników
    std::unique_ptr<HALInterface> motor_hal_;
    
    // Stan pinów i timerów
    struct St4PulseState {
        std::chrono::steady_clock::time_point edge_time;
        bool active{false};
        int gpio_pin{-1};
    };
    std::array<St4PulseState, 4> pulse_states_;  // N, S, E, W
    
    // Wątek monitorowania
    std::thread monitor_thread_;
    std::atomic<bool> running_{false};
};
```

### 4.4 Algorytm wykrywania impulsów ST4 (GPIO)

```
1. Inicjalizacja GPIO (libgpiod):
   - Otwórz linie GPIO (N=GPIO17, S=GPIO27, E=GPIO22, W=GPIO23)
   - Ustaw kierunek na "input"
   - Skonfiguruj zbocza detekcji: "falling" (active-low ST4 → falling = start pulse)
   - Utwórz event loop z callbackami na zbocza

2. Detekcja impulsu przez przerwania:
   ┌─────┐                  ┌─────┐
   │ HIGH│                  │     │
   │     │                  │     │  (ST4 active-low:
   │     │                  │     │   normal=HIGH,
   └─────┴──────────────────┴─────┴──► pulse=LOW)
         ↑                   ↑
    falling edge        rising edge
    (start pulse)       (end pulse)
    ↓                   ↓
    timestamp_start     timestamp_end
    duration = t_end - t_start

3. Filtracja (debounce):
   - duration < min_pulse_ms (8ms) → ignoruj (zakłócenia)
   - duration > max_pulse_ms (3000ms) → ogranicz do max
   - duration w zakresie → akceptuj

4. Konwersja na korekcję:
   - guide_rate = 7.5 arcsec/s (0.5× sidereal, konfigurowalne)
   - correction_arcsec = guide_rate * duration_s
   
   North → dec_correction = +correction
   South → dec_correction = -correction
   East  → ra_correction  = +correction
   West  → ra_correction  = -correction

5. Przekazanie do MountController:
   guideCorrectionCallback(ra_correction, dec_correction)
   → applyGuiderCorrection()
     → clamping, cos(δ), aggression
     → guider_delta_axis1_ / guider_delta_axis2_
     → tracking loop aplikuje offset
```

### 4.5 Modyfikacja HALFactory

```cpp
// W src/hal/hal_factory.cpp (linia 17-36)
case HALType::ST4:
    return createSt4HAL(config);
```

### 4.6 Modyfikacja MountController

```cpp
// W include/controllers/mount_controller.h
struct St4GuiderConfig {
    bool enabled{false};
    int gpio_north{17};     // GPIO pin for North
    int gpio_south{27};     // GPIO pin for South
    int gpio_east{22};      // GPIO pin for East
    int gpio_west{23};      // GPIO pin for West
    double guide_rate_arcsec_per_sec{7.5};
    double aggression{0.8};
    int debounce_ms{5};
};
```

### 4.7 Modyfikacja konfiguracji JSON

```json
{
  "hal": {
    "type": "st4",
    "st4": {
      "gpio_pins": {
        "north": 17, "south": 27, "east": 22, "west": 23
      },
      "guide_rate_arcsec_per_sec": 7.5,
      "aggression": 0.8,
      "debounce_ms": 5,
      "min_pulse_ms": 8,
      "max_pulse_ms": 3000
    }
  }
}
```

---

## 5. Opcja B — Implementacja ST4 przez SerialHAL (szczegółowa analiza)

### 5.1 Koncepcja ogólna

Zamiast tworzyć nowy typ HAL, istniejący [`SerialHAL`](../../src/hal/serial_hal/serial_hal.h:18)
może zostać rozszerzony o monitorowanie wejść cyfrowych (dla ST4) poprzez:

1. **Modbus RTU Digital Input Module** — zewnętrzny moduł I/O z interfejsem RS-485
2. **USB-GPIO dongle** (FTDI FT232H, CH341) — konwerter USB↔GPIO na serial
3. **Dedykowany kontroler ST4↔RS232** — sprzętowy konwerter ST4 na komunikaty szeregowe
4. **Protokół LX200** — rozszerzenie istniejącego protokołu tekstowego o komendy guidingu

### 5.2 Podejście 1: Modbus RTU Digital Input Module

#### Opis sprzętu

Moduły I/O z interfejsem Modbus RTU przez RS-485, np.:

| Moduł | Wejścia | I/O | Interfejs | Cena |
|-------|---------|-----|-----------|------|
| ADAM-4055 (Advantech) | 8 DI | 2 DO | RS-485 Modbus | ~200 PLN |
| ICP DAS I-7041 | 14 DI | - | RS-485 Modbus | ~180 PLN |
| Waveshare RS485 MODBUS | 8 DI | 8 DO | RS-485 Modbus | ~80 PLN |
| NodeMCU + MAX485 | 6+ DI | - | WiFi/RS-485 | ~30 PLN (DIY) |

#### Schemat blokowy

```
┌─────────────┐     ┌──────────────┐     ┌──────────────┐     ┌─────────────────┐
│ ST4 Port    │────▶│ Modbus DI    │────▶│ SerialHAL    │────▶│ MountController │
│ (opto)      │     │ Module       │     │ (rozszerzony)│     │ (guiding loop)  │
│             │     │ RS-485       │     │              │     │                 │
│ North ──────┤     │ ┌──────────┐ │     │ - Poll DI    │     │ applyGuider     │
│ South ──────┤     │ │ DI0 ──── │ │     │   registers  │     │ Correction()    │
│ East  ──────┤     │ │ DI1 ──── │ │     │ - Detect     │     │                 │
│ West  ──────┤     │ │ DI2 ──── │ │     │   state      │     │ - Cos(Dec)      │
│ GND   ──────┤     │ │ DI3 ──── │ │     │   changes    │     │ - Aggression    │
│             │     │ └──────────┘ │     │ - Measure    │     │ - Clamp         │
└─────────────┘     └──────────────┘     │   duration   │     └─────────────────┘
                                         └──────────────┘
```

#### Modyfikacje SerialHAL

```cpp
// W src/hal/serial_hal/serial_hal.h
class SerialHAL : public HALInterface {
    // ... istniejące ...
    
    // NOWE: Tryb ST4 przez Modbus DI
    struct St4ModbusConfig {
        bool enabled{false};
        uint8_t node_id{1};                 // Modbus node ID modułu DI
        uint16_t di_register{0x0000};       // Rejestr wejść cyfrowych
        uint16_t di_north_bit{0};           // Bit dla North (DI0)
        uint16_t di_south_bit{1};           // Bit dla South (DI1)
        uint16_t di_east_bit{2};            // Bit dla East (DI2)
        uint16_t di_west_bit{3};            // Bit dla West (DI3)
        int poll_interval_ms{2};            // Interwał pollingu (2ms = 500Hz)
        double guide_rate_arcsec_per_sec{7.5};
        double aggression{0.8};
        int debounce_ms{5};
        int min_pulse_ms{8};
        int max_pulse_ms{3000};
    } st4_modbus;
    
    // NOWE: Metody dla ST4
    void setGuideCorrectionCallback(GuideCorrectionCallback callback);
    
private:
    // NOWY wątek monitorowania ST4
    void st4MonitorLoop();
    std::thread st4_thread_;
    std::atomic<bool> st4_running_{false};
    
    // Stan ST4
    struct St4PulseState {
        std::chrono::steady_clock::time_point start_time;
        bool last_state{false};             // Poprzedni stan bitu
        bool pulse_active{false};
    };
    std::array<St4PulseState, 4> st4_pins_; // N, S, E, W
};
```

#### Algorytm (polling Modbus RTU)

```
1. Inicjalizacja:
   - Otwórz port RS-485 (standardowy flow SerialHAL)
   - Skonfiguruj Modbus node ID i mapowanie bitów

2. Pętla monitorowania (500Hz, co 2ms):
   ┌──────────────────────────────────────────────────────┐
   │ while (st4_running_) {                               │
   │   // Odczyt rejestru wejść cyfrowych (Modbus FC 02)  │
   │   uint8_t di_value = readDiscreteInputs(node_id,     │
   │                               di_register, 1);       │
   │                                                      │
   │   for (each ST4 pin N/S/E/W) {                      │
   │     bool current = (di_value >> bit) & 1;            │
   │     bool prev = st4_pins[pin].last_state;            │
   │                                                      │
   │     if (prev == HIGH && current == LOW) {            │
   │       // Falling edge = start pulse                  │
   │       st4_pins[pin].start_time = now;                │
   │       st4_pins[pin].pulse_active = true;             │
   │     }                                                │
   │     else if (prev == LOW && current == HIGH) {       │
   │       // Rising edge = end pulse                     │
   │       if (st4_pins[pin].pulse_active) {              │
   │         duration = now - start_time;                 │
   │         if (duration > min_pulse_ms)                 │
   │           handlePulse(pin, duration);                │
   │         st4_pins[pin].pulse_active = false;          │
   │       }                                              │
   │     }                                                │
   │     st4_pins[pin].last_state = current;              │
   │   }                                                  │
   │                                                      │
   │   sleep_until(last_poll + 2ms);                      │
   │ }                                                    │
   └──────────────────────────────────────────────────────┘

3. Konwersja i korekcja — identycznie jak w Opcji A (sekcja 4.4, krok 4-5)
```

#### Zalety

- **Wykorzystanie istniejącego kodu** — minimalne modyfikacje `SerialHAL`
- **Brak GPIO** — działa na każdej platformie z portem RS-485 (RPi, x86, itp.)
- **Izolacja galwaniczna** — moduły Modbus DI (np. ADAM-4055) mają wbudowaną izolację
- **Niska cena** — moduł Modbus DI ~80-200 PLN
- **Rozszerzalność** — można dodać więcej wejść (limit switches, czujniki)

#### Wady

- **Opóźnienie pomiaru** — polling 2ms + czas odpowiedzi Modbus (~500µs) = ±2-3ms błędu
- **Zależność od szybkości RS-485** — przy 115200 bps, ramka Modbus trwa ~1.5ms
- **Większe opóźnienie niż GPIO** — przez dodatkowy interfejs szeregowy
- **Zajęty port RS-485** — może kolidować z komunikacją z napędami

### 5.3 Podejście 2: USB-GPIO dongle (FTDI FT232H / CH341 / MCP2221)

#### Opis sprzętu

Konwertery USB↔GPIO, które prezentują się jako wirtualne porty szeregowe (USB CDC),
ale dodatkowo umożliwiają sterowanie pinami GPIO przez komendy `ioctl()` lub specjalne
protokoły:

| Układ | GPIO | Interfejs | Biblioteka |
|-------|------|-----------|------------|
| FTDI FT232H | 8 | USB 2.0 High-Speed | `libftdi1` lub `MPSSE` |
| FTDI FT4232H | 12 | USB 2.0 High-Speed | `libftdi1` |
| CH341 | 8 | USB 2.0 Full-Speed | `libusb` lub `ch347` |
| MCP2221 (Microchip) | 4 | USB 2.0 Full-Speed | `hidapi` lub `libmcp2221` |

#### Integracja z SerialHAL

Zamiast rozszerzać [`SerialHAL`](../../src/hal/serial_hal/serial_hal.h), te urządzenia
wymagają **osobnej warstwy GPIO** (podobnie jak Opcja A), ponieważ komunikują się przez
USB, a nie przez port szeregowy typu UART/RS-232:

- FTDI FT232H używa `libftdi1` z trybem `MPSSE` (bitbang) — **nie jest to standardowe API szeregowe**
- CH341 używa `libusb` z trybem bitbang — **wymaga `libusb`**
- MCP2221 używa `hidapi` — **wymaga `hidapi`**

**Wniosek**: Te urządzenia NIE pasują do modelu [`SerialHAL`](../../src/hal/serial_hal/serial_hal.cpp)
(otwarcie portu szeregowego, `read()`/`write()`). Wymagają osobnej implementacji jako
nowy backend w Opcji A (St4HAL) lub dedykowany `UsbGpioHAL`.

### 5.4 Podejście 3: Dedykowany konwerter ST4↔RS232

#### Opis koncepcji

Niewielkie urządzenie (mikrokontroler + MAX232) konwertujące sygnały ST4 na komunikaty
tekstowe przez RS-232. Np. na Arduino Pro Mini:

- Wejścia: 4 × optoizolowane ST4 (N/S/E/W)
- Wyjście: RS-232 (przez MAX232) lub USB (przez CH340)
- Protokół: ASCII, np. `:GN200#` = Guide North 200ms, `:GS150#` = Guide South 150ms

#### Protokół

```
Format ramki wyjściowej (z konwertera do kontrolera):
  :G{direction}{duration}#

  gdzie:
    G        - prefix guiding
    direction: N=North, S=South, E=East, W=West
    duration - czas impulsu w ms (1-3000)
    #        - terminator

Przykład:
  :GN200#  → impuls North, 200ms
  :GS050#  → impuls South, 50ms
  :GE150#  → impuls East, 150ms
  :GW300#  → impuls West, 300ms
```

#### Integracja z SerialHAL

```cpp
// W src/hal/serial_hal/serial_hal.h
class SerialHAL : public HALInterface {
    // ... istniejące ...
    
    // NOWY tryb: ST4 przez dedykowany konwerter RS-232
    struct St4ConverterConfig {
        bool enabled{false};
        std::string protocol{"st4_ascii"};  // "st4_ascii", "lx200"
    } st4_converter;
};
```

Parser w pętli odczytu:

```cpp
void SerialHAL::st4MonitorLoop() {
    uint8_t buf[256];
    while (st4_running_) {
        size_t bytes_read;
        if (readPort(buf, sizeof(buf), bytes_read, 10)) {
            for (size_t i = 0; i < bytes_read; i++) {
                parseChar(buf[i]);  // Akumulacja ramki
                if (complete_frame) {
                    // :GN200# → dec_correction = 7.5 * 0.2 = 1.5 arcsec
                    handlePulse(frame.direction, frame.duration_ms);
                }
            }
        }
        // Sprawdź timeouty aktywnych impulsów
        checkPulseTimeouts();
    }
}
```

#### Zalety

- **Bardzo niskie opóźnienie** — przerwania sprzętowe na mikrokontrolerze
- **Prosta integracja** — tylko parser ASCII w SerialHAL
- **Izolacja galwaniczna** — opcja na poziomie konwertera
- **Niski koszt** — Arduino Pro Mini ~15 PLN + MAX232 ~5 PLN

#### Wady

- **Wymaga dodatkowego sprzętu** (mikrokontroler + konwerter RS-232)
- **Dodatkowe opóźnienie transmisji** — ramka ASCII trwa ~2ms @ 115200 bps
- **Zajęty port szeregowy** — konflikt z innymi urządzeniami na tym samym porcie

### 5.5 Podejście 4: Rozszerzenie protokołu LX200

#### Opis

Projekt posiada już udokumentowany protokół LX200 przez RS-485
([`docs/pl/rs485_lx200_integracja.md`](../../docs/pl/rs485_lx200_integracja.md)) jako
alternatywa dla CANopen. Protokół ten można rozszerzyć o komendy guidingu.

#### Istniejące podstawy

W dokumentacji LX200 zdefiniowano już komendy sterowania osiami:
```
:A0Sr+123.4567#  → set position
:A0Rv+1.5041#    → set velocity
:A0Q#            → stop
```

#### Nowe komendy guidingu

```
:Mgnn#           → Move guide North przez nn jednostek (0.1 arcsec?)
:Mgsnn#          → Move guide South
:Mgenn#          → Move guide East
:Mgwnn#          → Move guide West

lub z czasem impulsu (zgodne z ASCOM PulseGuide):
:MGn,duration#   → PulseGuide North, duration ms
:MGs,duration#   → PulseGuide South
:MGe,duration#   → PulseGuide East
:MGw,duration#   → PulseGuide West
```

#### Integracja

Nowe komendy byłyby parsowane w [`SerialHAL`](../../src/hal/serial_hal/serial_hal.cpp)
lub w adapterze [`LX200Interface`](../../include/controllers/lx200_interface.h) (który
implementuje `ICanOpenInterface` dla LX200). W przypadku adaptera:

```cpp
// W lx200_interface.cpp
bool LX200Interface::parseAndExecute(const std::string& cmd) {
    if (cmd.size() >= 4 && cmd[1] == 'M' && cmd[2] == 'G') {
        // :MGs,150# → guide South, 150ms
        char dir = cmd[3];
        int duration_ms = std::stoi(cmd.substr(5));  // po przecinku
        
        // Wywołanie applyGuiderCorrection przez callback
        if (guide_callback_) {
            double correction = 7.5 * duration_ms / 1000.0;
            switch (dir) {
                case 'n': guide_callback_(0.0, +correction); break;
                case 's': guide_callback_(0.0, -correction); break;
                case 'e': guide_callback_(+correction, 0.0); break;
                case 'w': guide_callback_(-correction, 0.0); break;
            }
        }
        return true;
    }
    // ... istniejący parser LX200 ...
}
```

#### Zalety

- **Brak dodatkowego sprzętu** — LX200 już zakłada połączenie RS-485
- **Spójność protokołu** — wszystkie komendy w jednym standardzie
- **Kompatybilność z OnStep** — podobny protokół używany w OnStep (`:Mgn#` itp.)
- **Możliwość guidingu przez ten sam port** co sterowanie osiami

#### Wady

- **Wymaga autoguidéra obsługującego LX200** — PHD2 obsługuje przez ASCOM
- **RJ12 ST4 nie jest LX200** — potrzebny konwerter ST4→LX200 lub dedykowany kabel
- **Większe opóźnienie** — polling szeregowy + parsowanie tekstu

---

## 6. Porównanie opcji

| Cecha | Opcja A: St4HAL (GPIO) | Opcja B1: Modbus DI | Opcja B2: USB-GPIO | Opcja B3: Konwerter ST4↔RS232 | Opcja B4: LX200 |
|-------|------------------------|--------------------|--------------------|------------------------------|-----------------|
| **Nowy kod** | Nowy HAL (~1500 LOC) | Modyfikacja SerialHAL (~300 LOC) | Nowy backend (~500 LOC) | Parser w SerialHAL (~200 LOC) | Rozszerzenie LX200 (~150 LOC) |
| **Dodatkowy sprzęt** | Brak (GPIO RPi) | Modbus DI (~200 PLN) | FTDI FT232H (~50 PLN) | Arduino+MAX232 (~25 PLN) | Brak (istniejący RS-485) |
| **Opóźnienie** | **~50µs** (przerwania) | ~3ms (polling 2ms + Modbus) | ~100µs (USB bitbang) | ~5ms (RS-232 + parse) | ~5-10ms (polling + parse) |
| **Platformy** | RPi (GPIO) | Wszystkie (RS-485) | Wszystkie (USB) | Wszystkie (RS-232) | Wszystkie (RS-485) |
| **Izolacja** | Wymaga zewnętrznej | Wbudowana w moduł | Zależna od dongle | Opcjonalna | Wymaga zewnętrznej |
| **Niezawodność** | Wysoka (bezpośrednie GPIO) | Średnia (polling) | Wysoka (FTDI sprawdzony) | Średnia (DIY) | Średnia (polling) |
| **Łatwość testowania** | Wysoka (mock GPIO) | Wysoka (symulacja Modbus) | Średnia (wymaga USB mock) | Wysoka (symulacja ASCII) | Wysoka (symulacja LX200) |
| **Wysiłek implementacji** | ~8-10 dni | ~3-4 dni | ~5-6 dni | ~3-4 dni | ~2-3 dni |

---

## 7. Rekomendowana architektura hybrydowa

Zamiast wybierać jedną opcję, rekomenduje się **architekturę z unified ST4 Input API**,
która pozwala na używanie różnych backendów (GPIO, Modbus, USB, Serial) przez wspólny interfejs.

### 7.1 Warstwa abstrakcji St4Input

Wzorowana na [`GamepadInput`](../../include/hal/gamepad_input.h:66):

```cpp
// include/hal/st4_input.h
namespace astro_mount {
namespace hal {

enum class St4Direction {
    NORTH, SOUTH, EAST, WEST, NONE
};

struct St4PulseEvent {
    St4Direction direction{St4Direction::NONE};
    int duration_ms{0};
    std::chrono::steady_clock::time_point timestamp;
};

/// Callback wywoływany przy każdym wykrytym impulsie ST4
using St4PulseCallback = std::function<void(const St4PulseEvent& event)>;

class St4Input {
public:
    virtual ~St4Input() = default;
    
    /// Inicjalizacja interfejsu ST4
    virtual bool initialize(const St4Config& config) = 0;
    virtual void shutdown() = 0;
    virtual bool isInitialized() const = 0;
    
    /// Uruchom/zatrzymaj monitorowanie
    virtual bool start(St4PulseCallback callback) = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    
    /// Diagnostyka
    virtual std::string getDeviceName() const = 0;
    virtual std::string getDiagnostics() const = 0;
};

} // namespace hal
} // namespace astro_mount
```

### 7.2 Implementacje backendów

```
St4Input (interface abstrakcyjny)
├── GpioSt4Input          — GPIO przez libgpiod (Raspberry Pi)
│   └── src/hal/st4_input_gpio.cpp
├── ModbusSt4Input        — Modbus RTU Digital Input Module
│   └── src/hal/st4_input_modbus.cpp   (wykorzystuje SerialHAL::sendModbusRequest)
├── SerialSt4Input        — Dedykowany konwerter ST4↔RS232 (ASCII)
│   └── src/hal/st4_input_serial.cpp   (wykorzystuje SerialHAL)
├── Lx200St4Input         — Rozszerzenie protokołu LX200
│   └── src/hal/st4_input_lx200.cpp    (wykorzystuje istniejący LX200 parser)
└── MockSt4Input          — Do testów
    └── src/hal/st4_input_mock.cpp
```

### 7.3 Fabryka St4InputFactory

```cpp
// src/hal/st4_input_factory.cpp
std::unique_ptr<St4Input> St4InputFactory::create(const St4Config& config) {
    switch (config.backend) {
        case St4Backend::GPIO:
            return std::make_unique<GpioSt4Input>(config);
        case St4Backend::MODBUS:
            return std::make_unique<ModbusSt4Input>(config, serial_port);
        case St4Backend::SERIAL_CONVERTER:
            return std::make_unique<SerialSt4Input>(config, serial_port);
        case St4Backend::LX200:
            return std::make_unique<Lx200St4Input>(config, lx200_interface);
        case St4Backend::MOCK:
            return std::make_unique<MockSt4Input>(config);
        default:
            throw std::runtime_error("Unknown ST4 backend");
    }
}
```

### 7.4 Konfiguracja

```json
{
  "hal": {
    "type": "st4",
    "st4": {
      "backend": "gpio",          // "gpio", "modbus", "serial", "lx200", "mock"
      "guide_rate_arcsec_per_sec": 7.5,
      "aggression": 0.8,
      "debounce_ms": 5,
      "min_pulse_ms": 8,
      "max_pulse_ms": 3000,
      
      // GPIO backend
      "gpio_pins": {
        "north": 17, "south": 27, "east": 22, "west": 23
      },
      
      // Modbus backend
      "modbus": {
        "port": "/dev/ttyUSB0",
        "baud_rate": 115200,
        "node_id": 1,
        "di_register": 0,
        "bit_north": 0,
        "bit_south": 1,
        "bit_east": 2,
        "bit_west": 3,
        "poll_interval_ms": 2
      },
      
      // Serial converter backend
      "serial": {
        "port": "/dev/ttyUSB1",
        "baud_rate": 115200,
        "protocol": "st4_ascii"
      }
    }
  }
}
```

### 7.5 Integracja z istniejącym kodem

```
MountController
  └── mount_controller.cpp:4429 ── applyGuiderCorrection()
                                    ↑
                                    │ (callback)
                                    │
  St4Input (abstrakcja) ────────────┘
    ├── GpioSt4Input          (libgpiod)
    ├── ModbusSt4Input        (SerialHAL + Modbus RTU)
    ├── SerialSt4Input        (SerialHAL + ASCII parser)
    ├── Lx200St4Input         (LX200Interface + extended protocol)
    └── MockSt4Input          (testy)
```

W [`MountController::Impl`](../../src/controllers/mount_controller.cpp):
```cpp
// Nowe pole
std::unique_ptr<hal::St4Input> st4_input_;

// Inicjalizacja
bool initSt4Guider(const St4Config& config) {
    st4_input_ = St4InputFactory::create(config);
    if (!st4_input_ || !st4_input_->initialize(config)) {
        return false;
    }
    
    // Rejestracja callback → applyGuiderCorrection
    st4_input_->start([this](const St4PulseEvent& event) {
        double ra = 0.0, dec = 0.0;
        double correction = config_.st4.guide_rate_arcsec_per_sec 
                          * event.duration_ms / 1000.0;
        
        switch (event.direction) {
            case St4Direction::NORTH: dec = +correction; break;
            case St4Direction::SOUTH: dec = -correction; break;
            case St4Direction::EAST:  ra  = +correction; break;
            case St4Direction::WEST:  ra  = -correction; break;
            default: return;
        }
        
        applyGuiderCorrection(ra, dec);
    });
    
    return true;
}
```

---

## 8. Zależności sprzętowe i biblioteczne

### 8.1 Dla poszczególnych backendów

| Backend | Biblioteka | Zależność |
|---------|-----------|-----------|
| GPIO (libgpiod) | `libgpiod-dev` (>= 2.0) | Nowa, wymaga instalacji |
| GPIO (sysfs) | Wbudowane w kernel | Deprecated, ale zero zależności |
| Modbus RTU | Wbudowane w SerialHAL | Zero nowych zależności |
| USB-GPIO (FTDI) | `libftdi1-dev` | Nowa, wymaga instalacji |
| Serial converter | Wbudowane w SerialHAL | Zero nowych zależności |
| LX200 | Wbudowane w LX200Interface | Zero nowych zależności |

### 8.2 Wymagania czasowe

| Backend | Rozdzielczość pomiaru | Maks. błąd |
|---------|----------------------|------------|
| GPIO (przerwania libgpiod) | ~50µs | ±100µs |
| GPIO (polling 1ms) | ~1ms | ±1ms |
| Modbus RTU (polling 2ms @ 115200) | ~2ms | ±3ms |
| Serial converter (przerwania HW) | ~100µs | ±500µs |
| LX200 (polling 10ms) | ~10ms | ±10ms |

### 8.3 Platformy docelowe

| Platforma | GPIO | Modbus RTU | USB-GPIO | Serial konwerter | LX200 |
|-----------|------|------------|----------|-----------------|-------|
| Raspberry Pi | ✅ GPIO 17,27,22,23 | ✅ RS-485 (GPIO 14/15) | ✅ USB | ✅ USB/UART | ✅ RS-232/485 |
| Orange Pi | ✅ GPIO (alternatywne) | ✅ RS-485 | ✅ USB | ✅ USB/UART | ✅ RS-232/485 |
| x86 PC | ❌ | ✅ USB↔RS-485 | ✅ USB | ✅ USB | ✅ USB↔RS-232 |
| ASUS Tinker Board | ✅ GPIO | ✅ RS-485 | ✅ USB | ✅ USB/UART | ✅ RS-232/485 |

---

## 9. Testowanie

### 9.1 Testy jednostkowe

```cpp
// tests/test_st4_input.cpp

// Mock St4Input
TEST(St4InputTest, MockPulseDetection) {
    St4Config config;
    config.backend = St4Backend::MOCK;
    
    auto mock = std::make_unique<MockSt4Input>(config);
    ASSERT_TRUE(mock->initialize(config));
    
    std::vector<St4PulseEvent> events;
    mock->start([&](const St4PulseEvent& e) { events.push_back(e); });
    
    mock->simulatePulse(St4Direction::NORTH, 200);
    mock->simulatePulse(St4Direction::EAST, 150);
    
    std::this_thread::sleep_for(50ms);
    
    ASSERT_EQ(events.size(), 2);
    EXPECT_EQ(events[0].direction, St4Direction::NORTH);
    EXPECT_EQ(events[0].duration_ms, 200);
    EXPECT_EQ(events[1].direction, St4Direction::EAST);
    EXPECT_EQ(events[1].duration_ms, 150);
}

// Integracja St4Input → applyGuiderCorrection
TEST(St4IntegrationTest, PulseToCorrection) {
    MountController controller;
    ControllerConfig cfg;
    cfg.enable_guider = true;
    cfg.st4 = {true, 7.5, 0.8, 5, 8, 3000};
    ASSERT_TRUE(controller.initialize(cfg));
    
    // Symuluj impuls ST4
    controller.injectSt4Pulse(St4Direction::NORTH, 200);
    
    auto status = controller.getStatus();
    // 7.5 arcsec/s * 0.2s * 0.8 aggression = 1.2 arcsec Dec
    EXPECT_NEAR(status.tracking_error_dec, 1.2, 0.01);
}

// SerialHAL z Modbus DI (symulowany)
TEST(St4ModbusTest, ModbusPulseDetection) {
    auto mock_port = std::make_shared<MockSerialPort>();
    SerialHAL hal(config);
    hal.setMockPort(mock_port);
    
    // Symuluj odpowiedź Modbus: tylko DI0 (North) = LOW (aktywny)
    mock_port->setResponse({0x01, 0x02, 0xFE, 0x00});  // FC02, 1 byte, DI0=0
    
    hal.initSt4Modbus(st4_modbus_config);
    hal.startSt4Monitor();
    
    std::this_thread::sleep_for(5ms);
    
    // Symuluj DI0 = HIGH (koniec impulsu)
    mock_port->setResponse({0x01, 0x02, 0xFF, 0x00});  // DI0=1
    
    std::this_thread::sleep_for(5ms);
    
    // Sprawdź czy wykryto impuls
    EXPECT_TRUE(hal.getLastSt4Event().has_value());
    EXPECT_EQ(hal.getLastSt4Event()->direction, St4Direction::NORTH);
}
```

### 9.2 Testy integracyjne

- **Test z symulacją sprzętu**: Pythonowy skrypt symulujący autoguider ST4 przez GPIO
  (RPi.GPIO) lub przez Modbus RTU (pymodbus)
- **Test przepływu end-to-end**: ST4 → St4Input → MountController → SimulatedHAL → weryfikacja pozycji
- **Test wydajnościowy**: Pomiar opóźnienia między impulsem ST4 a aplikacją korekcji

---

## 10. Podsumowanie i rekomendacje

### 10.1 Ocena złożoności (architektura hybrydowa)

| Komponent | Wysiłek | Ryzyko |
|-----------|---------|--------|
| `St4Input` abstrakcja | ~1 dzień | Niskie |
| `GpioSt4Input` (libgpiod) | ~1-2 dni | Średnie |
| `ModbusSt4Input` (SerialHAL) | ~1-2 dni | Niskie |
| `SerialSt4Input` (ASCII konwerter) | ~1 dzień | Niskie |
| `Lx200St4Input` (rozszerzenie LX200) | ~1 dzień | Niskie |
| `MockSt4Input` (testy) | ~0.5 dnia | Niskie |
| `St4InputFactory` | ~0.5 dnia | Niskie |
| Integracja z MountController | ~1 dzień | Niskie |
| Konfiguracja JSON + protobuf | ~0.5 dnia | Niskie |
| Testy | ~2 dni | Niskie |
| Dokumentacja | ~0.5 dnia | Niskie |
| **Razem** | **~10-12 dni** | **Niskie** |

### 10.2 Rekomendowana kolejność implementacji

1. **Faza 1** (dni 1-2): Warstwa abstrakcji [`St4Input`](../../include/hal/st4_input.h)
   i `MockSt4Input` — umożliwia testowanie niezależnie od sprzętu.

2. **Faza 2** (dni 3-4): Integracja z [`MountController`](../../src/controllers/mount_controller.cpp)
   — dodanie callbacku `St4PulseCallback → applyGuiderCorrection()`.

3. **Faza 3** (dni 5-6): `GpioSt4Input` przez `libgpiod` — główny backend docelowy (RPi).

4. **Faza 4** (dni 7-8): `ModbusSt4Input` przez istniejący [`SerialHAL`](../../src/hal/serial_hal/serial_hal.cpp)
   — alternatywa dla platform bez GPIO (x86) oraz dla użytkowników z istniejącą infrastrukturą RS-485.

5. **Faza 5** (dni 9-10): `SerialSt4Input` (ASCII konwerter) i `Lx200St4Input` — dodatkowe backendy.

6. **Faza 6** (dni 11-12): Testy, dokumentacja, przykłady konfiguracji.

### 10.3 Kluczowe zalety architektury hybrydowej

- **Jeden interfejs ST4** — `St4Input` obsługuje wszystkie backendy (GPIO, Modbus, Serial, LX200)
- **Wykorzystanie istniejącego kodu** — `ModbusSt4Input` i `SerialSt4Input` używają [`SerialHAL`](../../src/hal/serial_hal/serial_hal.cpp)
- **Bez zmian w istniejących HAL** — nowa warstwa `St4Input` jest komplementarna
- **Wybór backendu przez konfigurację** — użytkownik wybiera w JSON-ie
- **Testowalność** — `MockSt4Input` umożliwia pełne testy bez sprzętu
- **Rozszerzalność** — łatwo dodać nowe backendy (WiFi, Bluetooth, CANopen)

### 10.4 Potencjalne wyzwania

1. **Dokładność czasowa Modbus** — polling 2ms + czas odpowiedzi Modbus daje błąd ±3ms,
   co przy minimalnym impulsie 8ms daje ~37% błędu. Dla guidingu jest to akceptowalne,
   ale dla precyzyjnego (sub-arcsec) guidingu zalecany jest backend GPIO.

2. **Konflikt portów szeregowych** — jeśli RS-485 jest używany do komunikacji z napędami
   (LX200) i jednocześnie do ST4 (Modbus DI), potrzebny jest drugi port lub multipleksacja.

3. **Wymagania czasowe kernela Linux** — dla backendu GPIO z przerwaniami przez `libgpiod`,
   kernel Linux na RPi może mieć latencję do 100µs, co jest w pełni akceptowalne dla ST4
   (minimalny impuls 8ms). Dla backendu Modbus, opóźnienie kernela jest pomijalne przy pollingu 2ms.

4. **Brak GPIO na x86** — backendy `ModbusSt4Input` i `SerialSt4Input` rozwiązują ten problem
   przez RS-485/USB, dostępny na każdej platformie.
