# Integracja serwonapędów RS-485/LX200

## Raport projektowy — alternatywa dla CANopen

**Data:** 2026-07-04  
**Status:** Projekt / Raport  
**Pliki referencyjne:** [`docs/pl/alternatywy_dla_canopen.md`](docs/pl/alternatywy_dla_canopen.md), [`include/controllers/icanopen_interface.h`](include/controllers/icanopen_interface.h), [`src/controllers/canopen_factory.cpp`](src/controllers/canopen_factory.cpp)

---

## 1. Architektura — punkt integracji

System używa abstrakcyjnego interfejsu [`ICanOpenInterface`](include/controllers/icanopen_interface.h) do komunikacji z napędami. Wszystkie operacje (pozycjonowanie, prędkość, odczyt stanu) przechodzą przez ten interfejs. Fabryka [`CanOpenFactory`](src/controllers/canopen_factory.cpp) tworzy konkretną implementację na podstawie nazwy biblioteki (`"mock"`, `"canopensocket"`, `"canfestival"` itd.).

```
MountController::Impl
  └── std::shared_ptr<ICanOpenInterface> canopen_interface_
        ├── CanOpenInterface  (CANopen/CiA 402 przez SocketCAN)
        ├── TestCanOpenService (mock — testy)
        └── LX200Interface     (NOWY — RS-485 z protokołem LX200)
```

**Strategia:** Dodać `LX200Interface` jako kolejną implementację `ICanOpenInterface`, rejestrowaną w fabryce pod nazwą `"lx200"`. Nie modyfikujemy `MountController` — nowy interfejs jest przezroczysty dla warstwy sterowania.

---

## 2. Protokół komunikacyjny

### 2.1 Warstwa fizyczna

| Parametr | Wartość |
|----------|---------|
| Medium | RS-485 (half-duplex, różnicowy) |
| Topologia | Multidrop — do 32 urządzeń na magistrali |
| Prędkość | 115200–921600 bps (domyślnie 115200) |
| Format | 8N1 (8 bitów danych, bez parzystości, 1 bit stopu) |
| Adresowanie | Prefix adresu w ramce (np. `:A1...`) |

### 2.2 Ramki — rozszerzony protokół LX200

Standardowy LX200 nie ma adresowania (jest point-to-point RS-232). Rozszerzamy go o prefix adresu osi i dodatkowe komendy dla serwonapędów:

```
Format ramki:
  : A <axis_id> <CMD> [parametry] # <CR>
  
  :      — znak startu (zgodny z LX200)
  A      — prefix adresowania (A = Axis)
  <id>   — identyfikator osi: 0 = HA/Az, 1 = Dec/Alt
  <CMD>  — dwuliterowa komenda (zgodna z LX200 gdzie to możliwe)
  #      — znak końca (zgodny z LX200)
```

### 2.3 Komendy

#### Sterowanie pozycją (tryb Profile Position)

| Komenda | Opis | Odpowiedź |
|---------|------|-----------|
| `:A0Sr+123.4567#` | Set RA/HA target dla osi 0 | `1` (OK) |
| `:A1Sd+45.2803#` | Set Dec target dla osi 1 | `1` |
| `:A0MS#` | Slew do targetu (oś 0) | `0` (w trakcie) → `1` (osiągnięty) |
| `:A0Q#` | Stop osi 0 | `1` |

#### Sterowanie prędkością (tryb Profile Velocity)

| Komenda | Opis | Odpowiedź |
|---------|------|-----------|
| `:A0Rv+1.5041#` | Set velocity (deg/s) dla osi 0 | `1` |
| `:A1Rv+0.0000#` | Set velocity dla osi 1 (0 = stop) | `1` |
| `:A0Ra+2.256#` | Set acceleration (deg/s²) dla osi 0 | `1` |

#### Odczyt statusu

