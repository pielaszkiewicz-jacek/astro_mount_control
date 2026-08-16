# Kompleksowa analiza projektu `astro_mount_control`

**Data analizy:** 2026-08-11
**Zakres:** stabilność i poprawność, poprawność architektury, poprawność numeryczna, kompletność funkcjonalna, łatwość użytkowania (perspektywa użytkownika końcowego).
**Metoda:** analiza statyczna kodu źródłowego + uruchomienie testów jednostkowych + weryfikacja dokumentacji względem implementacji.
**Werifikacja testów (wykonana):** `test_astronomical_calculations` 22/22 ✅, `test_tpoint_model` 15/15 ✅, `test_kalman_filter` 22/22 ✅, `test_configuration` 26/26 ✅, `test_mount_controller` 128/128 ✅, `test_ephemeris_tracker` 68/68 ✅, `test_subarcsecond_accuracy` ✅, `test_watchdog` 11/11 ✅.

---

## ⚙️ Status poprawek N1–N10 (2026-08-11)

Wszystkie 10 rekomendowanych poprawek zostało **wdrożonych**. Weryfikacja: pełny build (`cmake` + `make -j`) bez błędów oraz wszystkie testy jednostkowe przeszły (m.in. `test_config_monitor` 21/21, `test_grpc_integration` 44/44).

| # | Poprawka | Status | Lokalizacja |
|---|----------|--------|-------------|
| N1 | `WatchState`: pola `tracking_rate_ra/dec` = `axis1/2_rate·3600` (nie `tracking_error`) | ✅ wdrożone | [service_impl.cpp:187](../src/api/service_impl.cpp:187) |
| N2 | Usunięto duplikat `proto/dome.proto` i `proto/derotator.proto` (jeden źródłowy proto na usługę w podkatalogach) | ✅ wdrożone | [dome/proto/dome.proto](../dome/proto/dome.proto), [derotator/proto/derotator.proto](../derotator/proto/derotator.proto) |
| N3 | Derotator: realne `ha_hours`/`dec_deg` (LST−RA, Dec) zamiast 0.0 | ✅ wdrożone | [main.cpp:428](../src/main.cpp:428) |
| N4 | Dome: prawdziwy azymut z `equatorialToHorizontal` (nie oś1/HA) | ✅ wdrożone | [main.cpp:415](../src/main.cpp:415) |
| N5 | Web proxy: domyślne porty dome/derotator = 50051 (in-process) | ✅ wdrożone | [config.js:24](../web/proxy/config.js:24), [.env.example](../web/proxy/.env.example) |
| N6 | Dokumentacja GUI Qt oznaczona jako niedostępna | ✅ wdrożone | [qt_gui.md](../docs/en/qt_gui.md), [index.md](../docs/en/index.md), [architecture.md](../docs/en/architecture.md), [installation.md](../docs/en/installation.md), [wsl_build_guide.md](../docs/en/wsl_build_guide.md), [index.md](../docs/pl/index.md) |
| N7 | Dokumentacja CANopen HAL ujednolicona ze stanem (niezaimplementowany; CAN = MF7025v2) | ✅ wdrożone | [VERIFICATION_REPORT.md](../VERIFICATION_REPORT.md), [hal_layer.md](../docs/en/hal_layer.md), [hal_layer.md](../docs/pl/hal_layer.md), [architecture.md](../docs/pl/architecture.md) |
| N8 | `WatchState`: wyrównany zestaw pól z `GetState` (actual rates, pier/meridian, target) | ✅ wdrożone | [service_impl.cpp:175](../src/api/service_impl.cpp:175) |
| N9 | `ConfigMonitor`/`ConfigManager`: `getConfiguration()` zwraca kopię pod lockiem; callback z snapshotem | ✅ wdrożone | [config_monitor.cpp:56](../src/config/config_monitor.cpp:56), [:90](../src/config/config_monitor.cpp:90) |
| N10 | PEC `performFFT`: DFT per-cykl (koherentna integracja 3 cykli), ortogonalna baza, poprawna faza | ✅ wdrożone | [pec_model.cpp:72](../src/models/pec_model.cpp:72) |

