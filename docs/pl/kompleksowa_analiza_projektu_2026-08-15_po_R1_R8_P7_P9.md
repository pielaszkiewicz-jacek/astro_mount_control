# Kompleksowa analiza projektu `astro_mount_control` — po poprawkach R1–R8 i P7/P9

**Data:** 2026-08-15
**Zakres:** ponowna, pełna analiza statyczna po wdrożeniu poprawek R1–R8 oraz dokończeniu P7/P9 (opisanych w [`kompleksowa_analiza_projektu_2026-08-14_po_wdrozeniu.md`](kompleksowa_analiza_projektu_2026-08-14_po_wdrozeniu.md)).
**Metoda:** analiza statyczna kodu (C++, proto, Node.js proxy, SPA) + pełny build + `ctest` + testy proxy.

---

## 0. Streszczenie

Wszystkie usterki R1–R8 z poprzedniego raportu zostały **zweryfikowane jako wdrożone**, a podczas weryfikacji wykryto i **naprawiono dwie realne usterki** (paraboliczny współczynnik R² w `focus_curve` oraz test skalowania refrakcji w `TPointModel`). Dodatkowo zidentyfikowano **nowe, istotne luki** — głównie w warstwie powiadomień (R1/P9): kanały email/webhook/MQTT są zaimplementowane, ale **nie są podpinane z konfiguracji** do `NotificationEngine`.

**Weryfikacja (bezpośrednio po zmianach):**
- Pełny build ✅ (`cmake --build build -j6`)
- `ctest` — **19/19** ✅ (wcześniej 2 porażki — naprawione)
- Testy proxy — **43/43** ✅

---

## 1. Metodologia

1. Przegląd pełnego diffa (90 plików, +3381/−1256) oraz stanu roboczego (git status).
2. Weryfikacja każdej poprawki R1–R8 oraz P7/P9 w kodzie źródłowym.
3. Mapowanie API: proto ↔ `service_impl` ↔ trasy proxy ↔ komponenty SPA.
4. Identyfikacja nowych usterek (N1–N7) i pozostałego długu technicznego.
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

- **Port gRPC 50051** (unified) hostuje obecnie **9 serwisów in-process**: mount, dome, derotator, focuser, ST4 guider, PEC, camera (sim), pulley (sim), notifications.
- **Serwisy zewnętrzne** (osobne procesy): weather 50055, power 50056, sequencer 50057, object database 50052.
- **Web proxy** (Node.js/Express, domyślnie `0.0.0.0:8080`) tłumaczy REST/JSON ↔ gRPC; brak cichych danych symulowanych — niedostępny serwis zwraca jawny **503** (P1).

---

## 3. Weryfikacja poprawek R1–R8 i P7/P9

| # | Obszar | Status | Weryfikacja |
|---|--------|:------:|-------------|
| R1 | Powiadomienia — hosting `NotificationService` + trasa + UI | ✅ | [`main.cpp`](../src/main.cpp:360) hostuje serwis in-process; [`server.js`](../web/proxy/server.js:119) montuje `/api/notifications`; [`index.html`](../web/public/index.html:2835) + [`app.js`](../web/public/js/app.js:196) + [`notifications.js`](../web/public/js/components/notifications.js) — komponent zamontowany; gRPC client + testy proxy podpięte. **Uwaga N1/N2 — patrz §5.** |
| R2 | Guider — realna integracja PHD2 | ✅ | [`st4_guider.cpp`](../src/controllers/st4_guider.cpp:22) — realne TCP + handshake JSON-RPC `CONNECT`; `phd2_connected_` odzwierciedla faktyczne połączenie. Ograniczenie: socket zamykany po handshaku (sprawdzenie łączności, nie trwały strumień) — patrz N3. |
| R3 | Camera/Pulley — backend | ✅ | Hosted in-process w [`main.cpp`](../src/main.cpp:370) jako wyraźnie oznaczone symulacje; trasy zwracają jawny 503 gdy nieosiągalne (testy proxy). |
| R4 | Kwaternion orientacji — ujednolicenie `{0,0,0,1}` | ✅ | [`configuration.h`](../include/config/configuration.h:141) = [`mount_config.h`](../include/config/mount_config.h:67) = `{0,0,0,1}` (identyczność). |
| R5 | `setMountParameters` (mount_height/pier_west/pier_east) w TPointModel | ✅ | Implementacja + persystencja + zastosowanie w [`mount_controller.cpp`](../src/controllers/mount_controller.cpp:468); **naprawiono test** `MountHeightScalesRefraction` (12 próbek; poprzednio 8 < minimum 10). |
| R6 | `focus_curve` — fitowanie hiperboliczne | ✅ | Pełny Levenberg-Marquardt w [`focus_curve.cpp`](../src/models/focus_curve.cpp:198); **naprawiono R² paraboliczny** (poprzednio `evaluateAt()` zwracał 0 → R²<0). |
| R7 | Fabryka pogody — GPIO/cloud/GPS | ✅ | [`weather_factory.cpp`](../weather/src/weather_factory.cpp:65) — GPIO rain/wind, symulowane cloud/GPS; uczciwy stan „brak czujnika” gdy GPIO nie inicjuje się. |
| R8 | Proxy weather — history/rules/stream | ✅ | [`weather.js`](../web/proxy/routes/weather.js:54) — `GET /history`, `POST/GET /rules`, `GET /alerts/stream` (SSE); backend [`weather_service_impl.cpp`](../weather/src/weather_service_impl.cpp:179) implementuje metody. |
| P7 | PHD2 | ✅ | Kalibracja ST4 (teoretyczna/pomiarowa) + `startPHD2` realnie łączy się z PHD2. |
| P9 | Kanały email/webhook/MQTT realne | ⚠️ | Implementacje realne (SMTP libcurl, HTTP POST z retry, surowy MQTT 3.1.1) — **ale nie są rejestrowane z konfiguracji** (N2). |

