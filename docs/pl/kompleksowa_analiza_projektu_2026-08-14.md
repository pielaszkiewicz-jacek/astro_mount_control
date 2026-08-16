# Kompleksowa analiza projektu `astro_mount_control`

**Data analizy:** 2026-08-14
**Zakres:** stabilność serwisu, stabilność numeryczna, poprawność implementacyjna, poprawność integracji, kompletność implementacji UI.
**Metoda:** analiza statyczna kodu źródłowego (C++, proto, Node.js proxy, SPA) + weryfikacja dokumentacji względem implementacji + porównanie z poprzednim raportem ([`kompleksowa_analiza_projektu_2026-08-11.md`](kompleksowa_analiza_projektu_2026-08-11.md)).
**Niezależnie:** raport numericzny ([`analiza_stabilnosci_numerycznej.md`](analiza_stabilnosci_numerycznej.md)) oraz [`analiza_projektu.md`](analiza_projektu.md).

---

## 0. Streszczenie — kluczowe ustalenia

Projekt **rdzenia montażu** (montaż → kontroler → modele → HAL → gRPC) jest **dojrzały, dobrze zabezpieczony numerycznie i stabilny wątkowo**. Wszystkie 10 poprawek N1–N10 oraz poprawki B1–B8/R1–R3/R7 z poprzednich raportów są **potwierdzone w bieżącym kodzie**. Pojawiły się też nowe, realne implementacje (ST4 kalibracja pomiarowa, field-rotation CASUAL z kwaternionem, HAL derotatora TMC5160, in-process dome/derotator/focuser) — obszary wcześniej oznaczone jako "stuby" zostały częściowo uzupełnione.

Analiza ujawnia jednak **jeden krytyczny, systemowy defekt integracji web-proxy** oraz **kilka średnich/małych usterek poprawności i kompletności**:

