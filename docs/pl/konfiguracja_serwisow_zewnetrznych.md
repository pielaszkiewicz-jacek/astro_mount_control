# Konfiguracja Serwisów Zewnętrznych

## Przegląd

Kontroler montażu obsługuje podsystemy pomocnicze włączane przez sekcję `external_services`:

- **Kopuła, derotator, focuser, ST4 guider i PEC** są hostowane **w procesie** wewnątrz `astro_mount_controller` — nie wymagają osobnego procesu. Pole `address` to adres nasłuchu gRPC wystawiony klientom.
- **Pogoda, zasilanie i sekwencer** działają jako samodzielne procesy (`astro_weather_server`, `astro_power_server`, `astro_sequencer_server`) komunikujące się przez gRPC.

| Serwis | Hosting | Domyślny port | Przeznaczenie |
|--------|---------|--------------|---------------|
| **Kopuła** | w procesie (`astro_mount_controller`) | 50051 (wspólny) | Sterowanie obrotem i przesłoną kopuły, auto-synchronizacja z montażem |
| **Derotator** | w procesie (`astro_mount_controller`) | 50051 (wspólny) | Kompensacja pola rotacji |
| **Focuser** | w procesie (`astro_mount_controller`) | 50051 (wspólny) | Sterowanie focuserem i autofokus |
| **ST4 guider** | w procesie (`astro_mount_controller`) | 50051 (wspólny) | Kalibracja i impulsy ST4 |
| **PEC** | w procesie (`astro_mount_controller`) | 50051 (wspólny) | Korekcja błędu okresowego |
| **Pogoda** | samodzielny (`astro_weather_server`) | 50055 | Monitoring pogody, alerty, automatyczne parkowanie |
| **Zasilanie** | samodzielny (`astro_power_server`) | 50056 | Monitoring baterii/zasilania, parkowanie przy niskim napięciu |
| **Sekwencer** | samodzielny (`astro_sequencer_server`) | 50057 | Plan obserwacji i sekwencjonowanie |

Domyślne włączenia są zsynchronizowane z web proxy: **derotator, focuser, ST4 guider i PEC — włączone** (w procesie); **kopuła, pogoda, zasilanie i sekwencer — wyłączone**. Włączanie/wyłączanie w pliku konfiguracyjnym kontrolera montażu (`config/default.json`) w sekcji `external_services`.

---

## 1. Integracja z Kontrolerem Montażu

Integrację konfiguruje się w pliku JSON kontrolera montażu pod kluczem `external_services`. Podsystemy **kopuły, derotatora, focusera, ST4 guidera i PEC** są serwowane w procesie na **wspólnym porcie gRPC 50051** (nie potrzebują osobnego adresu). Dla **pogody, zasilania i sekwencera** (procesy samodzielne) `address` to adres, z którym łączy się kontroler montażu:

```json
{
  "external_services": {
    "dome": {
      "enabled": false,
      "update_interval_ms": 1000
    },
    "derotator": {
      "enabled": true,
      "update_interval_ms": 1000
    },
    "focuser": {
      "enabled": true,
      "poll_interval_ms": 5000
    },
    "st4_guider": {
      "enabled": true
    },
    "pec": {
      "enabled": true
    },
    "weather": {
      "enabled": false,
      "address": "127.0.0.1:50055",
      "poll_interval_ms": 10000
    },
    "power": {
      "enabled": false,
      "address": "127.0.0.1:50056",
      "poll_interval_ms": 10000
    },
    "sequencer": {
      "enabled": false,
      "address": "127.0.0.1:50057",
      "poll_interval_ms": 5000
    }
  }
}
```

### Opis parametrów

| Parametr | Typ | Domyślnie | Opis |
|-----------|------|---------|-------------|
| `enabled` | bool | `false` | Włącza tę integrację (podsystem w procesie lub klient zewnętrzny) |
| `address` | string | `127.0.0.1:500xx` | Serwisy w procesie: adres nasłuchu gRPC wystawiony klientom. Pogoda/zasilanie/sekwencer: adres gRPC zewnętrznego procesu |
| `update_interval_ms` | int | `1000` | Jak często przekazywać pozycję montażu do kopuły/derotatora (w procesie) |
| `poll_interval_ms` | int | `10000` | Jak często odpytować status (pogoda, zasilanie, focuser, sekwencer) |

---

## 2. Serwis Pogodowy (`astro_weather_server`)

### 2.1 Plik konfiguracyjny: `config/weather_config.json`