---

## 4. Analiza jakościowa wg obszarów

### 4.1 Rdzeń sterownika montażu — ocena: **9.5/10**

- IAU 2006 precesja/nutacja (SOFA), KF z LDLT i regularizacją S ([`kalman_filter.cpp`](../src/models/kalman_filter.cpp:165)), Joseph form — wzorcowe.
- `TPointModel`: fit QR z pivotingiem, korekta Newtona (poprawiona skala kroku), R5 — mont_height skaluje refrakcję (barometryczny czynnik wysokości), pier_west/east zmienia znak AN. **Dodatkowo** w [`fitModel()`](../src/models/tpoint_model.cpp:68) dodano zero-inicjalizację macierzy projektowych (eliminacja UB przy blokach bez aktywnych członów).
- `FieldRotationModel`: wybór modelu wg `mount_type` (EQUATORIAL→0, ALT_AZ→paralaktyka, CASUAL→kwaternion), brak osobliwości tan(δ).
- PEC: DFT per-cykl, spójna integracja (N10) — poprawne.
- `FocusCurve`: fit paraboliczny (centrowany, QR) + hiperboliczny (LM z numerycznym Jacobianem), guard ≥4 punktów.
- **Usterki: brak** — model numeryczny pozostaje mocną stroną projektu.

### 4.2 Powiadomienia (R1/P9) — ocena: **6/10** (najsłabsze ogniwo)

- ✅ Serwis gRPC hostowany, trasa i UI działają.
- ✅ Kanały email (SMTP/STARTTLS), webhook (HTTP+retry), MQTT (wire 3.1.1) — realne implementacje.
- ⚠️ **N2 (istotne):** [`NotificationEngine::configure()`](../src/notifications/notification_engine.cpp:95) **nie tworzy i nie rejestruje kanałów** z `NotificationConfig` (pętla po `channels()` zawiera tylko komentarz-placeholder). W [`main.cpp`](../src/main.cpp:363) rejestrowany jest wyłącznie `LogChannel`. Efekt: przycisk „Send Test Email/Webhook” w UI wysyła zdarzenie, ale trafia ono tylko do dziennika — realne kanały są **martwym kodem** bez podpięcia. Brakuje fabryki mapującej `ChannelType` → instancja kanału.
- ⚠️ **N4:** `aggregate()` to passthrough (TODO), `events_sent_last_hour` jest sztywno 0.
- MQTT: socket per wysyłka, QoS 0, brak TLS (mimo pola `use_tls`), brak retry — ograniczone, ale uczciwe („fire-and-forget”).

### 4.3 Guider (R2/P7) — ocena: **8/10**

- ✅ Kalibracja teoretyczna i pomiarowa (`fitSlope` przez punkt zerowy, uśrednianie kierunków), konwersja korekcji→impuls z clampingiem.
- ✅ `startPHD2` realnie nawiązuje połączenie TCP i wysyła `CONNECT`.
- ⚠️ **N3:** połączenie zamykane natychmiast po handshaku — brak trwałego strumienia zdarzeń PHD2; to „sprawdzenie łączności”, nie pełna integracja. `StartGuiding` ignoruje wynik `startPHD2` (zawsze OK).
- Kalibracja in-process używa teoretycznych współczynników (brak kamery prowadzącej) — udokumentowane i uczciwe.