| # | Priorytet | Obszar | Problem | Lokalizacja |
|---|-----------|--------|---------|-------------|
| P1 | 🔴 Wysoki | Integracja / UI | **Proxy web — wszystkie trasy serwisów rozszerzonych (weather, power, sequencer, camera, focuser, guider, pec, pulley) wywołują RPC nieistniejące na kliencie `MountControllerService`** (`grpcCall`) → `TypeError` → 502 lub dane symulowane. Działają tylko dome i derotator (dedykowane klienty). | [routes/focuser.js:22](../web/proxy/routes/focuser.js:22), [routes/camera.js:22](../web/proxy/routes/camera.js:22), [routes/guider.js:22](../web/proxy/routes/guider.js:22), [routes/pulley.js:22](../web/proxy/routes/pulley.js:22), [routes/weather.js:20](../web/proxy/routes/weather.js:20), [routes/power.js:20](../web/proxy/routes/power.js:20), [routes/sequencer.js:20](../web/proxy/routes/sequencer.js:20), [routes/pec.js:20](../web/proxy/routes/pec.js:20), [server.js:21](../web/proxy/server.js:21) |
| P2 | 🔴 Wysoki | Poprawność impl. | **Serwisy gRPC zdefiniowane w proto, ale nigdy niehostowane/niezaimplementowane w C++:** `CameraService`, `PulleyService`, `PecService`, `St4GuiderService`. Brak serwera/implementacji — zakładki UI camera/pulley/pec/guider są martwe. | [camera.proto:16](../proto/camera.proto:16), [pulley.proto:17](../proto/pulley.proto:17), [st4_guider.proto:6](../proto/st4_guider.proto:6), [CMakeLists.txt:341](../CMakeLists.txt:341) |
| P3 | 🟠 Średni | Integracja | **Focuser hostowany in-process na 50051, ale web-proxy nie tworzy dla niego klienta** (server.js tworzy tylko grpc/db/dome/derotator); trasa `focuser.js` woła RPC z klienta montażu. UI focuser zawsze symulowane. Dodatkowo brak `config/focuser_config.json` → focuser zawsze symulowany. | [server.js:21](../web/proxy/server.js:21), [routes/focuser.js:22](../web/proxy/routes/focuser.js:22), [src/main.cpp:289](../src/main.cpp:289) |
| P4 | 🟠 Średni | Integracja / konfig | **Niespójność domyślnych włączeń serwisów:** `default.json` nie zawiera sekcji `external_services` → dome/derotator/focuser/weather/power wyłączone w kontrolerze, a web-proxy domyślnie pokazuje zakładki derotator i focuser (symulowane). | [configuration.cpp:648](../src/config/configuration.cpp:648), [config.js:59](../web/proxy/config.js:59), [main.cpp:267](../src/main.cpp:267) |
| P5 | 🟠 Średni | Poprawność (derotator) | **`DerotatorController::getFieldRotation()` zawsze używa modelu alt-az** — dla montażu równikowego (domyślny `mount_type: equatorial`) podaje niezerową szybkość rotacji pola (powinna być ~0). Pole `MountPositionUpdate.mount_type` z proto nie jest wypełniane ani używane; `calculateEquatorial()` zwraca 0, ale nigdy nie jest wywoływane. | [derotator_controller.cpp:54](../src/controllers/derotator_controller.cpp:54), [derotator.proto:59](../derotator/proto/derotator.proto:59), [main.cpp:462](../src/main.cpp:462) |
| P6 | 🟠 Średni | Poprawność (HAL) | **TMC5160: `position_` nigdy nie jest aktualizowane podczas ruchu** — `setAngle()`/`setRate()` zapisują tylko target/rate; status `current_position_deg` pozostaje 0.0 (poza `home()`). | [derotator_tmc5160.cpp:147](../src/hal/derotator_hal/derotator_tmc5160.cpp:147), [:160](../src/hal/derotator_hal/derotator_tmc5160.cpp:160) |
| P7 | 🟠 Średni | Poprawność (guider) | **`St4Guider::startPHD2()` to stub** — ustawia `phd2_connected_=true` bez połączenia socketowego; brak realnej integracji PHD2; `St4GuiderService` niehostowana. | [st4_guider.cpp:12](../src/controllers/st4_guider.cpp:12) |
| P8 | 🟠 Średni | Poprawność (weather) | **Źródła pogody i czujniki GPIO to stuby:** `httpGet` (openweathermap/weathergov/imgw) to TODO bez libcurl; wiatromierz/deszczomierz GPIO zwracają 0.0/false. Serwer pogody zwraca bezwartościowe dane; auto-park zależny od pogody nie działa z prawdziwymi danymi. | [openweathermap_source.cpp:107](../src/weather/sources/openweathermap_source.cpp:107), [weathergov_source.cpp:135](../src/weather/sources/weathergov_source.cpp:135), [imgw_source.cpp:133](../src/weather/sources/imgw_source.cpp:133), [wind_sensor.cpp:60](../src/weather/sensors/wind_sensor.cpp:60), [rain_sensor.cpp:56](../src/weather/sensors/rain_sensor.cpp:56) |
| P9 | 🟡 Niski | Poprawność (powiadomienia) | **Kanały powiadomień to stuby:** email/mqtt/webhook logują zamiast wysyłać; `NotificationService` zaimplementowana, ale niehostowana; brak trasy `/api/notifications` i komponent nie jest montowany w UI. | [email_channel.cpp:37](../src/notifications/channels/email_channel.cpp:37), [mqtt_channel.cpp:23](../src/notifications/channels/mqtt_channel.cpp:23), [webhook_channel.cpp:75](../src/notifications/channels/webhook_channel.cpp:75), [notifications.js:45](../web/public/js/components/notifications.js:45) |
| P10 | 🟡 Niski | Architektura | **`ConfigMonitor` nie jest podłączony do `main.cpp`** — hot-reload konfiguracji to martwy kod (funkcjonalność przetestowana, ale niewykorzystana w runtime). | [config_monitor.cpp:175](../src/config/config_monitor.cpp:175), [main.cpp:85](../src/main.cpp:85) |
| P11 | 🟡 Niski | Konfig | Brak `config/focuser_config.json`, na który wskazuje `main.cpp` → focuser zawsze przechodzi na symulację. | [main.cpp:289](../src/main.cpp:289) |
| P12 | 🟡 Niski | Bezpieczeństwo | `power_stub` używa `InsecureChannelCredentials()` nawet gdy SSL włączony — niespójność z resztą. | [main.cpp:355](../src/main.cpp:355) |
| P13 | 🟡 Niski | Stabilność | **`WatchStatus` (derotator): pojedynczy `watching_`/wątek współdzielony** — dwóch klientów streamujących konfliktuje (rozłączenie jednego zatrzymuje stream drugiego). | [derotator_service_impl.cpp:143](../derotator/src/derotator_service_impl.cpp:143) |
| P14 | 🟡 Niski | Poprawność (HAL) | Stuby HAL: `st4_gpio_libgpiod`, `st4_ftdi`, `st4_mcp2221`, `st4_arduino`, `power_i2c`, focuser `moonlite/zwo/pegasus` (USB/HID/serial) — TODO. | [st4_gpio_libgpiod.cpp:3](../src/hal/st4_hal/st4_gpio_libgpiod.cpp:3), [power_i2c.cpp:3](../src/hal/power_hal/power_i2c.cpp:3), [focuser_zwo.cpp:29](../src/hal/focuser_hal/focuser_zwo.cpp:29) |
| P15 | 🟡 Niski | Porządek | Martwy kod/porządki: `alt_true` nieużywane w `calculateCasual`; callback `setPositionCallback` odtwarzany przy każdej aktualizacji; `FIXME setMountParameters` w `mount_controller.cpp`; `gui/` puste; `config/` pełne snapshotów. | [field_rotation_model.cpp:127](../src/models/field_rotation_model.cpp:127), [derotator_service_impl.cpp:192](../derotator/src/derotator_service_impl.cpp:192), [mount_controller.cpp:463](../src/controllers/mount_controller.cpp:463) |

