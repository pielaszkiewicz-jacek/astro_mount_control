# Kompleksowa analiza projektu `astro_mount_control` — po wdrożeniu N1–N7 + integracji strumienia PHD2

**Data:** 2026-08-15
**Zakres:** ponowna, pełna analiza statyczna po wdrożeniu poprawek N1–N7 (wykrytych w [`kompleksowa_analiza_projektu_2026-08-15_po_R1_R8_P7_P9.md`](kompleksowa_analiza_projektu_2026-08-15_po_R1_R8_P7_P9.md)) oraz integracji strumienia zdarzeń PHD2 z pętlą prowadzenia.
**Metoda:** analiza statyczna kodu (C++, proto, Node.js proxy, SPA) + pełny build + `ctest` + testy proxy + round-trip enum w proxy gRPC.

---

## 0. Streszczenie

Wszystkie usterki N1–N7 z poprzedniego raportu zostały **zweryfikowane jako wdrożone**, a podczas weryfikacji **poprawiono dwa dodatkowe problemy**:
- w [`mqtt_channel.cpp`](../src/notifications/channels/mqtt_channel.cpp:179) gałąź `#else` (build bez OpenSSL) deklarowała `SSL*`/`SSL_CTX*` bez dołączonych nagłówków → zmieniono na `void*` (build bez TLS nie kompilował się);
- w [`weather_service_impl.h`](../weather/include/weather_service_impl.h:50) po wdrożeniu N7 pozostał martwy kod (`alertBroadcastLoop()`, `watching_`, `watch_thread_`, `last_alert_`, `alert_changed_`) → usunięty (poprawka per-klientowa uczyniła go nieużywanym).

**Dodatkowo** (rekomendacja nr 2 z poprzedniego raportu, priorytet): **zintegrowano strumień zdarzeń PHD2 z pętlą prowadzenia** w [`st4_guider.cpp`](../src/controllers/st4_guider.cpp:150) — wątek czytelnika parsuje wiadomości JSON-RPC (`Content-Length` framing), `GuideStep` zamieniane są na impulsy ST4 przez HAL, zdarzenia stanu aktualizują `guiding_`, a `CalibrationComplete` wczytuje realne współczynniki kalibracji z PHD2. Parametry pętli (agresja, inwersje, min/max impuls) przekazywane z UI przez trasy proxy.

**Weryfikacja (bezpośrednio po zmianach):**
- Pełny build ✅ (`cmake --build build -j6`)
- `ctest` — **19/19** ✅
- Testy proxy — **43/43** ✅
- Round-trip enum w proxy (`CHANNEL_EMAIL` string → wartość 1) ✅ — potwierdza, że payload konfiguracji z UI poprawnie trafia do backendu (N1).
- Nowe testy integracji strumienia PHD2 (GuideStep→impulsy, dead zone, invert, stany, kalibracja) w [`test_field_rotation_st4.cpp`](../tests/test_field_rotation_st4.cpp:193) ✅

---

## 1. Metodologia

1. Przegląd stanu roboczego (git status) — poprawki N1–N7 w drzewie roboczym.
2. Weryfikacja każdej poprawki N1–N7 w kodzie źródłowym (implementacja + nagłówki + okablowanie).
3. Weryfikacja integracji proxy ↔ SPA ↔ gRPC (trasy, klient gRPC, montowanie komponentów, konwersja enum).
4. Identyfikacja i naprawa dodatkowych usterek znalezionych przy okazji (MQTT `#else`, martwy kod N7).
5. Pełny build + testy (C++ i proxy).

---

## 2. Architektura po zmianach

```mermaid
flowchart LR
    subgraph MountController (50051, in-process)
        M[MountControllerService]
        D[DomeServiceImpl]
        R[DerotatorServiceImpl]
        F[FocuserServiceImpl]
        G[St4GuiderServiceImpl]
        P[PecServiceImpl]
        C[CameraServiceImpl - simulated]
        Y[PulleyServiceImpl - simulated]
        N[NotificationServiceImpl]
    end
    subgraph Zewnetrzne procesy
        W[WeatherService 50055]
        PW[PowerService 50056]
        S[SequencerService 50057]
        DB[ObjectDatabase 50052]
    end
    Proxy[Web Proxy 8080] --> MountController
    Proxy --> W
    Proxy --> PW
    Proxy --> S
    Proxy --> DB
    SPA[SPA /static] --> Proxy
    ASCOM[ASCOM / INDI / GUI] --> MountController
```