---

## 0. Streszczenie

Projekt jest **dojrzały i generalnie dobrze zaprojektowany** — ma przejrzystą architekturę warstwową, silne zabezpieczenia numeryczne (strażniki NaN/Inf, formy Joseph, dekompozycje LDLT/QR/SVD), poprawną obsługę wątków i solidny zestaw testów. Wcześniejsze błędy poprawności opisane w [`analiza_stabilnosci_numerycznej.md`](analiza_stabilnosci_numerycznej.md) (B1–B8, R1–R3, R7) są **potwierdzone jako naprawione** w bieżącym kodzie.

Analiza ujawnia jednak **kilka nowych / pominiętych problemów**, głównie w warstwie API i integracji serwisów:

| # | Priorytet | Obszar | Problem | Lokalizacja |
|---|-----------|--------|---------|-------------|
| N1 | 🔴 Wysoki | API | `WatchState` wpisuje **błąd śledzenia** do pól **szybkości śledzenia** (niezgodne z `GetState`) | [service_impl.cpp:187](../src/api/service_impl.cpp:187) |
| N2 | 🟠 Średni | Architektura | **Duplikat protokołu** `dome.proto` — dwie różne definicje usługi w `proto/` i `dome/proto/`; kompilowane obie, różnią się API | [proto/dome.proto](../proto/dome.proto), [dome/proto/dome.proto](../dome/proto/dome.proto) |
| N3 | 🟠 Średni | Integracja | Derotator w `main()` dostaje **zahardkodowane 0.0** dla `ha_hours`/`dec_deg` — pole rotation liczony z bezsensownych danych | [main.cpp:429](../src/main.cpp:429) |
| N4 | 🟠 Średni | Integracja | Dome w `main()` dostaje azymut z **osi1** (`telescope_axis1_position`) — dla montażu równikowego to HA, nie azymut | [main.cpp:419](../src/main.cpp:419) |
| N5 | 🟠 Średni | Web proxy | Domyślne porty dome/derotator (50053/50054) nie pokrywają się z hostingiem **in-process** (50051) — web UI dome/derotator nie zadziała bez osobnych serwerów | [config.js:24](../web/proxy/config.js:24), [main.cpp:310](../src/main.cpp:310) |
| N6 | 🟡 Niski | Dokumentacja | GUI Qt (Qt Native GUI) **usunięte z repozytorium** (commit `3daf3f9`), ale dokumentacja nadal je opisuje | [qt_gui.md](../docs/en/qt_gui.md) |
| N7 | 🟡 Niski | Dokumentacja | CANopen HAL zgłasza „not yet available” w `HALFactory`, podczas gdy dokumentacja (raport weryfikacji) twierdzi, że jest wdrożony | [hal_factory.cpp:22](../src/hal/hal_factory.cpp:22) |
| N8 | 🟡 Niski | API | `WatchState` nie eksponuje `actual_rate_axis1/2`, celu śledzenia, `meridian_flipped` (mniejszy zestaw niż `GetState`) | [service_impl.cpp:175](../src/api/service_impl.cpp:175) |
| N9 | 🟡 Niski | Config | `ConfigMonitor::getConfiguration()` zwraca referencję do wewnętrznego `config_` poza blokadą — ryzyko data race przy równoległym reload | [config_monitor.cpp:56](../src/config/config_monitor.cpp:56) |
| N10 | 🟡 Niski | Numeryka | PEC `performFFT` sumuje harmoniczne po 3-cyklowym oknie z okresem 1 cyklu → wyciek spektralny dla k∤3 | [pec_model.cpp:72](../src/models/pec_model.cpp:72) |

---

## 1. Architektura — ocena

### 1.1 Struktura warstw

Poprawnie rozdzielone warstwy:

```
web (SPA) → web/proxy (Express, HTTP/JSON) → gRPC (50051) → src/api/service_impl
                                                          → src/controllers (MountController, dome, derotator, weather...)
                                                          → src/models (TPoint, KF, efemerydy, PEC, focus...)
                                                          → src/core (SOFA) 
                                                          → src/hal (SIMULATED, MF7025V2, SERIAL, ETHERNET, ...)
```

- **Pimpl** konsekwentnie stosowany (MountController, GrpcServer, KalmanFilter, TPointModel, Configuration, ConfigMonitor) — dobra hermetyzacja i stabilny ABI.
- **Jeden wspólny rdzeń** `astro_mount_core` (statyczna biblioteka) używany przez wszystkie serwery — eliminuje duplikację.
- Dome, derotator, focuser są hostowane **in-process** na wspólnym porcie 50051 (nowa architektura), a weather/power/sequencer/db pozostają osobnymi procesami — spójna, przemyślana decyzja.

### 1.2 Problem N2 — duplikat definicji `dome.proto`

W repozytorium istnieją **dwie różne definicje** usługi dome:

- [`proto/dome.proto`](../proto/dome.proto) — `package astro_mount`, RPC: `OpenDome`, `CloseDome`, `RotateDome`, `GetDomeStatus`, `ParkDome`, `SetDomeSync`;
- [`dome/proto/dome.proto`](../dome/proto/dome.proto) — `package astro_dome`, RPC: `OpenShutter`, `CloseShutter`, `RotateTo`, `Halt`, `Park`, `Unpark`, `GoHome`, `GetStatus`, `WatchStatus`, `SetAutoSync`, `GetAutoSync`, `UpdateMountAzimuth`.

Obie są kompilowane (CMake globuje `proto/*.proto` **i** podkatalogi). Implementacja ([`dome_service_impl.h`](../dome/include/dome_service_impl.h)) i web proxy ([`client.js`](../web/proxy/grpc/client.js), [`routes/dome.js`](../web/proxy/routes/dome.js)) używają wersji z `dome/proto/`. Wersja w `proto/` jest **przestarzała i pozostaje w binarkach** — każdy klient zbudowany na bazie `proto/dome.proto` (np. ASCOM/INDI, skrypty) otrzyma inny zestaw RPC. To realne zagrożenie spójności API.

**Rekomendacja:** usunąć `proto/dome.proto` (i ewentualnie `proto/derotator.proto`, jeśli istnieje duplikat) albo ujednolicić — jeden źródłowy proto na usługę.

### 1.3 Problemy integracyjne w `main.cpp`

- **N4:** dome auto-sync zasilany `status.telescope_axis1_position` ([main.cpp:419](../src/main.cpp:419)). Dla montażu **równikowego** oś1 to kąt godzinny (HA), a kopuła potrzebuje **azymutu**. Przy niewyrównanym/błędnym HA dome obraca się źle. Wymaga konwersji na azymut (alt/az) lub użycia osi azymutalnej.
- **N3:** derotator zasilany `ha_hours=0.0, dec_deg=0.0` z komentarzem „would need LST - RA” ([main.cpp:429](../src/main.cpp:429)). Pole rotation liczony z tych danych jest bezwartościowe. Należy podawać rzeczywiste HA/Dec (LST − RA, Dec).

### 1.4 Ocena architektury

| Kryterium | Ocena | Uwagi |
|-----------|:-----:|-------|
| Separacja warstw | ✅ 9/10 | Czyste rozdzielenie; Pimpl wszędzie |
| Spójność API (proto) | ⚠️ 6/10 | Duplikaty proto (N2), niespójność WatchState/GetState (N1/N8) |
| Integracja serwisów | ⚠️ 7/10 | In-process vs porty proxy (N5), błędne feedy dome/derotator (N3/N4) |
| Rozszerzalność (HAL) | ✅ 8/10 | Fabryka HAL + wiele implementacji |
| Dokumentacja architektury | ✅ 9/10 | Bardzo dobra (PL i EN) |