---

## 1. Metodologia

1. Pełny przegląd struktury (src/include/proto/serwisy/web/tests/config).
2. Analiza statyczna kluczowych modułów: `astronomical_calculations`, `mount_controller` (pętla trackingu, watchdog, wątki), `kalman_filter`, `tpoint_model`, `field_rotation_model`, `pec_model`, `st4_calibration/st4_guider`, HAL (derotator, st4, power, focuser, weather).
3. Mapowanie powierzchni API: proto gRPC ↔ `service_impl` ↔ trasy web-proxy ↔ komponenty SPA.
4. Weryfikacja poprawek z poprzednich raportów (N1–N10, B1–B8, R1–R3, R7).
5. Identyfikacja nowych usterek i przygotowanie planu napraw.

**Uwaga:** pełny build + uruchomienie testów nie były wykonywane w tej sesji (brak katalogu `build/` w workspace). Poprzedni raport (2026-08-11) potwierdzał przejście testów; nowe testy ([`test_field_rotation_st4.cpp`](../tests/test_field_rotation_st4.cpp)) pokrywają nowe ścieżki CASUAL/ST4 — zaleca się ich uruchomienie po wprowadzeniu zmian.

---

## 2. Weryfikacja poprawek z poprzedniego raportu (2026-08-11)

| # | Poprawka | Status 2026-08-14 | Weryfikacja |
|---|----------|:-----------------:|-------------|
| N1 | `WatchState` — pola `tracking_rate_ra/dec` = `axis_rate·3600` (nie `tracking_error`) | ✅ | [service_impl.cpp:191](../src/api/service_impl.cpp:191) |
| N2 | Usunięcie duplikatów `proto/dome.proto` / `proto/derotator.proto` | ✅ | katalog `proto/` zawiera tylko 10 unikalnych proto; brak dome/derotator |
| N3 | Derotator: realne `ha_hours`/`dec_deg` (LST−RA, Dec) | ✅ | [main.cpp:466](../src/main.cpp:466) |
| N4 | Dome: prawdziwy azymut z `equatorialToHorizontal` | ✅ | [main.cpp:430](../src/main.cpp:430) |
| N5 | Proxy: domyślne porty dome/derotator = 50051 (in-process) | ✅ | [config.js:24](../web/proxy/config.js:24), [:32](../web/proxy/config.js:32) |
| N6 | Dokumentacja Qt GUI oznaczona jako niedostępna | ✅ | brak śladów `qt_gui.md` w repo |
| N7 | Dokumentacja CANopen HAL ujednolicona (MF7025V2 jako jedyny CAN) | ✅ | [hal_factory.cpp:22](../src/hal/hal_factory.cpp:22) |
| N8 | `WatchState` — wyrównany zestaw pól z `GetState` | ✅ | [service_impl.cpp:207](../src/api/service_impl.cpp:207) |
| N9 | `ConfigMonitor::getConfiguration()` — kopia pod lockiem | ✅ | [config_monitor.cpp:195](../src/config/config_monitor.cpp:195) |
| N10 | PEC `performFFT` — DFT per-cykl, ortogonalna baza, poprawna faza | ✅ | [pec_model.cpp:72](../src/models/pec_model.cpp:72) |
| B1 | LST `lon*D2R` (nie `/15`) | ✅ | [astronomical_calculations.cpp:244](../src/core/astronomical_calculations.cpp:244) |
| B2 | GMST bez ΔAT | ✅ | [astronomical_calculations.cpp:243](../src/core/astronomical_calculations.cpp:243) |
| B4 | Refrakcja — jedna korekcja T, bez 1.33322 | ✅ | [astronomical_calculations.cpp:195](../src/core/astronomical_calculations.cpp:195) |
| B7 | FieldRotation — bez tan(δ), guard mianownika | ✅ | [field_rotation_model.cpp:32](../src/models/field_rotation_model.cpp:32) |
| R1–R3 | KF: kontrola PD S, metryka, adaptacyjne Q | ✅ | [kalman_filter.cpp](../src/models/kalman_filter.cpp) |
| R7 | SoftLimits — `std::fmod` zamiast pętli | ✅ | [mount_controller.cpp:6688](../src/controllers/mount_controller.cpp:6688) |