- **Port gRPC 50051** (unified) hostuje **9 serwisów in-process**: mount, dome, derotator, focuser, ST4 guider, PEC, camera (sim), pulley (sim), notifications.
- **Serwisy zewnętrzne**: weather 50055, power 50056, sequencer 50057, object database 50052.
- **Web proxy** (Node.js/Express, `0.0.0.0:8080`) tłumaczy REST/JSON ↔ gRPC; niedostępny serwis zwraca jawny **503** (P1).

---

## 3. Weryfikacja poprawek N1–N7

| # | Priorytet | Obszar | Status | Weryfikacja |
|---|-----------|--------|:------:|-------------|
| N1 | 🟠 Średni | Powiadomienia (UI) | ✅ | [`notifications.js`](../web/public/js/components/notifications.js:81) — przycisk „💾 Save Configuration” wywołuje `POST /api/notifications/configure`; payload budowany z pól formularza; enum `CHANNEL_EMAIL/WEBHOOK` wysyłany jako string, potwierdzony round-trip w proxy gRPC. |
| N2 | 🟠 Średni | Powiadomienia (backend) | ✅ | [`notification_engine.cpp`](../src/notifications/notification_engine.cpp:98) — fabryka kanałów (`CHANNEL_EMAIL/WEBHOOK/MQTT` → `EmailChannel/WebhookChannel/MqttChannel`), rejestracja z proto, `clearManagedChannels()`; [`main.cpp`](../src/main.cpp:111) buduje `NotificationConfig` z pliku konfiguracyjnego na starcie (email/webhook/mqtt z `hal_cfg.notifications`). |
| N3 | 🟡 Niski | Guider/PHD2 | ✅ | [`st4_guider.cpp`](../src/controllers/st4_guider.cpp:22) — **trwałe** połączenie TCP z PHD2 (handshake `CONNECT`, wątek czytelnika wykrywający rozłączenie, `stopPHD2()` zamyka socket i dołącza wątek); [`st4_guider_service_impl.cpp`](../st4guider/src/st4_guider_service_impl.cpp:83) — `StartGuiding` zwraca `UNAVAILABLE`, gdy PHD2 nieosiągalny (wynik nie jest już ignorowany); `populateStatus()` raportuje żywy stan połączenia. |
| N4 | 🟡 Niski | Powiadomienia | ✅ | [`notification_engine.cpp`](../src/notifications/notification_engine.cpp:280) — okienne agregowanie w `workerLoop()` (merge + flush po wygaśnięciu okna); licznik `events_sent_last_hour` w [`getStatus()`](../src/notifications/notification_engine.cpp:193) oparty o rolkę `sent_timestamps_` (pruning starszych niż 1h). |
| N5 | 🟡 Niski | Powiadomienia/MQTT | ✅ | [`mqtt_channel.cpp`](../src/notifications/channels/mqtt_channel.cpp:137) — QoS 0/1/2 (PUBACK dla QoS1, PUBREC/PUBREL/PUBCOMP dla QoS2), `retain`, TLS gdy `MQTT_TLS_ENABLED` ([CMakeLists.txt](../CMakeLists.txt:151) — OpenSSL optional); **dodatkowo** naprawiono gałąź `#else` (`void*` zamiast nieistniejących `SSL*`). |
| N6 | 🟡 Niski | Integracja | ✅ | [`notifications.js`](../web/public/js/components/notifications.js:161) — `startAutoRefresh()` (co 10 s, tylko gdy panel aktywny) + odświeżenie przy kliknięciu zakładki. |
| N7 | 🟡 Niski | Pogoda | ✅ | [`weather_service_impl.cpp`](../weather/src/weather_service_impl.cpp:236) — `SubscribeWeatherAlerts` z **per-klientową** pętlą (`context->IsCancelled()`), koniec streamu tylko po rozłączeniu danego klienta; **dodatkowo** usunięto martwy kod starego watchera (`watching_`, `watch_thread_`, `alertBroadcastLoop()`). |

---

## 4. Analiza jakościowa wg obszarów

### 4.1 Rdzeń sterownika montażu — ocena: **9.5/10**

- IAU 2006 precesja/nutacja (SOFA), KF z LDLT i regularizacją S, Joseph form — bez zmian, wzorcowe.
- `TPointModel` (fit QR, korekta Newtona, R5 — mont_height skaluje refrakcję), `FieldRotationModel`, PEC (DFT per-cykl), `FocusCurve` (paraboliczny QR + hiperboliczny LM) — bez nowych usterek.
- **Usterki: brak.**

### 4.2 Powiadomienia (N1/N2/N4/N5) — ocena: **9/10** (poprzednio 6/10)