---

## 2. Poprawność numeryczna — ocena

### 2.1 Potwierdzone poprawki (z wcześniejszego raportu)

| ID | Poprawka | Status | Lokalizacja |
|----|----------|--------|-------------|
| B1 | LST `lon*D2R` (nie `/15`) | ✅ potwierdzone | [astronomical_calculations.cpp:244](../src/core/astronomical_calculations.cpp:244), [:294](../src/core/astronomical_calculations.cpp:294) |
| B2 | GMST bez ΔAT (UT1≈UTC, reszta ≤0.9 s ≈ 15″) | ✅ potwierdzone | [astronomical_calculations.cpp:612](../src/core/astronomical_calculations.cpp:612) |
| B3 | TPoint Newton — poprawne jednostki kroku | ✅ potwierdzone | [tpoint_model.cpp:339](../src/models/tpoint_model.cpp:339) |
| B4 | Refrakcja — jedna korekcja temperatury, bez 1.33322 | ✅ potwierdzone | [astronomical_calculations.cpp:199](../src/core/astronomical_calculations.cpp:199) |
| B5 | FocusCurve — dane odśrodkowane + QR | ✅ potwierdzone | [focus_curve.cpp:141](../src/models/focus_curve.cpp:141) |
| B6 | ProperMotion — clamp cos(dec) | ✅ potwierdzone | [astronomical_calculations.cpp:764](../src/core/astronomical_calculations.cpp:764) |
| B7 | FieldRotation — bez tan(δ), guard mianownika | ✅ potwierdzone | [field_rotation_model.cpp:31](../src/models/field_rotation_model.cpp:31) |
| B8 | ExposurePlanner — formuła kwadratowa SNR | ✅ potwierdzone | [exposure_planner.cpp:80](../src/models/exposure_planner.cpp:80) |
| R1 | KF — kontrola PD S + regularyzacja | ✅ potwierdzone | [kalman_filter.cpp:177](../src/models/kalman_filter.cpp:177) |
| R2 | KF — metryka = promień spektralny F | ✅ potwierdzone | [kalman_filter.cpp:727](../src/models/kalman_filter.cpp:727) |
| R3 | KF — adaptacyjne Q tylko diagonala | ✅ potwierdzone | [kalman_filter.cpp:281](../src/models/kalman_filter.cpp:281) |
| R7 | SoftLimits — `std::fmod` zamiast pętli | ✅ potwierdzone | [mount_controller.cpp:6688](../src/controllers/mount_controller.cpp:6688) |

### 2.2 Mocne strony numeryczne (potwierdzone w kodzie)

- **IAU 2006** precesja/nutacja (`iauPmat06`, `iauNut06a`, `iauObl06`) we wszystkich ścieżkach — zgodnie z deklaracją sub-arcsecond.
- **KF**: Joseph form, LDLT z kontrolą pivotów, renormalizacja kwaternionu, symetryzacja P — bardzo dobra praktyka.
- **Efemerydy**: barycentryczna interpolacja Lagrange'a z prekomputowanymi wagami, clamp zakresu czasu (brak ekstrapolacji poza dane), fallback do liniowej.
- **TPoint**: ColPivHouseholderQR na macierzy projektowej (nie AᵀA), ostrzeżenie o kolinarnych kolumnach (WORM_ERROR vs POLAR_AZ).
- **Track loop**: strażniki `isfinite()` w ~15 punktach, watchdog 5 s, guider jako **offset pozycji** (niezależny od dt), nutacja/TPoint jako **delta** (brak akumulacji).

### 2.3 Pozostałe uwagi numeryczne

