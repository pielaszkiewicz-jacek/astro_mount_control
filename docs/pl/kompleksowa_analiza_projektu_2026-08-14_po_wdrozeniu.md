# Kompleksowa analiza projektu `astro_mount_control` — po wdrożeniu Faz 0–6

**Data:** 2026-08-14 (po wdrożeniu)
**Zakres:** ponowna, pełna analiza statyczna kodu po implementacji Faz 0–6 z [`kompleksowa_analiza_projektu_2026-08-14.md`](kompleksowa_analiza_projektu_2026-08-14.md).
**Metoda:** analiza statyczna (C++, proto, Node.js proxy, SPA) + weryfikacja poprawek P1–P15 + uruchomienie pełnego builda i testów.
**Raporty faz:** [Faza 0](raport_faza_0_2026-08-14.md) · [1](raport_faza_1_2026-08-14.md) · [2](raport_faza_2_2026-08-14.md) · [3](raport_faza_3_2026-08-14.md) · [4](raport_faza_4_2026-08-14.md) · [5](raport_faza_5_2026-08-14.md) · [6](raport_faza_6_2026-08-14.md)

---

## 0. Streszczenie — kluczowe ustalenia po wdrożeniu

Wszystkie krytyczne i większość średnich usterek z poprzedniego raportu zostały **naprawione i zweryfikowane**. Rdzeń montażu pozostaje wzorowy; warstwa integracji web-proxy, która była najsłabszym ogniwem (5/10), została **gruntownie naprawiona** (P1–P4). Dodano **dwa nowe realne serwisy in-process** (ST4 guider, PEC — P2), poprawiono derotator (P5/P6), źródła pogody i kanały powiadomień (P8/P9), stabilność (P10/P12/P13) i porządki (P14/P15).

**Weryfikacja:** pełny build ✅ · `ctest` **18/18** ✅ · testy proxy **32/32** ✅ · testy live (emulacja) ✅.

**Pozostałe usterki (niewielkie):**

| # | Priorytet | Obszar | Problem | Lokalizacja |
|---|-----------|--------|---------|-------------|
| R1 | 🟠 Średni | Powiadomienia (P9, część) | Kanały email/webhook/MQTT realne, ale **`NotificationService` (gRPC) nadal niehostowana**; brak trasy `/api/notifications`; komponent `notifications.js` nie jest montowany w UI. | [notification.proto:17](../proto/notification.proto:17), [server.js](../web/proxy/server.js:78), [app.js](../web/public/js/app.js:145) |
| R2 | 🟠 Średni | Guider (P7) | `St4Guider::startPHD2()` to nadal stub logiczny (ustawia `phd2_connected_=true` bez połączenia socketowego). Kalibracja ST4 działa (teoretyczna/pomiarowa). | [st4_guider.cpp:12](../src/controllers/st4_guider.cpp:12) |
| R3 | 🟡 Niski | Integracja (P2, część) | `CameraService`/`PulleyService` wciąż bez backendu — zakładki ukryte, trasy 503 „not implemented”. | [camera.proto:16](../proto/camera.proto:16), [pulley.proto:17](../proto/pulley.proto:17) |
| R4 | 🟡 Niski | Konfig | Niespójny **domyślny kwaternion orientacji**: `Configuration::orientation_quaternion{1,0,0,0}` vs `MountConfig::quaternion{0,0,0,1}` (identyczność). Domyślny ładunek z `getDefault()` jest poprawny, ale domyślna wartość pola struktury nie. | [configuration.h:135](../include/config/configuration.h:135), [mount_config.h:67](../include/config/mount_config.h:67) |
| R5 | 🟡 Niski | Poprawność | FIXME `setMountParameters` (mount_height/pier_west/pier_east) w TPointModel — nieużywane. | [mount_controller.cpp:463](../src/controllers/mount_controller.cpp:463) |
| R6 | 🟡 Niski | Poprawność | `focus_curve` — fallback paraboliczny (brak fitowania hiperbolicznego). | [focus_curve.cpp:178](../src/models/focus_curve.cpp:178) |
| R7 | 🟡 Niski | Pogoda | Fabryka pogody łączy tylko `api_source` + reguły; czujniki GPIO/cloud/GPS nie są instancjonowane (symulowane domyślnie). | [weather_factory.cpp:38](../weather/src/weather_factory.cpp:38) |
| R8 | 🟡 Niski | Proxy | Trasa weather wystawia tylko `GET /status` (brak history/rules/stream alertów). | [weather.js](../web/proxy/routes/weather.js) |

---

## 1. Metodologia