```json
{
  "rules": {
    "min_temperature_c": -20.0,
    "max_temperature_c": 50.0,
    "max_wind_speed_ms": 15.0,
    "max_wind_gust_ms": 20.0,
    "auto_park_on_rain": true,
    "max_rain_rate_mmh": 5.0,
    "max_cloud_cover_percent": 90.0,
    "max_humidity_percent": 95.0,
    "dew_point_tolerance_c": 3.0,
    "auto_park_enabled": true,
    "check_interval_seconds": 10,
    "debounce_seconds": 30,
    "notify_on_alert": true,
    "close_dome_on_danger": false
  },
  "sensors": {
    "rain": { "enabled": false, "gpio_pin": -1 },
    "wind": { "enabled": false, "gpio_pin": -1, "calibration_factor": 1.0 },
    "cloud": { "enabled": false, "device": "/dev/i2c-1", "address": 90 }
  },
  "gps": { "enabled": false },
  "api_source": {
    "provider": "",
    "api_key": "",
    "latitude": 0.0,
    "longitude": 0.0,
    "station_id": ""
  }
}
```

### 2.2 Reguły bezpieczeństwa

| Parametr | Typ | Domyślnie | Opis |
|-----------|------|---------|-------------|
| `min_temperature_c` | double | -20.0 | Minimalna bezpieczna temperatura [°C] |
| `max_temperature_c` | double | 50.0 | Maksymalna bezpieczna temperatura [°C] |
| `max_wind_speed_ms` | double | 15.0 | Maks. bezpieczny wiatr ciągły [m/s] (~54 km/h) |
| `max_wind_gust_ms` | double | 20.0 | Maks. bezpieczny poryw wiatru [m/s] (~72 km/h) |
| `auto_park_on_rain` | bool | true | Parkuj montaż gdy wykryty deszcz |
| `max_rain_rate_mmh` | double | 5.0 | Maks. natężenie deszczu przed parkowaniem [mm/h] |
| `max_cloud_cover_percent` | double | 90.0 | Maks. zachmurzenie do obserwacji [%] |
| `max_humidity_percent` | double | 95.0 | Maks. bezpieczna wilgotność [%] |
| `dew_point_tolerance_c` | double | 3.0 | Wymagany margines powyżej punktu rosy [°C] |
| `auto_park_enabled` | bool | true | Włącz automatyczne parkowanie przy alertach |
| `check_interval_seconds` | int | 10 | Interwał odczytu czujników [s] |
| `debounce_seconds` | int | 30 | Czas oczekiwania przed wyzwoleniem alertu [s] |
| `notify_on_alert` | bool | true | Wysyłaj powiadomienia przy alertach |
| `close_dome_on_danger` | bool | false | Auto-zamykanie kopuły (wymaga serwisu kopuły) |

### 2.3 Czujniki

| Czujnik | Pola | Opis |
|---------|------|-------------|
| **Deszcz** | `gpio_pin` | Pin GPIO dla czujnika deszczu (np. 17 dla BCM) |
| **Wiatr** | `gpio_pin`, `calibration_factor` | Pin GPIO + współczynnik kalibracji |
| **Chmury** | `device`, `address` | Urządzenie I²C (np. `/dev/i2c-1`) i adres czujnika (np. 0x5A) |

### 2.4 Źródła API

| Dostawca | Pola | Opis |
|----------|--------|-------------|
| `openweathermap` | `api_key`, `latitude`, `longitude` | OpenWeatherMap One Call API 3.0 |
| `weathergov` | `latitude`, `longitude`, `station_id` | US National Weather Service API |
| `imgw` | `station_id` | Polski Instytut Meteorologii i Gospodarki Wodnej |

### 2.5 Uruchamianie

```bash
# Uruchomienie z domyślną konfiguracją
./build/bin/astro_weather_server

# Niestandardowy adres i konfiguracja
./build/bin/astro_weather_server \
    --address 0.0.0.0:50055 \
    --config config/weather_config.json

# Włączenie SSL
./build/bin/astro_weather_server \
    --address 0.0.0.0:50055 \
    --config config/weather_config.json \
    --ssl /path/to/cert.pem /path/to/key.pem

# Systemd
sudo systemctl enable astro-weather-server
sudo systemctl start astro-weather-server
```

### 2.6 Zachowanie Integracji

Gdy włączony (`weather.enabled: true`), kontroler montażu:
1. **Tworzy `WeatherClient`** który odpytuje serwis pogodowy co `poll_interval_ms` milisekund
2. **Przy ostrzeżeniu pogodowym** — wysyła powiadomienie `WARNING` przez silnik powiadomień
3. **Przy zagrożeniu** — wysyła `ERROR`
4. **Przy niebezpieczeństwie** — wysyła `CRITICAL` i **automatycznie parkuje montaż**
5. **Przy poprawie pogody** — wysyła `INFO` all-clear
6. **Przy utracie łączności** — wysyła `WARNING`

---

## 3. Serwis Zasilania (`astro_power_server`)