- **N10 (PEC):** [`performFFT()`](../src/models/pec_model.cpp:72) używa okresu `T = worm_cycle_seconds` (1 cykl) przy sumowaniu po oknie 3 cykli. Harmoniczne, których rząd nie dzieli 3, mają zaniżone amplitudy (wyciek spektralny). To wada **jakości** modelu PEC, nie stabilności. Poprawa: DFT z okresem okna (3T) lub detekcja fazy względem 1 cyklu.
- **R6 (z poprzedniego raportu):** KF w pętli trackingu dostaje pozycję **po** przesunięciu kinematycznym `rate·dt` — celowo udokumentowane; nie jest to błąd, ale ograniczenie (KF nie filtruje surowego enkodera). Warto w przyszłości podawać surowy odczyt enkodera.
- **GMST:** użycie UTC jako przybliżenie UT1 daje stały błąd ≤ ~15″ — akceptowalne dla celu ~1″, udokumentowane. Do poprawnego UT1 potrzebny IERS Bulletin A (`iauUtcut1`).
- **Konwencja kwaternionu** `[qx,qy,qz,qw]` (astro_calc) vs `[qw,qx,qy,qz]` (KF `updateStateTransitionMatrix`) — ryzyko przy przyszłej integracji EKF; warto udokumentować w jednym miejscu.

### 2.4 Ocena poprawności numerycznej

**9/10.** Poprawki wdrożone i potwierdzone testami. Pozostałe uwagi to jakość (PEC) i udokumentowane decyzje projektowe, nie błędy krytyczne.

---

## 3. Stabilność i obsługa błędów — ocena

### 3.1 Potwierdzone mocne strony

- **Watchdog trackingu** (dt > 5 s → ERROR) + **slew watchdog** (`max(60 s, 3×nominalny+60 s)` → ERROR **z zatrzymaniem napędów**).
- **`shutdown()` w pełni idempotentny** — guard UNINITIALIZED + `joinable()`; HAL czyszczony w odwrotnej kolejności.
- **Thread safety:** `shared_mutex` (state/rate/env/thread), spójna hierarchia blokowania, `joinWorkThread()` bez `state_mutex_` (brak deadlocku), re-entrancy guard `notify_in_progress_`.
- **Maszyna stanów** (UNINITIALIZED/IDLE/SLEWING/TRACKING/MERIDIAN_FLIP/PARKING/PARKED/ERROR) — spójna, `clearErrors()` kompletny.
- **Signal handling** w `main.cpp` — tylko atomic flag (poprawnie, bez niebezpiecznych wywołań w handlerze).
- **Teardown** w `main.cpp` — jawna kolejność niszczenia obiektów globalnych przed `Logger::shutdown()` (komentarz o pułapce statyków — dobre zrozumienie).

### 3.2 Uwagi stabilnościowe

- **N9:** [`ConfigMonitor::getConfiguration()`](../src/config/config_monitor.cpp:56) zwraca `const Configuration&` po zwolnieniu `config_mutex_` — konfiguracja może być podmieniona przez `monitorLoop` między zwróceniem referencji a jej użyciem. W praktyce ryzyko niskie (głównie odczyt), ale to data race wg modelu pamięci C++. Poprawka: zwracać kopię lub trzymać blokadę przez czas użycia.
- **`ConfigMonitor` nie jest podłączony** do `main.cpp` — plik monitoruje zmiany, ale zmiany nie trafiają do działającego kontrolera (brak integracji).
- **WatchState** działa w pętli `while` z `sleep_for(100 ms)` i tylko sprawdza `context->IsCancelled()` — brak guarda na opóźnienia; dopuszczalne dla strumienia statusu.

### 3.3 Ocena stabilności

**9/10.** Architektura bezpieczeństwa jest bardzo dobra; drobne uwagi to N9 i brak integracji ConfigMonitor.

---

## 4. Kompletność funkcjonalna — ocena

### 4.1 Stan według dokumentacji a rzeczywistość