- ✅ Kanały email (SMTP/STARTTLS), webhook (HTTP+retry), MQTT (wire 3.1.1, QoS 0/1/2, retain, opcjonalny TLS) — **realne i podpięte** z konfiguracji (N2) oraz przez UI (N1).
- ✅ `NotificationEngine::configure()` tworzy kanały z proto; re-konfiguracja nie duplikuje kanałów (`clearManagedChannels()`).
- ✅ Agregacja okienna (N4) + licznik 1-godzinny (N4) — `events_sent_last_hour` odzwierciedla faktyczną wysyłkę.
- ⚠️ Pozostałe ograniczenia (świadome, nie blokujące): MQTT to „fire-and-forget” — socket per wysyłka, brak trwałego strumienia; TLS best-effort (`SSL_VERIFY_NONE`).
- Uwaga: `getStatus().configured` zwraca true, gdy `channels_` niepuste (log zawsze obecny) — interpretowane jako „skonfigurowano ≥1 kanał”.

### 4.3 Guider (N3 + integracja strumienia PHD2) — ocena: **9/10** (poprzednio 8/10)

- ✅ `startPHD2` utrzymuje **trwałe** połączenie TCP; `StartGuiding` respektuje wynik połączenia (UNAVAILABLE, gdy PHD2 nieosiągalny).
- ✅ **PHD2 może działać na innym hoście** — endpoint konfigurowalny: pola `phd2_host`/`phd2_port` w [`st4_guider.proto`](../proto/st4_guider.proto:12) (nadpisują konfigurację z [`st4_guider_config.json`](../config/st4_guider_config.json:11), fallback `localhost:4400`); [`StartGuiding`](../st4guider/src/st4_guider_service_impl.cpp:87) używa hosta z żądania lub konfiguracji; UI ([`guiderStatus.js`](../web/public/js/components/guiderStatus.js:79)) i trasa proxy ([`guider.js`](../web/proxy/routes/guider.js:97)) przekazują endpoint.
- ✅ **Pełna integracja strumienia PHD2 z pętlą prowadzenia** — [`st4_guider.cpp`](../src/controllers/st4_guider.cpp:150) parsuje wiadomości JSON-RPC (framing `Content-Length`) w `phd2ReaderLoop()` i przekazuje do `handlePhd2Event()`:
  - `GuideStep` → `RADistance`/`DecDistance` (arcsec) → [`applyGuideCorrection()`](../src/controllers/st4_guider.cpp:248) zamienia na impulsy ST4 przez HAL (kierunek E/W i N/S wg znaku korekcji, uwzględniając `invert_ra`/`invert_dec`, agresję i min/max czas impulsu);
  - `StartGuiding`/`GuideStopped`/`StarLost`/`AppState` → aktualizacja stanu `guiding_`;
  - `CalibrationComplete` → ingestia realnych współczynników `ra/dec_arcsec_per_ms` z PHD2 (`method="phd2"`).
- ✅ `St4Guider::sendPhd2Method()` — [`StartGuiding`](../st4guider/src/st4_guider_service_impl.cpp:72) wysyła `start_guiding`, `StopGuiding` wysyła `stop_guiding`.
- ✅ [`populateStatus()`](../st4guider/src/st4_guider_service_impl.cpp:180) raportuje żywy stan prowadzenia + korekcje/RMS (`rms_ra`/`rms_dec` — wykładnicze RMS w `St4Guider`).
- ✅ Parametry pętli (`aggression`, `invert_ra/dec`, `min/max_pulse_ms`) przekazywane z UI ([`guiderStatus.js`](../web/public/js/components/guiderStatus.js:91) i trasy [`guider.js`](../web/proxy/routes/guider.js:97)) do `setGuideParams()`.
- ✅ Testy: [`test_field_rotation_st4.cpp`](../tests/test_field_rotation_st4.cpp:193) — GuideStep→impulsy, dead zone min-pulse, invert RA, stany PHD2, ingestia kalibracji.
- ⚠️ Pozostałe (świadome): kalibracja in-process bez kamery prowadzącej używa współczynników teoretycznych (chyba że PHD2 przekaże `CalibrationComplete`); brak obserwacji zdarzeń `SettleDone`/`StarSelected` (nie wpływają na korekcje).

### 4.4 Pogoda (N7 + realne sterowniki cloud/GPS) — ocena: **9.5/10** (poprzednio 9/10)