### 3.1 Plik konfiguracyjny: `config/power_config.json`

```json
{
  "hal_type": "simulated",
  "low_voltage_threshold": 11.5,
  "poll_interval_s": 10
}
```

### 3.2 Opis parametrów

| Parametr | Typ | Domyślnie | Opis |
|-----------|------|---------|-------------|
| `hal_type` | string | `"simulated"` | Backend HAL zasilania: `simulated` (rozszerzalny o `i2c`, `serial`) |
| `low_voltage_threshold` | double | 11.5 | Próg napięcia dla alarmu niskiej baterii [V] |
| `poll_interval_s` | int | 10 | Interwał odczytu czujników [s] |

### 3.3 Uruchamianie

```bash
# Uruchomienie z domyślną konfiguracją (symulowane dane)
./build/bin/astro_power_server

# Niestandardowy adres i konfiguracja
./build/bin/astro_power_server \
    --address 0.0.0.0:50056 \
    --config config/power_config.json

# Systemd
sudo systemctl enable astro-power-server
sudo systemctl start astro-power-server
```

### 3.4 Zachowanie Integracji

Gdy włączony (`power.enabled: true`), kontroler montażu:
1. **Tworzy `PowerService::Stub`** który odpytuje serwis zasilania co `poll_interval_ms`
2. **Przy starcie** — loguje początkowy status (napięcie, prąd, poziom naładowania)
3. **Na baterii** — loguje ostrzeżenie z szacowanym czasem pracy
4. **Przy charge < 20%** — **automatycznie parkuje montaż**

### 3.5 Pola statusu (gRPC `PowerStatus`)

| Pole | Typ | Opis |
|------|------|-------------|
| `voltage_v` | double | Aktualne napięcie baterii [V] |
| `current_a` | double | Pobór prądu [A] |
| `power_w` | double | Pobór mocy [W] |
| `capacity_ah` | double | Pojemność baterii [Ah] |
| `charge_percent` | double | Poziom naładowania [%] |
| `charging` | bool | Bateria jest ładowana |
| `on_battery` | bool | Praca na baterii (brak zasilania zewnętrznego) |
| `temperature_c` | double | Temperatura baterii [°C] |
| `estimated_runtime_min` | double | Szacowany pozostały czas pracy [min] |
| `input_voltage_v` | double | Napięcie wejściowe/zasilacza [V] |
| `output_voltage_v` | double | Regulowane napięcie wyjściowe [V] |
| `outputs[]` | repeated | Status per-wyjście (id, nazwa, włączone, napięcie, prąd, przeciążenie) |

---

## 4. Kompletny Przykład

### 4.1 Włączenie Wszystkich Serwisów

W `config/default.json`:

```json
{
  "external_services": {
    "dome": {
      "enabled": true,
      "address": "127.0.0.1:50051",
      "update_interval_ms": 1000
    },
    "derotator": {
      "enabled": true,
      "address": "127.0.0.1:50051",
      "update_interval_ms": 1000
    },
    "focuser": {
      "enabled": true,
      "address": "127.0.0.1:50051",
      "poll_interval_ms": 5000
    },
    "weather": {
      "enabled": true,
      "address": "127.0.0.1:50055",
      "poll_interval_ms": 10000
    },
    "power": {
      "enabled": true,
      "address": "127.0.0.1:50056",
      "poll_interval_ms": 10000
    }
  }
}
```

### 4.2 Sekwencja Uruchamiania

```bash
# Uruchom zewnętrzne (samodzielne) serwisy pogody i zasilania
./build/bin/astro_weather_server --config config/weather_config.json &
./build/bin/astro_power_server --config config/power_config.json &

# Uruchom kontroler montażu.
# Kopuła, derotator i focuser są hostowane W PROCESIE i serwowane na
# wspólnym porcie gRPC 50051 — nie są potrzebne osobne procesy ani porty.
./build/bin/astro_mount_controller config/default.json
```

### 4.3 Weryfikacja

```bash
# Sprawdź czy serwisy nasłuchują (wszystko na wspólnym porcie 50051 + pogoda/zasilanie)
ss -tlnp | grep -E '50051|5005[5-7]'

# Test serwisu kopuły (w procesie, wspólny port 50051)
grpcurl -plaintext 127.0.0.1:50051 astro_dome.DomeService/GetStatus

# Test serwisu derotatora (w procesie, wspólny port 50051)
grpcurl -plaintext 127.0.0.1:50051 astro_derotator.DerotatorService/GetStatus

# Test serwisu focusera (w procesie, wspólny port 50051)
grpcurl -plaintext 127.0.0.1:50051 astro_mount.FocuserService/GetFocuserPosition

# Test serwisu pogodowego
grpcurl -plaintext 127.0.0.1:50055 astro_mount.WeatherService/GetWeatherStatus

# Test serwisu zasilania
grpcurl -plaintext 127.0.0.1:50056 astro_mount.PowerService/GetPowerStatus

# Sprawdź logi kontrolera
journalctl -u astro-mount-controller | grep -i "weather\|power\|dome\|derotator\|focuser"
```