| Funkcja | Dokumentacja | Rzeczywistość w kodzie | Ocena |
|---------|--------------|------------------------|:-----:|
| Sterowanie montażem (slew/track/park/unpark) | ✅ | ✅ pełna implementacja + testy | ✅ |
| TPoint (21 parametrów, progressive) | ✅ | ✅ pełny | ✅ |
| CASUAL (quaternion, SVD Wahba) | ✅ | ✅ pełny | ✅ |
| Bootstrap kalibracja | ✅ | ✅ pełny (EQ + CASUAL SVD) | ✅ |
| Efemerydy (interpolacja, predykcja) | ✅ | ✅ pełny | ✅ |
| Guider | ✅ | ✅ pełny (offset pozycji) | ✅ |
| Field rotation / derotator | ✅ | ⚠️ **stuby** | ⚠️ |
| ST4 autoguiding | ✅ | ⚠️ **stuby** | ⚠️ |
| PEC | ✅ | ⚠️ jakość FFT (N10) | ⚠️ |
| Weather, Power, Sequencer | ✅ | ✅ serwisy + klienci | ✅ |
| Notifications | ✅ | ✅ engine + kanały | ✅ |
| LX200 | ✅ | ✅ serwer | ✅ |
| Qt GUI | ✅ opisany | ❌ **usunięty z repo** (N6) | ❌ |
| CANopen HAL | ✅ (raport) | ❌ „not yet available” (N7) | ❌ |

### 4.2 Stuby / niezaimplementowane ścieżki (potwierdzone)

- [`st4_calibration.cpp`](../src/controllers/st4_calibration.cpp) — tylko komentarze (brak pomiaru arcsec/ms);
- `St4Guider::calibrate()` — bez rzeczywistej kalibracji kierunków;
- [`derotator_tmc5160.cpp`](../src/hal/derotator_hal/derotator_tmc5160.cpp) — pusty;
- `FieldRotationModel::calculateCasual()` — **ignoruje kwaternion** („Simplified model”);
- `DerotatorController::getFieldRotation()` — zwraca `{}`;
- **N3:** derotator w `main.cpp` dostaje zerowe HA/Dec.

### 4.3 Ocena kompletności funkcjonalnej

Rdzeń montażu (najważniejsza funkcjonalność) jest **w pełni kompletny**. Luki dotyczą peryferii: derotator/ST4/PEC są na poziomie stubów lub jakościowych ograniczeń, a GUI Qt i CANopen HAL są udokumentowane, ale **nieobecne w kodzie** — to największa rozbieżność dokumentacja↔kod (N6, N7).

**Ocena: 8/10** (rdzeń 10/10, peryferia 5–6/10).

---

## 5. Łatwość użytkowania (użytkownik końcowy) — ocena

### 5.1 Web UI (SPA) — mocna strona

- **12 zakładek**: Status, Sterowanie, Ustawienia, Kalibracja, Baza Danych, Śledzenie, Logging, Testy + rozszerzone (PEC, Power, Guider, Derotator, Sequencer, Camera, Focuser, Dome, Weather, Pulley, LX200).
- Motyw **noktowizyjny (czerwony)**, tryb mobilny, pełny ekran, przełącznik PL/EN (i18n).
- Odświeżanie statusu co 1 s, wskaźniki połączenia, obsługa formatów RA/Dec (hms/dms).
- Baza obiektów z katalogami (Messier, NGC, IC, Caldwell, HYG, SAO, ...).

### 5.2 Problemy UX / konfiguracji