---

## 3. Stabilność serwisu — ocena

### 3.1 Potwierdzone mocne strony

- **Watchdog trackingu** (dt > 5 s → ERROR) i **slew watchdog** (max(60 s, 3×nominalny+60 s) → ERROR z zatrzymaniem napędów) — [mount_controller.cpp:1738](../src/controllers/mount_controller.cpp:1738).
- **Teardown** w `main.cpp` w pełni jawny i sekwencyjny (gamepad → gRPC → subusługi → `shutdown()` → `Logger::shutdown()`), z obsługą wyjątków — [main.cpp:525](../src/main.cpp:525).
- **Signal handling** — tylko atomic flag (poprawnie).
- **Thread safety**: `shared_mutex`, spójna hierarchia blokad, `joinWorkThread()` bez `state_mutex_` (brak deadlocku), re-entrancy guard `notify_in_progress_`.
- **`shutdown()` idempotentny** (guard UNINITIALIZED + `joinable()`).

### 3.2 Uwagi stabilnościowe

- **P13:** [`WatchStatus` derotatora](../derotator/src/derotator_service_impl.cpp:143) — pojedynczy flag/thread współdzielony; przy ≥2 klientach streamy się zakłócają. Wymaga per-klientowej obsługi (pętla lokalna zamiast członka klasy).
- **P10:** `ConfigMonitor` nieużywany w runtime — brak hot-reloadu (zmiany konfiguracji z UI zapisywane do pliku, ale kontroler nie przeładowuje ich bez restartu).
- `WatchState` (montaż) działa z `sleep_for(100 ms)` bez guarda na opóźnienia — dopuszczalne dla strumienia statusu.

### 3.3 Ocena stabilności serwisu

**9/10.** Rdzeń montażu bardzo stabilny; drobne uwagi to P13 (konkurencja WatchStatus) i P10 (ConfigMonitor martwy).

---

## 4. Stabilność numeryczna — ocena

### 4.1 Potwierdzone mocne strony

- **IAU 2006** precesja/nutacja (`iauPmat06`, `iauNut06a`, `iauObl06`) — sub-arcsecond.
- **KF**: Joseph form, LDLT z kontrolą pivotów, renormalizacja kwaternionu, symetryzacja P; guardy NaN.
- **Track loop**: ~20 strażników `std::isfinite()`, watchdog, guider jako offset pozycji, nutacja/TPoint jako delta.
- **Efemerydy**: barycentryczna interpolacja Lagrange'a, clamp zakresu czasu.
- **TPoint**: ColPivHouseholderQR, ostrzeżenie o kolinarnych kolumnach.
- **Refrakcja**: poprawiona formuła Saemundssona z clampem zenitu ([astronomical_calculations.cpp:165](../src/core/astronomical_calculations.cpp:165)).
- **PEC**: DFT per-cykl z koherentną integracją 3 cykli — wyciek spektralny usunięty ([pec_model.cpp:72](../src/models/pec_model.cpp:72)).

### 4.2 Nowe/pozostałe uwagi numeryczne