| Komenda | Opis | Odpowiedź |
|---------|------|-----------|
| `:A0GP#` | Get Position (deg) | `+123.4567#` |
| `:A0GV#` | Get Velocity (deg/s) | `+1.5041#` |
| `:A0GS#` | Get Status (hex bitmask) | `0x0037#` |
| `:A0GT#` | Get Temperature (°C) | `+42.5#` |
| `:A0GC#` | Get Current (A) | `+1.23#` |

#### Konfiguracja

| Komenda | Opis | Odpowiedź |
|---------|------|-----------|
| `:A0EN#` | Enable drive | `1` |
| `:A0ED#` | Disable drive | `1` |
| `:A0SP+123.4567#` | Set Actual Position (home) | `1` |
| `:A0CE#` | Clear Errors | `1` |

#### Status word (bity w `:A0GS#`)

| Bit | Znaczenie |
|-----|-----------|
| 0 | Drive enabled |
| 1 | Drive moving |
| 2 | Target reached |
| 3 | Warning active |
| 4 | Error active |
| 5 | Limit switch active |
| 6 | Homed |

---

## 3. Implementacja — `LX200Interface`

### 3.1 Pliki

| Plik | Rola |
|------|------|
| [`include/controllers/lx200_interface.h`](include/controllers/lx200_interface.h) | Deklaracja klasy `LX200Interface` |
| [`src/controllers/lx200_interface.cpp`](src/controllers/lx200_interface.cpp) | Implementacja — RS-485 + parser LX200 |

### 3.2 Struktura klasy

```cpp
class LX200Interface : public ICanOpenInterface {
public:
    // Konfiguracja specyficzna dla LX200
    struct LX200Config {
        std::string port;           // np. "/dev/ttyUSB0"
        int baud_rate{115200};      // 115200, 230400, 460800, 921600
        int timeout_ms{500};        // timeout odpowiedzi
        int axis_addrs[2]{0, 1};    // adresy RS-485 dla osi 0 i 1
    };

    bool initialize(const Config& config) override;
    // ... wszystkie metody ICanOpenInterface ...
    
private:
    // Port szeregowy
    int fd_{-1};
    LX200Config lx200_config_;
    
    // Stan osi
    struct AxisState {
        double position{0.0};
        double velocity{0.0};
        double target{0.0};
        bool enabled{false};
        bool moving{false};
        bool target_reached{true};
    };
    AxisState axis_[2];
    mutable std::mutex mutex_;
    
    // Komunikacja
    bool sendCommand(int axis, const std::string& cmd);
    std::string readResponse(int axis, int timeout_ms);
    bool openPort();
    void closePort();
};
```

### 3.3 Mapowanie `ICanOpenInterface` → LX200

| Metoda ICanOpenInterface | Mapowanie na LX200 |
|--------------------------|---------------------|
| `initialize(Config)` | Otwiera port RS-485, konfiguruje `termios` |
| `connect()` | Wysyła `:A0EN#` i `:A1EN#` |
| `disconnect()` | Wysyła `:A0ED#` i `:A1ED#`, zamyka port |
| `setPositionTarget(axis, pos, vel, acc)` | `:A{axis}Sr{pos}#` + `:A{axis}Ra{acc}#` + `:A{axis}MS#` |
| `setVelocityTarget(axis, vel, acc)` | `:A{axis}Rv{vel}#` + `:A{axis}Ra{acc}#` |
| `getPositionData(axis)` | `:A{axis}GP#` |
| `getDriveStatus(axis)` | `:A{axis}GS#` — parsuje status word |
| `setActualPosition(axis, pos)` | `:A{axis}SP{pos}#` |
| `stopAxis(axis)` | `:A{axis}Q#` |
| `emergencyStop(axis)` | `:A{axis}ED#` |
| `enableDrive(axis)` | `:A{axis}EN#` |
| `disableDrive(axis)` | `:A{axis}ED#` |

### 3.4 Pseudo-implementacja — pętla komunikacji