- **N5 (proxy):** web proxy domyślnie łączy dome/derotator z portów 50053/50054 (osobne procesy), ale nowa architektura hostuje je **in-process na 50051**. Użytkownik uruchamiający tylko `astro_mount_controller` zobaczy „dome/derotator offline” w web UI, dopóki nie uruchomi osobnych binarek lub nie zmieni `.env`. To mylące domyślnie.
- **Domyślny config** [`default.json`](../config/default.json) ma `hal.type = "mf7025v2"` (sprzęt CAN) — nowy użytkownik bez sprzętu musi znać `test_no_hardware.json`/`emulation.json`. Brak interaktywnego „first-run” wykrywania sprzętu.
- **Ścieżki config w `main.cpp`** są względne (`config/default.json`, `config/dome_config.json`) — wymagają uruchomienia z katalogu projektu; usługa systemd używa absolutnych (dobrze). Rozbieżność między trybem dev a systemd.
- **Bałagan w `config/`**: dziesiątki snapshotów (`default_2026-*.json`, `mf7025v2_2026-*.json`) — trudno odróżnić bieżący od archiwalnych.
- **N6:** dokumentacja Qt GUI prowadzi użytkownika do nieistniejącej aplikacji.

### 5.3 Ocena łatwości użytkowania

Web UI i dokumentacja są **bardzo dobre** (9/10). Obniżają ocenę: rozbieżność portów proxy vs in-process (N5), brak out-of-box dla sprzętu, bałagan w config, i dokumentacja GUI Qt, którego nie ma.

**Ocena: 7.5/10.**

---

## 6. Ranking rekomendacji

### Wysoki priorytet
1. **[N1]** Poprawić `WatchState`: `set_tracking_rate_ra/dec` ma przyjmować `axis1_rate * 3600.0` / `axis2_rate * 3600.0` (jak w `GetState`), a nie `tracking_error_*`. Dodać test wartości.
2. **[N2]** Usunąć duplikat `proto/dome.proto` (lub ujednolicić definicje); zrobić audyt pozostałych duplikatów proto (derotator).

### Średni priorytet
3. **[N4]** Dome auto-sync: przekazywać rzeczywisty azymut (konwersja alt/az), nie oś1/HA.
4. **[N3]** Derotator: podawać realne `ha_hours`/`dec_deg` (LST−RA, Dec) zamiast 0.0.
5. **[N5]** Ujednolicić porty web proxy z hostingiem in-process (50051) lub udokumentować wymóg uruchamiania osobnych serwerów; poprawić `.env.example`/README.
6. **[N9]** `ConfigMonitor::getConfiguration()` — zwracać kopię lub utrzymywać blokadę przez użycie.
7. **[N10]** Poprawić DFT w PEC (okno = 3 cykle) dla poprawnej estymacji harmonicznych.

### Niski priorytet / porządkowe
8. **[N6]** Zaktualizować/oznaczyć dokumentację Qt GUI jako niedostępną (lub przywrócić źródła).
9. **[N7]** Ujednolicić dokumentację CANopen HAL z rzeczywistym stanem (MF7025V2 jako jedyny CAN).
10. **[N8]** Wyrównać zestaw pól `WatchState` z `GetState`.
11. Wykorzystać `ConfigMonitor` w `main.cpp` (hot-reload configu) lub usunąć martwy kod.
12. Oczyścić `config/` z archiwalnych snapshotów; dodać `first-run` wykrywanie HAL.
13. Ujednolicić konwencję kwaternionów `[w,x,y,z]` vs `[x,y,z,w]` w całym kodzie.

---

## 7. Podsumowanie końcowe

| Kategoria | Ocena |
|-----------|:-----:|
| Architektura | 8/10 |
| Poprawność numeryczna | 9/10 |
| Stabilność | 9/10 |
| Kompletność funkcjonalna | 8/10 |
| Łatwość użytkowania | 7.5/10 |
| **Średnia** | **8.3/10** |

**Najważniejszy wniosek:** rdzeń systemu (obliczenia astronomiczne, sterowanie montażem, TPoint, KF, efemerydy, bezpieczeństwo) jest **wysokiej jakości** — poprawny numerycznie, stabilny i dobrze przetestowany. Główne ryzyka leżą na **peryferiach**: niespójności API (WatchState), duplikaty protokołów, błędne feedy dome/derotator w `main.cpp`, rozbieżność portów web proxy z hostingiem in-process oraz stuby derotator/ST4/PEC. Rekomendowane poprawki N1–N5 w pierwszej kolejności.