1. Pełny build + testy: `ctest` (18/18), proxy `npm test` (32/32).
2. Weryfikacja każdej poprawki P1–P15 w kodzie.
3. Mapowanie API: proto ↔ `service_impl` ↔ trasy proxy ↔ komponenty SPA.
4. Identyfikacja pozostałych i nowych usterek (R1–R8).
5. Testy live w trybie emulacji (kontroler + proxy).

---

## 2. Weryfikacja poprawek z poprzedniego raportu (P1–P15)

| # | Poprawka | Status | Weryfikacja |
|---|----------|:------:|-------------|
| P1 | Proxy: trasy serwisów rozszerzonych → dedykowane klienty, **koniec danych symulowanych** (jawny 503) | ✅ | [`client.js`](../web/proxy/grpc/client.js:281), [`weather.js`](../web/proxy/routes/weather.js:22) |
| P2 | Hosting `St4GuiderService` i `PecService` in-process (50051) | ✅/⏸ | [`st4_guider_service_impl.cpp`](../st4guider/src/st4_guider_service_impl.cpp), [`pec_service_impl.cpp`](../pec/src/pec_service_impl.cpp); camera/pulley odroczone |
| P3 | Klient focuser w proxy (in-process 50051) | ✅ | [`client.js`](../web/proxy/grpc/client.js:350) |
| P4 | Ujednolicone domyślne włączenia `external_services` (config ↔ proxy) | ✅ | [`default.json`](../config/default.json:431), [`config.js`](../web/proxy/config.js:76) |
| P5 | Derotator: model field-rotation wg `mount_type` (EQUATORIAL→0) | ✅ | [`derotator_controller.cpp`](../src/controllers/derotator_controller.cpp:33), test P5 |
| P6 | TMC5160: integracja pozycji (VACTUAL·dt / slew do XTARGET) | ✅ | [`derotator_tmc5160.h`](../include/hal/derotator_tmc5160.h), test P6 |
| P7 | PHD2 | ⚠️ | kalibracja ST4 działa; `startPHD2` stub logiczny (R2) |
| P8 | Pogoda: `httpGet` libcurl + parsowanie JSON; GPIO sysfs „brak czujnika” | ✅ | [`http_client.cpp`](../src/http_client.cpp), [`imgw_source.cpp`](../src/weather/sources/imgw_source.cpp), [`wind_sensor.cpp`](../src/weather/sensors/wind_sensor.cpp) |
| P9 | Kanały email/webhook/MQTT realne (SMTP/HTTP/MQTT) | ✅/⚠️ | [`email_channel.cpp`](../src/notifications/channels/email_channel.cpp), [`webhook_channel.cpp`](../src/notifications/channels/webhook_channel.cpp), [`mqtt_channel.cpp`](../src/notifications/channels/mqtt_channel.cpp); hosting serwisu — R1 |
| P10 | `ConfigMonitor` podpięty do `main.cpp` (log zmiany + wymóg restartu) | ✅ | [`main.cpp`](../src/main.cpp:135), zweryfikowane live |
| P11 | `focuser_config.json` | ✅ | [`focuser_config.json`](../config/focuser_config.json) |
| P12 | `power_stub` — spójne SSL | ✅ | [`main.cpp`](../src/main.cpp:407) |
| P13 | `WatchStatus` per-klient (pętla lokalna, bez `watching_`) | ✅ | [`derotator_service_impl.cpp`](../derotator/src/derotator_service_impl.cpp:143) |
| P14 | Stuby HAL usunięte (8 plików) | ✅ | [`CMakeLists.txt`](../CMakeLists.txt:237) |
| P15 | Porządki: `alt_true`, `setPositionCallback` (raz), `gui/README`, decyzja o snapshotach | ✅ | [`field_rotation_model.cpp`](../src/models/field_rotation_model.cpp), [`gui/README.md`](../gui/README.md) |

---

## 3. Stabilność serwisu — ocena: **9.5/10** (było 9/10)

- Watchdog trackingu i slew nadal poprawne; teardown jawny i sekwencyjny.
- **P13** usunął konflikt współdzielonego `WatchStatus` (per-klientowa pętla).
- **P10** — `ConfigMonitor` działa (wątek monitorujący, bezpieczne zamykanie).
- Nowe serwisy in-process (st4_guider, pec) rejestrowane i czyszczone w teardown (normalna ścieżka i wyjątków).
- Drobne uwagi: MQTT/socket raw tworzy gniazdo per wysyłka (brak trwałego połączenia); `WatchState` montażu nadal `sleep_for(100 ms)` bez guarda (dopuszczalne).

## 4. Stabilność numeryczna — ocena: **9.5/10** (było 9/10)

