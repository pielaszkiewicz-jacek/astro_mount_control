# Raport — Faza 4: Prawdziwe dane pogody i powiadomień (P8, P9)

**Data:** 2026-08-14
**Cel:** wykonanie Fazy 4 z [`kompleksowa_analiza_projektu_2026-08-14.md`](kompleksowa_analiza_projektu_2026-08-14.md):
1. **P8** — implementacja `httpGet` przez libcurl (CMake: `find_package(CURL)`) + parsowanie JSON dla openweathermap/weathergov/imgw; czujniki GPIO (wiatromierz/deszczomierz) — uczciwe oznaczenie „brak czujnika” zamiast 0.0/false.
2. **P9** — kanały powiadomień: email (SMTP/libcurl), webhook (libcurl), MQTT.

Środowisko: libcurl 8.5.0 (dev) dostępny; **libgpiod i libmosquitto/paho niedostępne** → GPIO przez sysfs (uczciwe „brak czujnika”), MQTT przez minimalny klient MQTT 3.1.1 na surowym gnieździe TCP.

---

## 1. Wspólny klient HTTP/SMTP (libcurl)

| Plik | Opis |
|------|------|
| [`include/http_client.h`](../../include/http_client.h) | `astro_mount::http::get()`, `post()`, `smtpSend()` |
| [`src/http_client.cpp`](../../src/http_client.cpp) | Implementacja libcurl: GET/POST (timeout, User-Agent, nagłówki, retry po stronie wywołującego), SMTP (STARTTLS `CURLUSESSL_TRY`, auth) |
| [`CMakeLists.txt`](../../CMakeLists.txt) | `find_package(CURL REQUIRED)` + `CURL::libcurl` linkowane do `astro_mount_core`; `src/http_client.cpp` w `CORE_SOURCES` |

## 2. Źródła pogody (P8)

| Plik | Zmiana |
|------|--------|
| [`openweathermap_source.cpp`](../../src/weather/sources/openweathermap_source.cpp) | `httpGet` → `http::get`; `parseResponse` → pełne parsowanie One Call API 3.0 (temp, feels_like, ciśnienie, wilgotność, wiatr, zachmurzenie, dew point, deszcz 1h) |
| [`imgw_source.cpp`](../../src/weather/sources/imgw_source.cpp) | `httpGet` → `http::get`; `parseSynopticResponse` → wybór najbliższej stacji (Haversine) + parsowanie pól (wiatr km/h → m/s); `findNearestStation`/`findStationData` zrealizowane |
| [`weathergov_source.cpp`](../../src/weather/sources/weathergov_source.cpp) | `httpGet` → `http::get` z `User-Agent`/`Accept: application/json`; `getGridPoints`, `parseHourlyResponse` (temp °F→°C, wiatr mph→m/s, kierunek kardynalny→stopnie, szansa opadów→mm/h), `parseGridpointData` (zachmurzenie, dew point) |

## 3. Czujniki GPIO — uczciwe „brak czujnika” (P8)

libgpiod niedostępny → **sysfs GPIO** (bez zewnętrznych zależności):

| Plik | Zmiana |
|------|--------|
| [`wind_sensor.cpp`](../../src/weather/sensors/wind_sensor.cpp) | `GpioWindSensor::initialize()` eksportuje pin sysfs + kierunek „in”; gdy pin niedostępny → `false` → `isOperational()==false` (monitor raportuje „brak czujnika” zamiast fałszywego 0.0). `readWindSpeed()` liczy zbocza narastające w oknie 100 ms i przelicza na m/s |
| [`rain_sensor.cpp`](../../src/weather/sensors/rain_sensor.cpp) | `GpioRainSensor::initialize()` sysfs GPIO; brak pinu → `false` → „brak czujnika”. `readRainDetected()` czyta rzeczywistą wartość (z opcją inwersji); `readTotalRainfall()` akumuluje |

## 4. Kanały powiadomień (P9)

| Plik | Zmiana |
|------|--------|
| [`webhook_channel.cpp`](../../src/notifications/channels/webhook_channel.cpp) | `httpPost` → realny `http::post` (POST/PUT, nagłówki, auth token, retry z backoffem) — zamiast stuba zwracającego `true` |
| [`email_channel.cpp`](../../src/notifications/channels/email_channel.cpp) | `send()` → realny SMTP przez `http::smtpSend` (STARTTLS + auth) — zamiast logowania |
| [`mqtt_channel.cpp`](../../src/notifications/channels/mqtt_channel.cpp) | **Minimalny MQTT 3.1.1 publisher** na surowym gnieździe TCP: CONNECT → CONNACK → PUBLISH (QoS 0) → DISCONNECT. Przy braku brokera `send()` zwraca **false** (uczciwa porażka zamiast cichego `true`) |

## 5. Testy

### Nowy test — [`tests/test_weather.cpp`](../../tests/test_weather.cpp) (11 testów)

| Obszar | Test |
|--------|------|
| P8 (parsowanie, próbki JSON bez sieci) | OpenWeatherMap: One Call current, odrzucenie złego JSON; IMGW: najbliższa stacja + pola (km/h→m/s); Weather.gov: hourly (mph→m/s, SW→225°, POP→mm/h), gridpoint (zachmurzenie/dew point) |
| P8 („brak czujnika”) | `GpioWindSensor`/`GpioRainSensor` bez sprzętu → `initialize()==false`, `isOperational()==false` |
| P9 | Email wymaga odbiorców; Webhook wymaga URL; MQTT wymaga brokera; **MQTT bez brokera → `send()==false`** (uczciwa porażka) |

Zarejestrowany w [`CMakeLists.txt`](../../CMakeLists.txt) jako `WeatherTest`.

### Wyniki

```
100% tests passed, 0 tests failed out of 18   (C++ ctest, w tym nowy WeatherTest)
32/32 proxy tests                              (bez regresji)
```

### Weryfikacja live (libcurl)

Test standalone `http::get`/`http::post` przeciw lokalnemu serwerowi HTTP:
```
GET ok=1 body='hello-from-server'
POST ok=1
```

## 6. Status Fazy 4

| Usterka | Status |
|---------|:------:|
| **P8** — `httpGet` (openweathermap/weathergov/imgw) TODO | ✅ libcurl + parsowanie JSON |
| **P8** — czujniki GPIO zwracały 0.0/false | ✅ sysfs GPIO + uczciwe „brak czujnika” |
| **P9** — email/mqtt/webhook logowały zamiast wysyłać | ✅ SMTP (libcurl), webhook (libcurl), MQTT (surowy socket) |

**Uwagi:** MQTT używa minimalnego klienta MQTT 3.1.1 QoS 0 (brak libmosquitto/paho); gdy libmosquitto będzie dostępny, można podmienić. `notification_engine` nadal nie jest hostowany jako `NotificationService` (P9 w zakresie kanałów) — hosting serwisu to osobny krok.