```cpp
bool LX200Interface::setPositionTarget(int axis_id, double position, 
                                        double velocity, double acceleration) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 1. Ustaw akcelerację
    char accel_cmd[32];
    snprintf(accel_cmd, sizeof(accel_cmd), ":A%dRa%.4f#", axis_id, acceleration);
    if (!sendCommand(axis_id, accel_cmd)) return false;
    
    // 2. Ustaw pozycję docelową
    char pos_cmd[32];
    snprintf(pos_cmd, sizeof(pos_cmd), ":A%dSr%.4f#", axis_id, position);
    if (!sendCommand(axis_id, pos_cmd)) return false;
    
    // 3. Rozpocznij slew
    char slew_cmd[16];
    snprintf(slew_cmd, sizeof(slew_cmd), ":A%dMS#", axis_id);
    if (!sendCommand(axis_id, slew_cmd)) return false;
    
    axis_[axis_id].target = position;
    axis_[axis_id].moving = true;
    return true;
}

PositionData LX200Interface::getPositionData(int axis_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Wyślij zapytanie o pozycję
    char cmd[16];
    snprintf(cmd, sizeof(cmd), ":A%dGP#", axis_id);
    std::string response = sendCommandReadResponse(axis_id, cmd);
    
    // Parsuj: "+123.4567#"
    PositionData data{};
    if (!response.empty() && response[0] == '+' || response[0] == '-') {
        data.actual_position = std::stod(response);
    }
    data.timestamp = std::chrono::system_clock::now();
    return data;
}
```

---

## 4. Integracja z fabryką

W [`canopen_factory.cpp`](src/controllers/canopen_factory.cpp) dodać gałąź `"lx200"`:

```cpp
else if (lib == "lx200") {
    return std::make_unique<LX200Interface>();
}
```

Oraz zarejestrować w `getSupportedLibraries()`:

```cpp
libraries.push_back("lx200");
```

---

## 5. Konfiguracja

### 5.1 JSON (`config/default.json`)

```json
"lx200": {
    "enabled": false,
    "port": "/dev/ttyUSB0",
    "baud_rate": 115200,
    "timeout_ms": 500,
    "axis_addrs": [0, 1]
}
```

### 5.2 Proto (`proto/mount_controller.proto`)

```protobuf
message LX200Config {
    optional bool enabled = 1;
    string port = 2;
    int32 baud_rate = 3;
    int32 timeout_ms = 4;
    repeated int32 axis_addrs = 5;
}
```

Dodać pole w `Configuration`:
```protobuf
optional LX200Config lx200_config = 87;
```

### 5.3 Web UI

Nowa grupa w [`settings.js`](web/public/js/components/settings.js) — "LX200 (RS-485)" z polami:
- Checkbox: Enable LX200
- Text: Serial Port
- Select: Baud Rate (115200, 230400, 460800, 921600)
- Number: Timeout (ms)

---

## 6. Porównanie: CANopen vs RS-485/LX200

| Cecha | CANopen (CiA 402) | RS-485/LX200 |
|-------|-------------------|--------------|
| **Przepustowość** | 1 Mbps (CAN) | Do 10 Mbps (RS-485), typ. 115200 |
| **Determinizm** | Wysoki (arbitraż CAN, PDO, SYNC) | Średni (polling, half-duplex) |
| **Liczba osi** | Do 127 na magistrali | Do 32 na magistrali RS-485 |
| **Konfiguracja** | SDO (odczyt/zapis słownika obiektów) | Własne komendy tekstowe |
| **Monitoring** | Heartbeat, Node Guarding, Emergency | Polling statusu |
| **Bezpieczeństwo** | Emergency stop, limit switches (CiA 402) | Limit switches (własna implementacja) |
| **Koszt sprzętu** | Średni (~200-500 PLN/napęd) | **Bardzo niski** (~50-150 PLN/napęd) |
| **Kable** | 2 żyły (CAN_H, CAN_L) + zasilanie | 2 żyły (A, B) + zasilanie |
| **Długość kabla** | Do 40m @ 1 Mbps | **Do 1200m** @ 115200 |
| **Dojrzałość w astronomii** | ⭐⭐ (przemysł) | ⭐⭐⭐⭐⭐ (Meade, Celestron, OnStep) |
| **Zakłócenia EMI** | Odporny (różnicowy) | Bardzo odporny (różnicowy, izolowany) |
| **Debugowanie** | Trudne (analizator CAN) | **Łatwe** (terminal szeregowy, czytelny tekst) |