---

## 5. Diagram Architektury

```
┌─────────────────────────────────────────────────────────┐
│  Proces Kontrolera Montażu (main.cpp)                   │
│                                                         │
│  ┌──────────────┐   ┌────────────────────────────────┐  │
│  │ MountController│   │ Klienty Integracji             │  │
│  │ Maszyna Stanów  │   │                                │  │
│  │ Pętla Trackingu │   │ WeatherClient ──gRPC──► :50055 │  │
│  │ Parkowanie     │   │ PowerStub     ──gRPC──► :50056 │  │
│  └──────────────┘   │ Dome/Derotator/Focuser          │  │
│                      │  (w procesie, serwowane na :50051) │  │
│                      └────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
                              │
                    gRPC      │
                              ▼
              ┌──────────────────────────┐
              │  astro_weather_server    │
              │  :50055                  │
              │  ┌────────────────────┐   │
              │  │ WeatherMonitor     │   │
              │  │ ├─ RainSensor      │   │
              │  │ ├─ WindSensor      │   │
              │  │ ├─ CloudSensor     │   │
              │  │ └─ WeatherApiSrc   │   │
              │  └────────────────────┘   │
              └──────────────────────────┘

              ┌──────────────────────────┐
              │  astro_power_server      │
              │  :50056                  │
              │  ┌────────────────────┐   │
              │  │ PowerManager       │   │
              │  │ └─ PowerControl    │   │
              │  │    (simulated/i2c) │   │
              │  └────────────────────┘   │
              └──────────────────────────┘
```

---

## 6. Integracja z Systemem Powiadomień

Zdarzenia pogodowe i zasilania wyzwalają powiadomienia przez `NotificationEngine`:

| Zdarzenie | Poziom | Kategoria | Auto-akcja |
|-----------|--------|-----------|-------------|
| Ostrzeżenie pogodowe | `WARNING` | `WEATHER` | Log |
| Zagrożenie pogodowe | `ERROR` | `WEATHER` | Log |
| Niebezpieczeństwo pogodowe | `CRITICAL` | `WEATHER` | **Parkuj montaż** |
| Poprawa pogody | `INFO` | `WEATHER` | Log |
| Serwis pogodowy offline | `WARNING` | `WEATHER` | Log |
| Niski poziom baterii (<20%) | `CRITICAL` | `POWER` | **Parkuj montaż** |
| Praca na baterii | `WARNING` | `POWER` | Log |

---

## 7. Referencje do kodu źródłowego

| Komponent | Pliki |
|-----------|-------|
| Struktura konfiguracji integracji | [`include/config/configuration.h:220`](include/config/configuration.h:220) — `ExternalIntegrationConfig` |
| Parsowanie JSON konfiguracji | [`src/config/configuration.cpp:734`](src/config/configuration.cpp:734) — `getExternalIntegrationConfig()` |
| Serwer pogodowy | [`weather/src/main.cpp`](weather/src/main.cpp), [`weather/src/weather_server.cpp`](weather/src/weather_server.cpp), [`weather/src/weather_service_impl.cpp`](weather/src/weather_service_impl.cpp) |
| Klient pogodowy | [`include/controllers/weather_client.h`](include/controllers/weather_client.h), [`src/controllers/weather_client.cpp`](src/controllers/weather_client.cpp) |
| Fabryka pogodowa | [`weather/src/weather_factory.cpp`](weather/src/weather_factory.cpp) |
| Serwer zasilania | [`power/src/main.cpp`](power/src/main.cpp), [`power/src/power_server.cpp`](power/src/power_server.cpp), [`power/src/power_service_impl.cpp`](power/src/power_service_impl.cpp) |
| Integracja w kontrolerze | [`src/main.cpp:282`](src/main.cpp:282) — konfiguracja pogody, [`src/main.cpp:306`](src/main.cpp:306) — konfiguracja zasilania |
| Pętla główna - polling | [`src/main.cpp:420`](src/main.cpp:420) — sprawdzanie statusu zasilania |
| Cel budowania | [`CMakeLists.txt:482`](CMakeLists.txt:482) — `astro_weather_server`, [`CMakeLists.txt:499`](CMakeLists.txt:499) — `astro_power_server` |
| Systemd | [`scripts/astro-weather-server.service`](scripts/astro-weather-server.service), [`scripts/astro-power-server.service`](scripts/astro-power-server.service) |