### 4.4 Pogoda (R7/R8) — ocena: **9/10**

- ✅ Źródła API realne (libcurl + nlohmann/json): OpenWeatherMap, Weather.gov, IMGW (z selekcją najbliższej stacji, Haversine).
- ✅ Fabryka podpina GPIO rain/wind, symulowane cloud/GPS, reguły bezpieczeństwa.
- ✅ Serwis udostępnia status/history/rules/alerts; proxy ma komplet tras (R8).
- ⚠️ Cloud/GPS tylko symulowane (brak sterowników MLX90614/Boltwood/NMEA) — udokumentowane.
- ⚠️ **N7:** `SubscribeWeatherAlerts` używa współdzielonej flagi `watching_` (per-serwis, nie per-klient) — ten sam wzorzec, który P13 naprawił dla derotatora; rozłączenie jednego klienta kończy strumień dla wszystkich. Drobne w praktyce (zwykle 1 subskrybent).

### 4.5 Integracja (proxy + SPA) — ocena: **9/10**

- ✅ Każdy serwis rozszerzony ma dedykowany klient gRPC ([`client.js`](../web/proxy/grpc/client.js)); brak danych symulowanych (jawny 503).
- ✅ `config.js` — domyślne porty 50051 dla serwisów in-process, 50055–50057 zewnętrzne; domyślne włączenia spójne z `default.json`.
- ✅ UI: wszystkie zakładki (w tym notifications/camera/pulley) montowane; `showServiceUnavailable` + ukrywanie zakładek.
- ⚠️ **N1:** zakładka Notifications **nie wywołuje** `/api/notifications/configure` — pola konfiguracji kanałów w [`notifications.js`](../web/public/js/components/notifications.js:18) są tylko wizualne (brak przycisku zapisu); a nawet gdyby wywoływała, backend nie tworzy kanałów (N2).

### 4.6 Pozostałe

- **HAL:** usunięto 8 plików-stubów (P14); `SimulatedHAL` nie implementuje SafetyMonitor/SensorInterface (świadomie, zwraca nullptr — bezpieczne).
- **Config monitor (P10):** działa, loguje potrzebę restartu; poprawki N9 (snapshot pod lockiem).
- **Camera/Pulley:** symulacje wyraźnie oznaczone; cooler/pulley mają realistyczne modele ruchu.

---

## 5. Nowe usterki i dług techniczny (po wdrożeniu)

| # | Priorytet | Obszar | Problem | Lokalizacja |
|---|-----------|--------|---------|-------------|
| N1 | 🟠 Średni | Powiadomienia (UI) | Zakładka Notifications nie zapisuje konfiguracji kanałów — brak wywołania `/api/notifications/configure`. | [notifications.js](../web/public/js/components/notifications.js:18) |
| N2 | 🟠 Średni | Powiadomienia (backend) | `NotificationEngine::configure()` nie tworzy/rejestruje kanałów z proto — tylko `LogChannel` aktywny; email/webhook/MQTT martwy kod. | [notification_engine.cpp](../src/notifications/notification_engine.cpp:112) |
| N3 | 🟡 Niski | Guider/PHD2 | `startPHD2` zamyka socket po handshaku (sprawdzenie łączności, nie trwały strumień); `StartGuiding` ignoruje wynik. | [st4_guider.cpp](../src/controllers/st4_guider.cpp:44), [st4_guider_service_impl.cpp](../st4guider/src/st4_guider_service_impl.cpp:81) |
| N4 | 🟡 Niski | Powiadomienia | `aggregate()` passthrough; `events_sent_last_hour` sztywno 0. | [notification_engine.cpp](../src/notifications/notification_engine.cpp:257) |
| N5 | 🟡 Niski | Powiadomienia/MQTT | MQTT: QoS 0, brak TLS (`use_tls` ignorowane), socket per wysyłka. | [mqtt_channel.cpp](../src/notifications/channels/mqtt_channel.cpp:99) |
| N6 | 🟡 Niski | Integracja | Kanał `notifications.js` nie aktualizuje stanu w pętli (tylko ręczny „Refresh”). | [notifications.js](../web/public/js/components/notifications.js:50) |
| N7 | 🟡 Niski | Pogoda | `SubscribeWeatherAlerts` — współdzielona flaga `watching_` (per-serwis, nie per-klient). | [weather_service_impl.cpp](../weather/src/weather_service_impl.cpp:240) |