- **P5 (derotator):** użycie modelu alt-az dla montażu równikowego daje błędną (niezerową) szybkość rotacji pola — to błąd poprawności, nie niestabilności, ale krytyczny dla działania derotatora. Konieczne przekazanie `mount_type` i wybór modelu (`calculateEquatorial`/`calculateAltAz`/`calculateCasual`).
- **Uwaga (drobna):** w `calculateCasual` zmienna `alt_true` jest wyliczana i clampowana, ale nieużywana w wyniku — porządek (P15).
- Konwencja kwaternionów `[x,y,z,w]` (astro_calc / field_rotation) vs `[w,x,y,z]` (KF) — warto udokumentować w jednym miejscu (pozostało z poprzedniego raportu).

### 4.3 Ocena stabilności numerycznej

**9/10.** Rdzeń numeryczny wzorowy. Główna uwaga to P5 (wybór modelu field-rotation zależnie od typu montażu).

---

## 5. Poprawność implementacyjna — ocena

### 5.1 Stan obszarów wcześniej oznaczonych jako stuby (2026-08-14)

| Obszar | 2026-08-11 | 2026-08-14 | Uwagi |
|--------|:----------:|:----------:|-------|
| ST4 kalibracja (`st4_calibration.cpp`) | stuba | ✅ zaimplementowana | fitSlope (LSQ), `theoretical()`, pomiarowa z probą przesunięcia |
| ST4 guider (`st4_guider.cpp`) | stuba | ⚠️ częściowo | kalibracja + konwersja korekty→impuls działa; `startPHD2()` to stub; serwis niehostowany (P2/P7) |
| Field-rotation CASUAL | ignorował kwaternion | ✅ z kwaternionem | `calculateCasual()` pełny + testy |
| `calculateEquatorial()` | stuba | ⚠️ zwraca `{}` | celowe (rotacja 0), ale nigdy nieużywane przez kontroler (P5) |
| Derotator TMC5160 HAL | pusty | ⚠️ podstawowy SPI | brak integracji pozycji podczas ruchu (P6) |
| `DerotatorController::getFieldRotation()` | zwracał `{}` | ⚠️ alt-az zawsze | P5 |
| In-process dome/derotator/focuser | — | ✅ wdrożone | działają na wspólnym porcie 50051 |

### 5.2 Pozostałe stuby / braki

- **P2:** `CameraService`, `PulleyService`, `PecService`, `St4GuiderService` — proto bez backendu.
- **P8:** źródła pogody (HTTP) i czujniki GPIO — TODO (brak libcurl, brak GPIO).
- **P9:** kanały powiadomień email/MQTT/webhook — TODO (logują zamiast wysyłać).
- **P14:** stuby HAL: `st4_gpio_libgpiod`, `st4_ftdi`, `st4_mcp2221`, `st4_arduino`, `power_i2c`, focuser `moonlite/zwo/pegasus`.
- **Drobne:** `focus_curve` — brak fitowania hiperbolicznego (fallback paraboliczny, [focus_curve.cpp:178](../src/models/focus_curve.cpp:178)); `mount_controller.cpp:463` — FIXME `setMountParameters`.

### 5.3 Ocena poprawności implementacyjnej

**7/10.** Rdzeń montażu kompletny i poprawny; peryferia (derotator/guider/weather/powiadomienia/kamera/pulley) na różnym poziomie — od "podstawowe działanie" po "czysta definicja proto".

---

## 6. Poprawność integracji — ocena

### 6.1 Ustalenie krytyczne (P1)

Web-proxy tworzy cztery klienty gRPC: **mount (50051), db (50052), dome (50051), derotator (50051)** ([server.js:21](../web/proxy/server.js:21)). Jednocześnie **wszystkie trasy serwisów rozszerzonych** wołają przez `grpcCall()` (klient **mount**) metody, które **nie istnieją** w `MountControllerService` ([mount_controller.proto:327](../proto/mount_controller.proto:327)):

| Trasa | Wywoływana metoda | Istnieje na kliencie mount? | Efekt |
|-------|------------------|:---------------------------:|-------|
| [weather.js:20](../web/proxy/routes/weather.js:20) | `GetWeatherStatus` | ❌ | 502 / dane symulowane |
| [power.js:20](../web/proxy/routes/power.js:20) | `GetPowerStatus` | ❌ | 502 / dane symulowane |
| [sequencer.js:20](../web/proxy/routes/sequencer.js:20) | `StartSequencer` | ❌ | 502 / dane symulowane |
| [focuser.js:22](../web/proxy/routes/focuser.js:22) | `MoveFocuser` | ❌ | 502 / dane symulowane |
| [camera.js:22](../web/proxy/routes/camera.js:22) | `StartExposure` | ❌ | 502 / dane symulowane |
| [guider.js:22](../web/proxy/routes/guider.js:22) | `StartGuiding` | ❌ | 502 / dane symulowane |
| [pulley.js:22](../web/proxy/routes/pulley.js:22) | `DeployPulley` | ❌ | 502 / dane symulowane |
| [pec.js:20](../web/proxy/routes/pec.js:20) | `GetPECStatus` | ❌ | 502 / dane symulowane |

