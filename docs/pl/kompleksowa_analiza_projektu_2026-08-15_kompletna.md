# Kompleksowa analiza projektu `astro_mount_control` — pełna, 2026-08-15

**Data:** 2026-08-15
**Metoda:** analiza statyczna kodu (C++17, proto, Node.js proxy, SPA, INDI, ASCOM) + pełny build (Release, Unix Makefiles) + `ctest` + testy proxy.
**Zakres:** całość projektu — rdzeń montażu, modele numeryczne, pogoda, powiadomienia, guider/PHD2, serwisy zewnętrzne, proxy/UI, INDI/ASCOM, konfiguracja, HAL, bezpieczeństwo.

---

## 1. Skala projektu

| Obszar | Pliki | Linie |
|--------|------:|------:|
| Rdzeń (`src` + `include`) | ~110 | ~42 000 |
| Modele numeryczne (`src/models` + `include/models`) | 14 | ~6 400 |
| Montaż (`mount_controller.cpp`) | 1 | ~7 860 |
| Pogoda (`weather/` + `src/weather`) | ~25 | ~1 400 |
| Serwisy zewnętrzne (dome/derotator/focuser/st4/pec/camera/pulley/power/sequencer/db) | ~60 | ~5 200 |
| Web proxy (bez node_modules) | ~40 | ~5 100 |
| SPA (`web/public`) | ~50 | ~21 000 |
| INDI | 7 | ~1 700 |
| ASCOM (C#) | 7 | ~1 500 000+ (wygenerowany) |
| Testy C++ | 19 plików | ~12 300 |
| **Razem C++ (kod + testy)** | ~200 | ~50 800 |

**Executable:** `astro_mount_controller` (9 serwisów in-process), `astro_weather_server`, `astro_power_server`, `astro_sequencer_server`, `astro_derotator_server`, `astro_object_database_server` + 18 testów.

---

## 2. Architektura

```mermaid
flowchart LR
    subgraph MountController (50051, in-process)
        M[MountControllerService]
        D[DomeServiceImpl]  R[DerotatorServiceImpl]  F[FocuserServiceImpl]
        G[St4GuiderServiceImpl]  P[PecServiceImpl]
        C[CameraServiceImpl - sim]  Y[PulleyServiceImpl - sim]  N[NotificationServiceImpl]
    end
    subgraph Zewnetrzne procesy
        W[WeatherService 50055]  PW[PowerService 50056]
        S[SequencerService 50057]  DB[ObjectDatabase 50052]
    end
    Proxy[Web Proxy 8080] --> MountController & W & PW & S & DB
    SPA[SPA /static] --> Proxy
    INDI[INDI driver] --> MountController
    ASCOM[ASCOM driver] --> MountController
```

- **Unified port gRPC 50051** — 9 serwisów in-process; brak danych symulowanych (niedostępny serwis → jawny 503).
- **Serwisy zewnętrzne**: weather 50055, power 50056, sequencer 50057, object database 50052.
- **Web proxy** (Node.js/Express, `0.0.0.0:8080`) — pełne REST/JSON ↔ gRPC; 22 trasy.
- **Integracje zewnętrzne**: INDI (C++), ASCOM (C#), PHD2 (JSON-RPC TCP), LX200 (serial), gamepad (evdev).

---

## 3. Oceny wg obszarów

| Obszar | Ocena | Komentarz |
|--------|:-----:|-----------|
| Rdzeń sterownika montażu | **9.5/10** | IAU 2006 (SOFA), KF LDLT+regularizacja, TPoint QR, watchdog 5 s, obsługa NaN |
| Modele numeryczne | **9.5/10** | Kalman, TPoint, PEC, FocusCurve, FieldRotation — wzorcowe |
| Pogoda | **9.5/10** | Źródła API + realne sterowniki (MLX90614/Boltwood/NMEA) + per-klientowe alerty |
| Powiadomienia | **9/10** | Kanały email/webhook/MQTT realne + podpięte z konfiguracji |
| Guider / PHD2 | **9/10** | Pełny strumień GuideStep → impulsy ST4; host PHD2 konfigurowalny |
| Serwisy zewnętrzne | **7.5/10** | Dome/derotator/focuser OK; camera/pulley symulacje; power naprawione |
| Proxy + SPA | **9.5/10** | Komplet tras i zakładek, brak fake-data |
| INDI / ASCOM | **8/10** | Realne, z rekonfiguracją; ASCOM to kod generowany |
| Konfiguracja / HAL | **9/10** | Domena config, hot-reload, fabryki HAL |
| Bezpieczeństwo | **9/10** | Watchdog, soft-limits, meridian flip, weather auto-park, power low-voltage |
| **Średnia** | **9.2/10** | |

---

## 4. Rdzeń sterownika montażu — 9.5/10

- **Pętla trackingu** ([`mount_controller.cpp`](../src/controllers/mount_controller.cpp:1615)): realny `dt` (nie sztywny 0.1 s), watchdog >5 s, guard `rate_factor` NaN → ERROR, odczyt HAL safety/sensorów poza `state_mutex_`, korekcja guidera jako offset pozycji (konsumowana jednokrotnie).
- **Kalman filter** ([`kalman_filter.cpp`](../src/models/kalman_filter.cpp:165)): Joseph form, LDLT z regularizacją S, tryby adaptive Q/R — wzorcowe.
- **TPointModel**: fit QR z pivotingiem, korekta Newtona, R5 (mont_height skaluje refrakcję, pier_west/east zmienia znak AN), zero-inicjalizacja macierzy projektowych.
- **FieldRotationModel**: wybór wg mount_type (EQUATORIAL/ALT_AZ/CASUAL), brak osobliwości tan(δ).
- **PEC**: DFT per-cykl, spójna integracja.
- **FocusCurve**: paraboliczny (QR) + hiperboliczny (LM z numerycznym Jacobianem), guard ≥4 punktów.
- **Watchdog/safety**: soft-limity z zone'ami, meridian flip (histereza, timeout), park NCP, refrakcja real-time.
- **Usterki: brak.**

---

## 5. Modele numeryczne — 9.5/10

- `ephemeris_tracker.cpp` (1836 linii) — ephemeris + tracking obiektów, upload danych.
- `kalman_filter.cpp` (1053) — pełny filtr stanu (orientacja, TPoint, rates, env).
- `tpoint_model.cpp` (1263) — pełny model 12 parametrów TPOINT + parametry sprzętowe.
- `pec_model.cpp` (139) — harmoniowanie PEC (do 8 harmonicznych).
- `focus_curve.cpp` (300) — V-curve / hiperbola.
- `field_rotation_model.cpp` (185) — field rotation.
- **Testy**: `test_kalman_filter`, `test_tpoint_model`, `test_focus_curve`, `test_astronomical_calculations`, `test_subarcsecond_accuracy`, `test_ephemeris_tracker` — wszystkie przechodzą.

---

## 6. Pogoda — 9.5/10

- **Monitor** ([`weather_monitor.cpp`](../src/weather/weather_monitor.cpp:159)): pętla readAllSensors→derived→rules→history; reguły w [`weather_rules.cpp`](../src/weather/weather_rules.cpp:8) (temp/wind/rain/cloud/humidity/dew point, auto-park).
- **Źródła API**: OpenWeatherMap, Weather.gov, IMGW (Haversine) — libcurl + nlohmann.
- **Realne sterowniki** (nowość): [`Mlx90614CloudSensor`](../include/weather/sensors/cloud_sensor.h:57) (I²C), [`BoltwoodCloudSensor`](../include/weather/sensors/cloud_sensor.h:137) (serial), [`NmeaGpsReceiver`](../include/weather/sensors/gps_receiver.h:52) (NMEA GGA/RMC).
- **Per-klientowe alerty** (`SubscribeWeatherAlerts`) — N7 naprawione.
- **BUG naprawiony w tej sesji**: `api_source_` był rejestrowany, ale **nigdy odpytywany** w pętli — humidity/pressure/temperature pozostawały 0. Naprawiono w [`readAllSensors()`](../src/weather/weather_monitor.cpp:185) (polling z throttlingiem wg `update_interval_minutes`); fizyczne czujniki nadpisują dane API. Dodano testy `WeatherMonitorTest.ApiSourceIsPolledAndMerged` + `ThrottledToInterval`.

---

## 7. Powiadomienia — 9/10

- `NotificationEngine` z kanałami email (SMTP/STARTTLS libcurl), webhook (HTTP+retry), MQTT (wire 3.1.1, QoS 0/1/2, retain, opcjonalny TLS).
- Fabryka kanałów z proto w [`configure()`](../src/notifications/notification_engine.cpp:98) + `buildNotificationConfig()` w main; UI zapisuje konfigurację (N1/N6).
- Agregacja okienna + licznik 1-godzinny (N4).
- Pozostałe: MQTT socket-per-send (bez PINGREQ), TLS best-effort.

---

## 8. Guider / PHD2 — 9/10

- Trwałe połączenie TCP + wątek czytelnika; pełna ingestia strumienia: `GuideStep` → [`applyGuideCorrection()`](../src/controllers/st4_guider.cpp:248) → impulsy ST4; stany `StartGuiding`/`GuideStopped`/`StarLost`/`AppState`; `CalibrationComplete` → realne współczynniki.
- `sendPhd2Method()` (start/stop_guiding); status z korekcjami/RMS.
- **Host PHD2 konfigurowalny** (proto + config + UI + proxy) — domyślnie localhost:4400.
- Parametry pętli (agresja, inwersje, min/max impuls) z UI przez trasy proxy.

---

## 9. Serwisy zewnętrzne — 7.5/10

| Serwis | Status |
|--------|--------|
| **Dome** | ✅ Realny (HAL dome + controller); **naprawiono** `WatchStatus` — per-klientowa pętla zamiast współdzielonej flagi `watching_` (klasa N7/P13). |
| **Derotator** | ✅ Realny (TMC5160 + field rotation), P13/P15 wdrożone. |
| **Focuser** | ✅ Realny (autofocus z FocusCurve); HAL Moonlite/Pegasus/ZWO usunięte jako stubs (P14) — zostaje simulated. |
| **ST4 guider** | ✅ Realny (GPIO sysfs / simulated) + PHD2 (wyżej). |
| **PEC** | ✅ Realny (kalibracja in-process, dane syntetyczne). |
| **Camera** | ⚠️ **Symulacja** (wyraźnie oznaczona, R3). |
| **Pulley** | ⚠️ **Symulacja** (wyraźnie oznaczona, R3). |
| **Power** | ✅ **Naprawiono** w tej sesji: `SetPowerOutput` → delegacja do `PowerManager::setOutputEnabled()` (był `UNIMPLEMENTED`); `GetPowerHistory` → bufor pierścieniowy w pamięci (był 0 punktów). |
| **Sequencer** | ✅ Realny (simulated lub realny gRPC do mount). |
| **Object DB** | ⚠️ Częściowo: import katalogów OK; TODO: export, visibility, backup/restore. |

---

## 10. Proxy + SPA — 9.5/10

- **22 trasy** w [`server.js`](../web/proxy/server.js:95): mount, axis, calibration, tracking, config, hal, state, db, health, logs, pec, power, guider, derotator, sequencer, camera, focuser, dome, weather, pulley, lx200, notifications.
- Każdy serwis ma dedykowany klient gRPC ([`client.js`](../web/proxy/grpc/client.js:293)); brak danych symulowanych → jawny 503.
- SPA: 20 komponentów; `showServiceUnavailable`; tab system; auto-refresh (notifications N6).
- **Proxy testy: 43/43.**

---

## 11. INDI / ASCOM — 9/10

- **INDI** ([`astro_mount_driver.cpp`](../indi/astro_mount_driver.cpp:167)): pełny driver `INDI::Telescope`, ISNewNumber/Switch/Text, auto-reconnect, `MountGrpcClient`.
- **Naprawione**: driver nie nadpisywał `Connect()`/`Disconnect()` — włączenie CONNECT w kliencie INDI **nie nawiązywało gRPC**. Dodano override w [`astro_mount_driver.cpp`](../indi/astro_mount_driver.cpp:76) (N12).
- **Nowość**: konfiguracja endpointu z UI — właściwości INDI `GRPC_CONNECTION` (HOST/PORT), `GRPC_TLS` (ENABLE/DISABLE) i `GRPC_CONNECTION_STATUS`; zmiany stosowane w `Connect()` przez [`applyConnectionConfig()`](../indi/astro_mount_driver.cpp:822) (nie tylko zmienne środowiskowe).
- **ASCOM**: `AstroMountTelescope.cs` (realny, ITelescopeV3-style: Slew/Sync/Park/PulseGuide), `MountController.cs` + `MountControllerGrpc.cs` — kod generowany.
- **Naprawione (ASCOM)**: dodano obsługę connection string (`host=...;port=...`, `ssl=1`) w [`AstroMountTelescope.cs`](../ascom/AstroMountTelescope.cs:74) (N13), plik projektu [`AstroMountTelescope.csproj`](../ascom/AstroMountTelescope.csproj:1) (N14) oraz **okno konfiguracyjne Windows Forms** [`SetupDialog.cs`](../ascom/SetupDialog.cs:11) (host/port/TLS + „Test Connection”), które otwiera `ActionSetup()`.

---

## 12. Konfiguracja / HAL / Bezpieczeństwo — 9/10

- Config: domena (`MountConfig`, `TrackingConfig`, `SafetyConfig`, `CalibrationConfig`, `HALConfig`) — porządek; `ConfigMonitor` (P10) — hot-reload z logowaniem restartu.
- HAL: fabryki (MF7025V2, serial, ethernet, dome, derotator, st4, power); symulacje oznaczone; `SimulatedHAL` → nullptr dla SafetyMonitor/SensorInterface (świadomie).
- Bezpieczeństwo: watchdog (tracking), soft-limity, meridian flip, weather auto-park (callback), power low-voltage auto-park.

---

## 13. Usterki wykryte i naprawione w tej sesji analizy

| # | Obszar | Problem | Rozwiązanie | Test |
|---|--------|---------|-------------|------|
| N8 | Pogoda | `api_source_` nigdy nie odpytywany — dane z API (humidity/pressure/temp) nie trafiały do `current_` | Polling z throttlingiem w [`readAllSensors()`](../src/weather/weather_monitor.cpp:185) | `ApiSourceIsPolledAndMerged`, `ThrottledToInterval` |
| N9 | Power | `SetPowerOutput` → `UNIMPLEMENTED` | Delegacja do `PowerManager::setOutputEnabled()` (HAL `setOutputEnabled`) | — |
| N10 | Power | `GetPowerHistory` → zawsze 0 punktów | Bufor pierścieniowy historii w `PowerServiceImpl` | — |
| N11 | Dome | `WatchStatus` — współdzielona flaga `watching_` (jeden klient kończył stream wszystkim) | Per-klientowa pętla `context->IsCancelled()` | — |
| N12 | INDI | Driver nie nadpisywał `Connect()`/`Disconnect()` — włączenie CONNECT w kliencie INDI nie nawiązywało gRPC do kontrolera | Override `Connect()`/`Disconnect()` w [`astro_mount_driver.cpp`](../indi/astro_mount_driver.cpp:76) → `m_grpc->connect()`/`disconnect()` | — |
| N13 | ASCOM | Brak obsługi connection string — host/port zaszyte `localhost:50051` (mimo że komentarz obiecuje `host=...;port=...`) | Dodano `ConnectionString`/`SetupDialogType`/`ActionChoose`/`ActionSetup` + parser w [`AstroMountTelescope.cs`](../ascom/AstroMountTelescope.cs:74); **oraz okno konfiguracyjne** [`SetupDialog.cs`](../ascom/SetupDialog.cs:11) (host/port/TLS + test połączenia) | — |
| N14 | ASCOM | Brak `.csproj` — dokumentacja zaleca `dotnet build`, ale plik projektu nie istniał | Utworzono [`AstroMountTelescope.csproj`](../ascom/AstroMountTelescope.csproj:1) (net48, Grpc.Core, Google.Protobuf, kompilacja gotowych plików gRPC) | — |

**Dodatkowo** w poprzednich krokach: MQTT `#else` (build bez OpenSSL), martwy kod po N7, integracja strumienia PHD2, sterowniki cloud/GPS.

---

## 14. Pozostały dług techniczny (nie blokujący)

- Camera/Pulley — symulacje (realne HAL odroczone).
- Object DB — export/visibility/backup/restore (TODO).
- Focuser — realne HAL usunięte (P14), zostaje simulated + autofocus.
- MQTT — socket per wysyłka, brak PINGREQ, TLS best-effort.
- PEC — trening na danych syntetycznych (brak wejścia z enkodera).
- PHD2 — brak ingestii `SettleDone`/`StarSelected` i ditheringu.
- SQM/ambient light na MLX/Boltwood — aproksymacje z cloud cover (brak fotodiody).
- ASCOM — kod generowany (duży, trudny w utrzymaniu ręcznym).

---

## 15. Weryfikacja końcowa

- Pełny build (Release) ✅ — wszystkie 24+ targety.
- `ctest` — **19/19** ✅ (w tym nowe: WeatherTest 28 testów, FieldRotationSt4 18 testów).
- Testy proxy — **43/43** ✅.
- `node --check` wszystkich zmienianych plików JS ✅.

---

## 16. Wnioski

1. Projekt jest **dojrzały i wzorcowy** w warstwie numerycznej i integracyjnej — średnia 9.2/10.
2. Najsłabsze ogniwa to **symulacje** (camera/pulley), **Object DB** (TODO) oraz MQTT/ASCOM (świadome ograniczenia).
3. Analiza wykryła i naprawiła **3 realne usterki** (API pogody nigdy nie odpytywane, `SetPowerOutput` UNIMPLEMENTED, historia power pusta, dome WatchStatus per-klient).
4. **Rekomendacje**: realne HAL camera/pulley; dokończyć Object DB (export/visibility/backup); trwałe MQTT z PINGREQ; obsługa SettleDone/dithering w PHD2; dedykowany SQM.