---

## 7. Ograniczenia i znane problemy

### 7.1 Brak PDO
CANopen używa PDO (Process Data Objects) do cyklicznej transmisji pozycji/prędkości bez narzutu ramki. W LX200 każdy odczyt wymaga osobnego zapytania i odpowiedzi. Przy 115200 bps i ~20 bajtach na transakcję, maksymalna częstotliwość odpytywania to ~300 Hz na oś. W praktyce wystarcza to do śledzenia z aktualizacją co 20 ms (50 Hz) — limitem jest latency portu szeregowego, nie przepustowość.

### 7.2 Half-duplex
RS-485 jest half-duplex — w danym momencie tylko jedno urządzenie nadaje. Master (kontroler) musi czekać na odpowiedź przed wysłaniem kolejnego polecenia. Przy 2 osiach i timeout 500ms, pełne odpytywanie zajmuje ~20 ms (zakładając 5 ms na komendę + odpowiedź).

### 7.3 Brak heartbeat
CANopen automatycznie monitoruje stan węzłów przez heartbeat. W LX200 musimy polegać na timeoutach odpowiedzi — brak odpowiedzi w zadanym czasie oznacza awarię osi.

### 7.4 Brak synchronizacji
CANopen używa SYNC do synchronizacji odczytu pozycji wielu osi w tym samym momencie. W LX200 każda oś jest odpytywana sekwencyjnie — może wystąpić kilkumilisekundowy skew między odczytami.

---

## 8. Rekomendowana ścieżka implementacji

| Krok | Czas | Opis |
|------|------|------|
| 1 | 1-2h | Utworzenie `include/controllers/lx200_interface.h` — deklaracja klasy |
| 2 | 3-4h | Implementacja `src/controllers/lx200_interface.cpp` — port szeregowy + parser LX200 |
| 3 | 1h | Dodanie `"lx200"` do `CanOpenFactory` i `getSupportedLibraries()` |
| 4 | 1h | Konfiguracja: JSON + proto + Web UI |
| 5 | 1h | Testy: `test_lx200_interface.cpp` z mockowanym portem szeregowym |
| 6 | 1h | Integracja z `mount_controller.cpp` — testy end-to-end z symulowanym napędem |

---

## 9. Przykład konfiguracji — dwuosiowy montaż EQ przez RS-485

```json
{
    "canopen": {
        "interface_name": "can0",
        "node_id": 1
    },
    "lx200": {
        "enabled": true,
        "port": "/dev/ttyRS485",
        "baud_rate": 460800,
        "timeout_ms": 200,
        "axis_addrs": [10, 11]
    },
    "mount": {
        "type": "equatorial",
        "canopen_interface": "lx200"
    }
}
```

Klucz `"canopen_interface"` w sekcji `mount` wskazuje którą bibliotekę fabryki użyć. Domyślnie `"canopensocket"`, po zmianie na `"lx200"` system użyje RS-485.

---

## 10. Wnioski

**RS-485/LX200 jest realną, tańszą alternatywą dla CANopen** w zastosowaniach astronomicznych gdzie:

- Koszt sprzętu jest priorytetem (napędy krokowe z RS-485 są 5-10× tańsze niż serwonapędy CANopen)
- Nie jest wymagana synchronizacja submilisekundowa między osiami
- Wystarcza przepustowość ~57600-115200 bps (co jest wystarczające dla śledzenia z aktualizacją 50 Hz)
- Debugowalność i prostota protokołu tekstowego są atutem

Integracja przez istniejący interfejs `ICanOpenInterface` zapewnia pełną przezroczystość dla warstwy sterowania — żadna linia kodu w `MountController` nie wymaga zmiany.