Mechanizm: `grpcCall()` wykonuje `client[method](...)`; dla metody nieobecnej na kliencie `@grpc/grpc-js` rzuca `TypeError` → łapany przez `try/catch` → trasa zwraca 502 albo **zahardkodowane dane symulowane** (np. camera zwraca "Simulated Camera IMX571", focuser "position 50000"). **Użytkownik widzi w UI fałszywe, statyczne dane, nie widząc żadnego błędu połączenia** — to najpoważniejszy problem UX/integracji.

Działają tylko dome i derotator (dedykowane klienty do usług in-process na 50051).

### 6.2 Pozostałe problemy integracyjne

- **P3:** focuser in-process na 50051, ale bez klienta w proxy i bez `focuser_config.json` → zawsze symulowany.
- **P4:** `default.json` nie ma sekcji `external_services` ([configuration.cpp:648](../src/config/configuration.cpp:648)) → wszystkie subusługi wyłączone domyślnie; proxy domyślnie pokazuje derotator/focuser ([config.js:59](../web/proxy/config.js:59)). Montaż + dome/derotator/focuser nie zostaną uruchomione bez edycji configu.
- **P12:** `power_stub` z `InsecureChannelCredentials()` niezależnie od SSL.
- WeatherClient w `main.cpp` działa z serwerem pogody (osobny proces 50055) — ale dane ze źródeł są stubami (P8), a auto-park zależny od pogody bazuje na bezwartościowych danych.

### 6.3 Ocena poprawności integracji

**5/10.** Architektura integracji (in-process vs osobne procesy, jeden wspólny rdzeń) jest przemyślana, ale **warstwa web-proxy jest systematycznie błędnie podpięta** (P1) — UI pokazuje dane symulowane zamiast rzeczywistych serwisów.

---

## 7. Kompletność implementacji UI — ocena

### 7.1 Struktura UI

SPA w `web/public` ma 22 zakładki (status, control, settings, calibration, database, tracking, logging, tests, camera, focuser, guider, pec, derotator, power, sequencer, dome, weather, pulley, lx200 + status/velocity itd.), motyw noktowizyjny, i18n PL/EN, tryb mobilny. Komponenty SPA ładują się poprawnie z `app.js`.

### 7.2 Mapa zakładka → backend → stan (kluczowe)

| Zakładka | Komponent | Trasa proxy | Klient gRPC | Stan backendu | UI działa? |
|----------|-----------|-------------|-------------|---------------|:----------:|
| Status/Control/Calibration/Tracking/Logging/Database/Tests | mountStatus/mountControl/... | mount.js/calibration.js/tracking.js/... | mount ✅ | pełny | ✅ |
| Dome | dome.js | dome.js | dome ✅ (in-process 50051) | pełny | ✅ |
| Derotator | derotator.js | derotator.js | derotator ✅ (in-process 50051) | pełny (z P5/P6) | ✅ (błędne dane rotacji) |
| Weather | weather.js | weather.js | ❌ mount | serwis osobny 50055; źródła = stuby | ⚠️ symulowane |
| Power | power.js | power.js | ❌ mount | serwis osobny 50056 | ⚠️ symulowane |
| Sequencer | sequencer.js | sequencer.js | ❌ mount | serwis osobny (brak klienta w proxy) | ⚠️ symulowane |
| Focuser | focuser.js | focuser.js | ❌ mount | in-process 50051, brak klienta; zawsze symulowany | ⚠️ symulowane |
| Camera | camera.js | camera.js | ❌ mount | brak backendu (P2) | ❌ symulowane |
| Guider | guiderStatus.js | guider.js | ❌ mount | brak backendu (P2/P7) | ❌ symulowane |
| PEC | pec.js | pec.js | ❌ mount | brak backendu (P2) | ❌ symulowane |
| Pulley | pulley.js | pulley.js | ❌ mount | brak backendu (P2) | ❌ symulowane |
| LX200 | lx200.js | lx200.js | mount ✅ | pełny | ✅ |
| Notifications | notifications.js | brak trasy | — | niehostowany; komponent nie montowany | ❌ martwy |