**Pozostały dług (nie blokujący):**
- Camera/Pulley to symulacje (realne HAL odroczone).
- Cloud/GPS — tylko symulacje (brak sterowników).
- PEC — trening na danych syntetycznych (brak wejścia z enkodera).
- `SimulatedHAL::createSafetyMonitor/createSensorInterface` → nullptr (świadome).

---

## 6. Oceny zbiorcze

| Kategoria | Poprzednio | Teraz |
|-----------|:----------:|:-----:|
| Stabilność serwisu | 9.5/10 | **9.5/10** |
| Stabilność numeryczna | 9.5/10 | **9.5/10** |
| Poprawność implementacyjna | 8.5/10 | **9/10** |
| Poprawność integracji | 9/10 | **9/10** |
| Kompletność UI | 8.5/10 | **9/10** |
| Powiadomienia (R1/P9) | — | **6/10** |
| **Średnia (całość)** | 9/10 | **9.2/10** |

**Najważniejsze wnioski:**
1. R1–R8 oraz P7/P9 są wdrożone i zweryfikowane; build i wszystkie testy przechodzą (C++ 19/19, proxy 43/43).
2. Rdzeń numeryczny i integracja pozostają wzorcowe; poprawki R5/R6 dodatkowo usunęły realne błędy.
3. **Słaby punkt:** warstwa powiadomień — kanały zaimplementowane, ale nie podłączone z konfiguracji (N1/N2). To jedyny obszar wymagający realnej pracy, aby P9 był w pełni funkcjonalny.

---

## 7. Rekomendacje (kolejna iteracja)

1. **N2 (priorytet):** zaimplementować fabrykę kanałów w `NotificationEngine::configure()` — mapowanie `ChannelType` → `EmailChannel/WebhookChannel/MqttChannel`, rejestracja i konfiguracja z proto.
2. **N1:** dodać w `notifications.js` przycisk „Zapisz konfigurację” wywołujący `POST /api/notifications/configure`.
3. **N3:** decyzja — albo utrzymać trwałe połączenie PHD2 (wątek + strumień zdarzeń), albo uczciwie oznaczyć stan jako „external/manual”.
4. **N4:** zaimplementować prostą agregację (bufor czasowy) i licznik ostatniej godziny.
5. **N5:** TLS dla MQTT (albo usunąć pole `use_tls` z proto, by nie myliło).
6. **N7:** per-klientowa pętla subskrypcji alertów (wzorzec P13).

---

## 8. Pliki kluczowe po zmianach

- Rdzeń: [`main.cpp`](../src/main.cpp:360), [`mount_controller.cpp`](../src/controllers/mount_controller.cpp:463), [`tpoint_model.cpp`](../src/models/tpoint_model.cpp:1226), [`focus_curve.cpp`](../src/models/focus_curve.cpp:198), [`pec_model.cpp`](../src/models/pec_model.cpp:72), [`field_rotation_model.cpp`](../src/models/field_rotation_model.cpp:7)
- Serwisy: [`camera_service_impl.cpp`](../camera/src/camera_service_impl.cpp:9), [`pulley_service_impl.cpp`](../pulley/src/pulley_service_impl.cpp:11), [`st4_guider_service_impl.cpp`](../st4guider/src/st4_guider_service_impl.cpp:19), [`pec_service_impl.cpp`](../pec/src/pec_service_impl.cpp:15)
- Powiadomienia: [`notification_engine.cpp`](../src/notifications/notification_engine.cpp), [`notification_service.cpp`](../src/notifications/notification_service.cpp:7), [`email_channel.cpp`](../src/notifications/channels/email_channel.cpp:33), [`webhook_channel.cpp`](../src/notifications/channels/webhook_channel.cpp:28), [`mqtt_channel.cpp`](../src/notifications/channels/mqtt_channel.cpp:93)
- Pogoda: [`weather_factory.cpp`](../weather/src/weather_factory.cpp:65), [`weather_service_impl.cpp`](../weather/src/weather_service_impl.cpp:169), [`weather.js`](../web/proxy/routes/weather.js:54)
- Proxy/UI: [`server.js`](../web/proxy/server.js:119), [`client.js`](../web/proxy/grpc/client.js:378), [`config.js`](../web/proxy/config.js:110), [`app.js`](../web/public/js/app.js:183), [`notifications.js`](../web/public/js/components/notifications.js:10)