- ✅ `SubscribeWeatherAlerts` — **per-klientowa** pętla (wzorzec P13); rozłączenie jednego klienta nie kończy streamu innych.
- ✅ Źródła API realne (libcurl + nlohmann/json): OpenWeatherMap, Weather.gov, IMGW (Haversine); fabryka GPIO rain/wind.
- ✅ **Realne sterowniki cloud/GPS** (zamiast samych symulacji):
  - [`Mlx90614CloudSensor`](../include/weather/sensors/cloud_sensor.h:57) — MLX90614 IR przez I²C (`/dev/i2c-N`, rejestry 0x06/0x07, `decodeTemperature` 0.02 °C/LSB); cloud cover z delty sky−ambient (mapowanie liniowe z progami `clear_delta_c`/`cloudy_delta_c`).
  - [`BoltwoodCloudSensor`](../include/weather/sensors/cloud_sensor.h:137) — Boltwood Cloud Sensor II przez RS-232 (`/dev/ttyUSB0`, 9600 baud, tolerancyjne parsowanie `CloudSkyTemp=`/`SkyTemp=`/`AmbientTemp=`).
  - [`NmeaGpsReceiver`](../include/weather/sensors/gps_receiver.h:52) — GPS NMEA 0183 przez UART/USB (parsowanie `GGA`/`RMC`, `parseCoordinate` ddmm.mmmm → °, walidacja fiksy).
  - Fabryka [`weather_factory.cpp`](../weather/src/weather_factory.cpp:94) wybiera sterownik po `driver` (mlx90614/boltwood/nmea/simulated) i uczciwie raportuje „brak czujnika”, gdy `initialize()` zawiedzie.
- ✅ Usunięto martwy kod starego watchera (N7 — sprzątanie).
- ✅ Testy: [`test_weather.cpp`](../tests/test_weather.cpp:257) — dekodowanie MLX, mapowanie cloud cover, parsowanie Boltwood, NMEA GGA/RMC, brak sprzętu → nieoperacyjne.
- ⚠️ Pozostałe (świadome): SQM (sky brightness) i ambient light na MLX/Boltwood to aproksymacje z cloud cover (brak fotodiody); wymagany prawdziwy sprzęt na I²C/serial, aby sterowniki były operacyjne.

### 4.5 Integracja (proxy + SPA) — ocena: **9.5/10**

- ✅ Każdy serwis ma dedykowany klient gRPC ([`client.js`](../web/proxy/grpc/client.js:378) — notifications wprost); brak danych symulowanych (jawny 503).
- ✅ Trasa `/api/notifications/*` zamontowana w [`server.js`](../web/proxy/server.js:119); testy proxy pokrywają 503.
- ✅ UI: wszystkie zakładki montowane; `notifications.js` — zapis konfiguracji (N1) + auto-refresh (N6); enum round-trip potwierdzony.

### 4.6 Pozostałe

- **HAL:** symulacje oznaczone; `SimulatedHAL` zwraca nullptr dla SafetyMonitor/SensorInterface (świadomie).
- **Config monitor (P10):** działa, loguje potrzebę restartu.
- **Camera/Pulley:** symulacje wyraźnie oznaczone; trasy zwracają 503 gdy nieosiągalne.

---

## 5. Pozostały dług techniczny (nie blokujący)

- Camera/Pulley — symulacje (realne HAL odroczone).
- Cloud/GPS — sterowniki realne (MLX90614 I²C, Boltwood serial, NMEA serial), ale SQM/ambient light to aproksymacje z cloud cover (brak fotodiody); działanie wymaga fizycznego sprzętu.
- PEC — trening na danych syntetycznych (brak wejścia z enkodera).
- PHD2 — połączenie i strumień zdarzeń zintegrowane; pozostaje brak ingestii zdarzeń `SettleDone`/`StarSelected` oraz obsługa „lens”/dither (nie blokujące).
- MQTT — socket per wysyłka (brak trwałego połączenia/PINGREQ), TLS best-effort (bez weryfikacji certyfikatu).
- `SimulatedHAL::createSafetyMonitor/createSensorInterface` → nullptr (świadome).
- `NotificationStatus.configured` — semantyka „≥1 kanał aktywny” (log zawsze obecny) — może wymagać doprecyzowania w UI.

---

## 6. Oceny zbiorcze

| Kategoria | Poprzednio (po R1–R8/P7/P9) | Teraz (po N1–N7 + PHD2 stream) |
|-----------|:----------:|:-----:|
| Stabilność serwisu | 9.5/10 | **9.5/10** |
| Stabilność numeryczna | 9.5/10 | **9.5/10** |
| Poprawność implementacyjna | 9/10 | **9.5/10** |
| Poprawność integracji | 9/10 | **9.5/10** |
| Kompletność UI | 9/10 | **9.5/10** |
| Powiadomienia (R1/P9 + N1/N2/N4/N5) | 6/10 | **9/10** |
| Guider (R2/P7 + N3 + PHD2 stream) | 8/10 | **9/10** |
| **Średnia (całość)** | 9.2/10 | **9.4/10** |