**Wniosek:** UI jest **rozległe i estetyczne, ale pod względem funkcjonalnym tylko ~50% zakładek komunikuje się z realnym backendem.** Zakładki weather/power/sequencer/focuser/camera/guider/pec/pulley pokazują dane symulowane/statyczne z powodu P1–P3; notifications jest komponentem martwym (nie montowanym).

### 7.3 Drobne uwagi UX

- Przyciski testowe w `notifications.js` to `alert()` bez integracji.
- Brak wskaźnika "dane symulowane" w UI — użytkownik nie wie, że nie widzi danych rzeczywistych (wynika z P1).
- `gui/` — katalog pusty (GUI Qt usunięte; dokumentacja zaktualizowana — N6 ✅).

### 7.4 Ocena kompletności UI

**6/10.** Warstwa wizualna bardzo dobra (9/10), ale kompletność funkcjonalna obniżona przez P1–P3 i martwe komponenty.

---

## 8. Pełna mapa usterek (priorytetyzowana)

### Krytyczne (blokują wiarygodność UI/integracji)
- **P1** — przekierowanie tras proxy na właściwe klienty gRPC (albo integracja wszystkich serwisów w jednym procesie).

### Wysokie
- **P2** — implementacja/hosting `CameraService`, `PulleyService`, `PecService`, `St4GuiderService` (lub usunięcie zakładek i dokumentacja).
- **P5** — poprawne modele field-rotation zależnie od `mount_type` (derotator).

### Średnie
- **P3** — klient focuser w proxy + `focuser_config.json`.
- **P4** — spójne domyślne włączenia serwisów (config ↔ proxy).
- **P6** — integracja pozycji w TMC5160 (pętla pozycji / odczyt XACTUAL).
- **P7** — realne PHD2/ST4 (lub uczciwe oznaczenie).
- **P8** — implementacja źródeł pogody i czujników (libcurl, GPIO).

### Niskie / porządkowe
- **P9** — kanały powiadomień; **P10** — podpięcie ConfigMonitor; **P11** — plik focuser_config.json; **P12** — SSL dla power_stub; **P13** — per-klientowy WatchStatus; **P14** — stuby HAL; **P15** — porządki (martwy kod, `config/`, puste `gui/`).

---

## 9. Plan implementacji zmian

### Faza 0 — Diagnostyka i testy (0.5–1 dzień)
1. Uruchomić pełny build i wszystkie testy (w tym [`test_field_rotation_st4.cpp`](../tests/test_field_rotation_st4.cpp)).
2. Napisać test integracyjny proxy (Node.js supertest) potwierdzający obecne 502/symulowane odpowiedzi dla tras rozszerzonych (regresja przed zmianą).

### Faza 1 — Naprawa integracji web-proxy (P1, P3, P4) — priorytet krytyczny (1–2 dni)
1. **Opcja A (rekomendowana, minimalna):** w `web/proxy/grpc/client.js` dodać dedykowane klienty gRPC dla serwisów rozszerzonych i podmienić w trasach:
   - `routes/weather.js` → klient `WeatherService` (port 50055),
   - `routes/power.js` → klient `PowerService` (port 50056),
   - `routes/sequencer.js` → klient `SequencerService` (port 50054 lub konfigurowalny),
   - `routes/focuser.js` → klient `FocuserService` (in-process 50051) z `proto/focuser.proto`,
   - `routes/guider.js` → klient `St4GuiderService` (jeśli serwis zostanie wdrożony w Faza 2),
   - `routes/camera.js`, `routes/pulley.js`, `routes/pec.js` → wstrzymać do Fazy 2 (ukryć zakładki lub zwracać `503` z jawnym komunikatem, **nigdy** danych symulowanych bez oznaczenia).
2. **Poprawić `grpcCall`** (i odpowiedniki) tak, aby brak metody rzucał czytelny błąd 501/503 z komunikatem, zamiast cichego fallbacku.
3. **UI:** w `app.js` / komponentach dodać wskaźnik "TRYB SYMULACJI" przy danych fallback; ukryć zakładki serwisów niedostępnych (`applyExternalServicesVisibility` już to robi dla configu proxy — rozszerzyć o stany z backendu).
4. **Config:** dodać do `default.json` sekcję `external_services` (dome/derotator/focuser/weather/power/sequencer) zgodną z proxy; ujednolicić domyślne włączenia (P4). Dodać `focuser_config.json` (P11).