- IAU 2006 precesja/nutacja, KF z Joseph form/LDLT — bez zmian (wzorcowe).
- **P5** — wybór modelu field-rotation wg `mount_type` (EQUATORIAL→0, ALT_AZ→paralaktyka, CASUAL→kwaternion) — poprawny.
- **P6** — integracja pozycji TMC5160 z guardem dużych odstępów czasu (>2 s).
- PEC: DFT per-cykl z koherentną integracją (N10) — bez zmian.
- Uwaga (R4): domyślny kwaternion `{1,0,0,0}` w `configuration.h` vs `{0,0,0,1}` w `mount_config.h` — warto ujednolicić.

## 5. Poprawność implementacyjna — ocena: **8.5/10** (było 7/10)

- **Zrealizowane nowe moduły:** ST4 guider (kalibracja + impulsy), PEC (trening syntetyczny + DFT + persystencja), derotator P5/P6, źródła pogody (HTTP+JSON), kanały powiadomień (SMTP/HTTP/MQTT), sysfs GPIO.
- **Usunięto stuby:** 8 plików HAL (martwy kod) — P14.
- **Pozostałe:** R1 (hosting NotificationService), R2 (PHD2), R3 (camera/pulley), R5 (setMountParameters), R6 (focus_curve hiperboliczny), R7 (fabryka pogody nie łączy czujników GPIO).

## 6. Poprawność integracji — ocena: **9/10** (było 5/10)

- **P1/P3/P4 zrealizowane:** każdy serwis rozszerzony ma dedykowany klient gRPC; brak danych symulowanych (jawne 503); focuser in-process; domyślne włączenia ujednolicone.
- **P2:** st4_guider i pec hostowane in-process na 50051 i podpięte w proxy + UI.
- **P8/P9:** źródła pogody realne (libcurl); kanały powiadomień realne.
- **Pozostałe:** R1 (brak trasy `/api/notifications` i montażu komponentu), R3 (camera/pulley odroczone, 503), R8 (trasa weather tylko `/status`).

## 7. Kompletność UI — ocena: **8.5/10** (było 6/10)

| Zakładka | Backend | UI działa? |
|----------|---------|:----------:|
| Status/Control/Calibration/Tracking/Logging/Database/Tests | mount ✅ | ✅ |
| Dome | in-process ✅ | ✅ |
| Derotator | in-process ✅ (P5/P6) | ✅ |
| Focuser | in-process ✅ | ✅ |
| Guider | in-process ✅ (P2) | ✅ |
| PEC | in-process ✅ (P2) | ✅ |
| Weather/Power/Sequencer | osobne procesy, trasy podpięte | ✅ (dane z backendu; 503 gdy brak) |
| Camera/Pulley | brak backendu (P2) | ⏸ zakładki ukryte, 503 |
| Notifications | niehostowany (R1) | ❌ komponent nie montowany |
| LX200 | mount ✅ | ✅ |

- Wskaźnik „service unavailable” (`App.showServiceUnavailable`) + ukrywanie zakładek odroczonych — dodane w Fazie 1/2.
- Pozostałe: R1 (notifications), R8 (brak podstron weather history/rules w UI).

---

## 8. Pełna mapa usterek (po wdrożeniu)

### Średnie
- **R1** — hosting `NotificationService` (gRPC) + trasa `/api/notifications` + montaż komponentu (kanały już realne).
- **R2** — realna integracja PHD2 (lub uczciwe oznaczenie „zewnętrzna”).

### Niskie / porządkowe
- **R3** — camera/pulley po dostępności HAL (zakładki ukryte).
- **R4** — ujednolicenie domyślnego kwaternionu (`{0,0,0,1}`).
- **R5** — `setMountParameters`/pola pier w TPointModel.
- **R6** — fitowanie hiperboliczne w `focus_curve`.
- **R7** — fabryka pogody: łączenie czujników GPIO/cloud/GPS.
- **R8** — proxy weather: history/rules/stream.

---

## 9. Podsumowanie ocen

| Kategoria | Przed wdrożeniem | Po wdrożeniu |
|-----------|:----------------:|:------------:|
| Stabilność serwisu | 9/10 | **9.5/10** |
| Stabilność numeryczna | 9/10 | **9.5/10** |
| Poprawność implementacyjna | 7/10 | **8.5/10** |
| Poprawność integracji | 5/10 | **9/10** |
| Kompletność UI | 6/10 | **8.5/10** |
| **Średnia (rdzeń)** | 9/10 | **9.5/10** |
| **Średnia (całość)** | 7/10 | **9/10** |

**Najważniejszy wniosek:** plan wdrożeniowy (Fazy 0–6) został zrealizowany. Najpoważniejszy problem — fałszywe dane symulowane w web UI (P1) — **usunięty**. Pozostały głównie usterki niskiego priorytetu oraz świadomie odroczone elementy (camera/pulley, hosting powiadomień R1, PHD2 R2), które można wdrożyć jako kolejne iteracje bez zmian architektonicznych.