**Najważniejsze wnioski:**
1. N1–N7 wdrożone i zweryfikowane; build i wszystkie testy przechodzą (C++ 19/19, proxy 43/43).
2. Warstwa powiadomień przestała być „martwym kodem” — kanały są tworzone z konfiguracji (N2), a UI faktycznie zapisuje konfigurację (N1).
3. Strumień PHD2 jest w pełni zintegrowany z pętlą prowadzenia (GuideStep → korekcje ST4, stany, kalibracja z PHD2) — zamknięto najważniejszy otwarty punkt z poprzedniej iteracji.
4. Zlikwidowano też dwa ukryte defekty znalezione przy okazji: build bez OpenSSL w MQTT (N5) oraz martwy kod po N7.

---

## 7. Rekomendacje (kolejna iteracja — opcjonalne, nie blokujące)

1. MQTT — rozważyć trwałe połączenie z PINGREQ (zamiast socketu per wysyłka) oraz opcjonalną weryfikację certyfikatu TLS.
2. PHD2 — rozważyć obsługę zdarzeń `SettleDone`/`StarSelected` oraz ditheringu; opcjonalnie przesyłać korekcje też do `MountController` (korekta hybrydowa).
3. `NotificationStatus.configured` — doprecyzować semantykę (np. „skonfigurowany kanał zewnętrzny”) w statusie/UI.
4. Pogoda — dodać dedykowany czujnik SQM (fotodioda) zamiast aproksymacji sky brightness z cloud cover; Camera/Pulley — realne HAL (odroczone).

---

## 8. Pliki kluczowe po zmianach

- Rdzeń: [`main.cpp`](../src/main.cpp:111) (buildNotificationConfig), [`mount_controller.cpp`](../src/controllers/mount_controller.cpp:463), [`tpoint_model.cpp`](../src/models/tpoint_model.cpp:1226), [`focus_curve.cpp`](../src/models/focus_curve.cpp:198)
- Powiadomienia: [`notification_engine.cpp`](../src/notifications/notification_engine.cpp:98) (N2/N4), [`notification_service.cpp`](../src/notifications/notification_service.cpp:7), [`email_channel.cpp`](../src/notifications/channels/email_channel.cpp:33), [`webhook_channel.cpp`](../src/notifications/channels/webhook_channel.cpp:28), [`mqtt_channel.cpp`](../src/notifications/channels/mqtt_channel.cpp:137) (N5)
- Guider: [`st4_guider.h`](../include/controllers/st4_guider.h:22) / [`st4_guider.cpp`](../src/controllers/st4_guider.cpp:22) (N3 + pętla PHD2: `handlePhd2Event`, `applyGuideCorrection`, `sendPhd2Method`), [`st4_guider_service_impl.cpp`](../st4guider/src/st4_guider_service_impl.cpp:72) (start/stop guiding + status)
- Testy: [`test_field_rotation_st4.cpp`](../tests/test_field_rotation_st4.cpp:193) (integracja strumienia PHD2)
- Pogoda: [`weather_service_impl.cpp`](../weather/src/weather_service_impl.cpp:236) (N7), [`weather_service_impl.h`](../weather/include/weather_service_impl.h:28) (czyszczenie N7), [`cloud_sensor.h`](../include/weather/sensors/cloud_sensor.h:57) / [`cloud_sensor.cpp`](../src/weather/sensors/cloud_sensor.cpp:25) (MLX90614 + Boltwood), [`gps_receiver.h`](../include/weather/sensors/gps_receiver.h:52) / [`gps_receiver.cpp`](../src/weather/sensors/gps_receiver.cpp:25) (NMEA), [`weather_factory.cpp`](../weather/src/weather_factory.cpp:94) (selekcja sterowników)
- Proxy/UI: [`server.js`](../web/proxy/server.js:119), [`client.js`](../web/proxy/grpc/client.js:378), [`config.js`](../web/proxy/config.js:84), [`app.js`](../web/public/js/app.js:183), [`notifications.js`](../web/public/js/components/notifications.js:10) (N1/N6), [`guiderStatus.js`](../web/public/js/components/guiderStatus.js:91) + [`guider.js`](../web/proxy/routes/guider.js:97) (parametry pętli PHD2)