### Faza 2 — Uzupełnienie brakujących serwisów (P2) (2–5 dni)
1. **St4GuiderService:** implementacja `St4GuiderServiceImpl` opartej o [`St4Guider`](../src/controllers/st4_guider.cpp) + `St4Calibration`; zarejestrować in-process na 50051 (jak dome/derotator) lub jako osobny serwer; podpiąć klient proxy (Faza 1).
2. **PecService:** service wrapper wokół [`PECModel`](../src/models/pec_model.cpp) (enable/train/status); zarejestrować in-process lub osobno.
3. **CameraService / PulleyService:** dopiero po dostępności HAL (kamera ZWO/SDK, silnik linowy) — w międzyczasie **usunąć zakładki** z UI lub oznaczyć jako "niedostępne".

### Faza 3 — Poprawność derotatora (P5, P6) (1–2 dni)
1. Przekazać `mount_type` przez łańcuch `main.cpp → setMountPosition → DerotatorController` oraz `MountPositionUpdate.mount_type`; w `getFieldRotation()` wybierać model:
   - EQUATORIAL → `calculateEquatorial()` (0),
   - ALT_AZ → `calculateAltAz()`,
   - CASUAL → `calculateCasual()` z kwaternionem.
2. W `Tmc5160Derotator` dodać integrację pozycji: pętla/odczyt `XACTUAL` (0x04) lub integrację `VACTUAL·dt`; aktualizować `position_` w `getStatus()`/`isMoving()`.
3. Dodać testy jednostkowe dla P5 (równikowy → 0) i P6 (position po setAngle).

### Faza 4 — Prawdziwe dane pogody i powiadomień (P8, P9) (2–4 dni)
1. Implementacja `httpGet` przez libcurl (uwzględnić w CMake: `find_package(CURL)`); parsowanie JSON dla openweathermap/weathergov/imgw.
2. Czujniki GPIO (libgpiod) dla wiatru/deszczu albo uczciwe oznaczenie "brak czujnika" zamiast 0.0/false.
3. Kanały powiadomień: SMTP (libcurl), MQTT (libmosquitto/paho), webhook (libcurl).

### Faza 5 — Stabilność i porządki (P13, P10, P12, P14, P15) (1–2 dni)
1. P13: per-klientowa obsługa `WatchStatus` (pętla lokalna, bez współdzielonego członka `watching_`).
2. P10: podpięcie `ConfigMonitor` do `main.cpp` (callback → bezpieczny hot-reload lub przynajmniej logowanie zmiany + wymaganie restartu).
3. P12: spójne poświadczenia SSL dla `power_stub`.
4. P14: implementacja lub usunięcie stübów HAL (ST4 i power/focuser).
5. P15: usunięcie martwego kodu (`alt_true`), ujednolicenie callbacku `setPositionCallback`, czyszczenie `config/` ze snapshotów, decyzja o pustym `gui/`.

### Faza 6 — Testy i dokumentacja (1 dzień)
1. Rozszerzyć testy: integracyjne proxy (Faza 1), field-rotation per mount_type (Faza 3), TMC5160 position (Faza 3), źródła pogody (mock HTTP, Faza 4).
2. Zaktualizować `docs/pl/` (API, integracja serwisów, porty, stan kompletności) o nowy stan po wdrożeniu.

---

## 10. Podsumowanie ocen

| Kategoria | Ocena (2026-08-14) |
|-----------|:-------------------:|
| Stabilność serwisu | 9/10 |
| Stabilność numeryczna | 9/10 |
| Poprawność implementacyjna | 7/10 |
| Poprawność integracji | 5/10 |
| Kompletność UI | 6/10 |
| **Średnia (rdzeń)** | **9/10** |
| **Średnia (całość z peryferiami)** | **7/10** |

**Najważniejszy wniosek:** rdzeń (montaż, modele, HAL, API montażu) jest w bardzo dobrym stanie. Najpilniejsza praca dotyczy **warstwy integracji web-proxy (P1–P4)** — bez jej naprawy użytkownik web UI otrzymuje dane symulowane zamiast rzeczywistych dla większości serwisów peryferyjnych, co obecnie maskuje także brak backendów (P2) i stuby (P8, P9, P14).
