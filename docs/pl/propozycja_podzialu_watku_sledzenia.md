# Propozycja: podział głównego wątku śledzenia na wątek komunikacyjny (I/O) i wątek obliczeniowy

> **Status:** propozycja projektowa — bez zmian w kodzie.
> Zakres: rozdzielenie pojedynczej pętli śledzenia
> ([`startTracking()`](../../src/controllers/mount_controller.cpp:1144),
> lambda wątku [`work_thread_`](../../src/controllers/mount_controller.cpp:1563))
> na dwa współpracujące wątki o różnych częstotliwościach.

## Spis treści

- [Propozycja: podział głównego wątku śledzenia na wątek komunikacyjny (I/O) i wątek obliczeniowy](#propozycja-podział-głównego-wątku-śledzenia-na-wątek-komunikacyjny-io-i-wątek-obliczeniowy)
  - [Spis treści](#spis-treści)
  - [1. Cel](#1-cel)
  - [2. Analiza obecnej pętli śledzenia](#2-analiza-obecnej-pętli-śledzenia)
  - [3. Proponowana architektura](#3-proponowana-architektura)
  - [4. Podział odpowiedzialności](#4-podział-odpowiedzialności)
    - [4.1 `calc_thread_` (obliczeniowy, domyślnie 50 Hz)](#41-calc_thread_-obliczeniowy-domyślnie-50-hz)
    - [4.2 `io_thread_` (komunikacyjny, domyślnie 200 Hz)](#42-io_thread_-komunikacyjny-domyślnie-200-hz)
  - [5. Wymiana danych między wątkami](#5-wymiana-danych-między-wątkami)
  - [6. Nowa konfiguracja](#6-nowa-konfiguracja)
  - [7. Cykl życia wątków](#7-cykl-życia-wątków)
  - [8. Synchronizacja, ryzyka i zapobieganie deadlockom](#8-synchronizacja-ryzyka-i-zapobieganie-deadlockom)
  - [9. Co pozostaje bez zmian](#9-co-pozostaje-bez-zmian)
  - [10. Przyrostowe fazy wdrożenia](#10-przyrostowe-fazy-wdrożenia)
  - [11. Czy zmiana poprawi płynność sterowania?](#11-czy-zmiana-poprawi-płynność-sterowania)
    - [11.1 Gdzie poprawa będzie wyraźna](#111-gdzie-poprawa-będzie-wyraźna)
    - [11.2 Gdzie poprawa będzie minimalna lub żadna](#112-gdzie-poprawa-będzie-minimalna-lub-żadna)
    - [11.3 Co tak naprawdę wygładza ruch silnika](#113-co-tak-naprawdę-wygładza-ruch-silnika)
    - [11.4 Wniosek](#114-wniosek)
  - [12. Czy wątek obsługujący sprzęt będzie wątkiem czasu rzeczywistego?](#12-czy-wątek-obsługujący-sprzęt-będzie-wątkiem-czasu-rzeczywistego)
    - [12.1 Opcjonalne wzmocnienie do `SCHED_FIFO`](#121-opcjonalne-wzmocnienie-do-sched_fifo)
    - [12.2 Wniosek](#122-wniosek)
  - [13. Gdzie naprawdę tkwi opóźnienie — analiza operacji CANopen](#13-gdzie-naprawdę-tkwi-opóźnienie--analiza-operacji-canopen)
    - [13.1 Najgorszy przypadek — tak, to SDO](#131-najgorszy-przypadek--tak-to-sdo)
    - [13.2 Ale większość „odczytów” w pętli śledzenia to cache](#132-ale-większość-odczytów-w-pętli-śledzenia-to-cache)
    - [13.3 Realnie blokujące operacje CAN w pętli śledzenia to zapisy](#133-realnie-blokujące-operacje-can-w-pętli-śledzenia-to-zapisy)
    - [13.4 Wniosek](#134-wniosek)
  - [14. Operacje synchroniczne vs asynchroniczne w CANopen — czy przejście na PDO pomoże?](#14-operacje-synchroniczne-vs-asynchroniczne-w-canopen--czy-przejście-na-pdo-pomoże)
    - [14.1 Co już jest w kodzie](#141-co-już-jest-w-kodzie)
    - [14.2 Co dałoby przejście na tryb asynchroniczny (PDO)](#142-co-dałoby-przejście-na-tryb-asynchroniczny-pdo)
    - [14.3 Zastrzeżenia](#143-zastrzeżenia)
    - [14.4 Rekomendacja](#144-rekomendacja)

---

## 1. Cel

Obecnie jedna pętla śledzenia wykonuje **naprzemiennie** operacje wejścia/wyjścia
(blokujące transakcje CANopen/HAL) i kosztowne obliczenia astronomiczne
(nutacja, TPOINT, refrakcja, filtr Kalmana, transformacje ALT-AZ/CASUAL).

Skutki obecnego rozwiązania:

1. **Częstotliwość pętli jest ograniczona przez najwolniejszą operację** —
   pojedynczy wolny odczyt SDO (np. `getStatus()` safety monitora) wydłuża
   całą iterację, więc nie da się sterować napędami z częstotliwością setek
   herców przy jednoczesnym wykonywaniu pełnych obliczeń w tej samej iteracji.
2. **`state_mutex_` jest trzymany przez długi blok obliczeniowy**
   ([`mount_controller.cpp:1651–2370`](../../src/controllers/mount_controller.cpp:1651)),
   przez co `getStatus()`, `stop()` i inne wywołania z wątków gRPC/INDI blokują
   się na czas obliczeń.
3. **Brak rozdzielenia „jak szybko komunikować się ze sprzętem” od „jak często
   przeliczać astronomię”** — obie te potrzeby mają różne wymagania czasowe:
   - komunikacja: 100–500 Hz (płynne zadawanie prędkości/pozycji, szybka reakcja
     na błędy bezpieczeństwa, odczyt pozycji rzeczywistych),
   - obliczenia: 10–50 Hz (nutacja/TPOINT/refrakcja zmieniają się powoli; filtr
     Kalmana i śledzenie HA nie wymagają setek herców).

---

## 2. Analiza obecnej pętli śledzenia

Pojedyncza lambda wątku [`work_thread_`](../../src/controllers/mount_controller.cpp:1563)
co `tracking_update_ms` (domyślnie 20 ms = 50 Hz,
[`tracking_config.h:29`](../../include/config/tracking_config.h:29)) wykonuje:

| Faza | Linie | Typ | Treść |
|---|---|---|---|
| I/O Block 1 | [`1600–1615`](../../src/controllers/mount_controller.cpp:1600) | I/O | `hal_safety_monitor_->getStatus()`, `checkLimits(0/1)` (blokujące SDO) |
| I/O Block 2 | [`1620–1649`](../../src/controllers/mount_controller.cpp:1620) | I/O | `hal_sensor_interface_->readAll()` (temperatura/ciśnienie/wilgotność) |
| Blok obliczeń | [`1651–2370`](../../src/controllers/mount_controller.cpp:1651) | CPU | soft limits, postęp pozycji, filtr Kalmana, normalizacja, korekcje nutacja/TPOINT/refrakcja, prędkości ALT-AZ/CASUAL, detekcja flipa |
| I/O Block 2b | [`2384–2403`](../../src/controllers/mount_controller.cpp:2384) | I/O | `setPosition()` celów meridian flip |
| I/O Block 3 | [`2405–2734`](../../src/controllers/mount_controller.cpp:2405) | I/O | `setPosition()` (tryb pozycyjny, co 50 iteracji) lub `setVelocity()` (tryb prędkościowy), odczyty `getActualPosition()` |

Kluczowa obserwacja: **blok CPU jest już wyizolowany pod `state_mutex_` i nie
zawiera I/O** (komentarze w kodzie celowo przeniosły I/O poza blokadę), ale
wszystko to nadal dzieje się w **jednym wątku** — dlatego I/O i CPU wzajemnie
się opóźniają.

---

## 3. Proponowana architektura

Rozdzielić pętlę na dwa wątki tworzone razem przez `startTracking()`, a
komunikację sprzętową oprzeć **głównie o PDO (asynchroniczne)**, z **SDO jako
opcjonalną, wolną ścieżką sterowania** (przełączanie trybów, konfiguracja,
homing, kasowanie błędów).

```
                        ┌────────────────────────────────────────────┐
                        │            MountController::Impl           │
                        │                                            │
   ┌──────────────┐     │   ┌──────────────────┐   TrackingCommand   │
   │ Wątek gRPC / │─────┼──▶│  calc_thread_    │──────────────────┐  │
   │ INDI / UI    │     │   │  (obliczeniowy)  │                  │  │
   └──────────────┘     │   │  20–50 Hz        │                  ▼  │
                        │   └──────────────────┘        ┌──────────────────┐
                        │                               │ command_mutex_   │
                        │                               │  (migawka)       │
                        │                               └──────────────────┘
                        │                                        │
                        │   ┌──────────────────┐        ┌──────────────────┐
                        │   │  io_thread_      │◀───────│  (migawka)       │
                        │   │  (komunikacyjny) │        │ telemetry_mutex_ │
                        │   │  100–500 Hz      │────────│                  │
                        │   └──────────────────┘        └──────────────────┘
                        │            │                           ▲
                        │            ▼                           │
                        │   ┌────────────────────────────────────────────┐
                        │   │  CANopen — ścieżka główna (asynchroniczna) │
                        │   │  TPDO1: status 6041h + pozycja 6064h (10 ms)│
                        │   │  RPDO2: controlword 6040h + target 607Ah    │
                        │   │  (fire-and-forget, bez ACK)                 │
                        │   └────────────────────────────────────────────┘
                        │            │
                        │            ▼   (opcjonalnie, rzadkie operacje)
                        │   ┌────────────────────────────────────────────┐
                        │   │  SDO — slow control path (opcjonalna)      │
                        │   │  mode switch / config / homing / clear     │
                        │   └────────────────────────────────────────────┘
                        └────────────────────────────────────────────┘
```

- **`calc_thread_`** — wątek obliczeniowy (10–50 Hz): przejmuje cały obecny
  „blok obliczeń” ([`1651–2370`](../../src/controllers/mount_controller.cpp:1651))
  i **produkuje** migawkę `TrackingCommand`.
- **`io_thread_`** — wątek komunikacyjny (100–500 Hz): **konsumuje**
  `TrackingCommand` i **produkuje** migawkę `TrackingTelemetry`, komunikując
  się z napędem **przez PDO** (odczyty z TPDO1, zapisy przez RPDO2); SDO
  używa tylko dla rzadkich operacji sterowania wolnymi zmianami.

---

## 4. Podział odpowiedzialności

### 4.1 `calc_thread_` (obliczeniowy, domyślnie 50 Hz)

Przejmuje bez zmian logikę CPU z obecnego bloku:

- `evaluateSoftLimits()` i wyznaczanie `rate_factor`
  ([`1676`](../../src/controllers/mount_controller.cpp:1676)),
- postęp pozycji (integracja kinematyczna) + offset guidera
  ([`1729`](../../src/controllers/mount_controller.cpp:1729)),
- filtr Kalmana (`predict`/`update`)
  ([`1788–1799`](../../src/controllers/mount_controller.cpp:1788)),
- normalizacja ALT-AZ/CASUAL
  ([`1818–1838`](../../src/controllers/mount_controller.cpp:1818)),
- korekcje nutacja/TPOINT/refrakcja dla EQUATORIAL
  ([`1875–1879`](../../src/controllers/mount_controller.cpp:1875))
  oraz ALT-AZ/CASUAL
  ([`1891–2054`](../../src/controllers/mount_controller.cpp:1891)),
- wyznaczanie prędkości zależnych od pozycji dla ALT-AZ/CASUAL
  ([`2061–2069`](../../src/controllers/mount_controller.cpp:2061)),
- **decyzja** o meridian flip (detekcja, liczniki, obliczenie celów
  `computeFlipTargets()`)
  ([`2071–2167`](../../src/controllers/mount_controller.cpp:2071)),
- wyliczenie nowych celów pozycyjnych dla trybu pozycyjnego EQUATORIAL
  (HA = LST − RA, lead, wybór najkrótszej drogi) — dziś wykonane w
  I/O Block 3, ale **obliczenie celu** należy do calc, a **wysłanie** do io
  ([`2417–2491`](../../src/controllers/mount_controller.cpp:2417)).

Wynik zapisuje do migawki `TrackingCommand` (patrz §5).

### 4.2 `io_thread_` (komunikacyjny, domyślnie 200 Hz)

Przejmuje wyłącznie kontakt ze sprzętem, bez astronomii. Komunikacja bazuje na
**PDO** (asynchroniczna, nieblokująca):

- **Odczyty (TPDO1):** status 6041h + pozycja 6064h napływają co 10 ms z
  callbacku PDO ([`canopen_interface.cpp:441`](../../src/controllers/canopen_interface.cpp:441))
  do cache `TrackingTelemetry` — `io_thread_` kopiuje cache, nie wykonuje
  żadnej transakcji CAN,
- **Zapisy pozycyjne (RPDO2):** cel śledzenia/flipa wysyłany przez
  `canopen_send_frame()` (controlword 6040h + target position 607Ah) —
  fire-and-forget, bez czekania na ACK,
- **Zapisy prędkościowe:** w trybie prędkościowym (ALT-AZ/CASUAL oraz
  EQUATORIAL velocity-mode) — opcjonalne przemapowanie RPDO na 6040h + 60FFh;
  dopóki go nie ma, rzadkie zmiany prędkości idą przez SDO (wolna ścieżka),
- **Weryfikacja dostarczenia:** poprawność zapisów PDO potwierdzana pośrednio
  przez status 6041h (TPDO1) i heartbeat — bez blokowania,
- **Safety/sensory:** `hal_safety_monitor_->getStatus()`/`checkLimits()` i
  `readAll()` sensorów z decymacją, wyłącznie z cache/odrębnych wątków,
- **Stop awaryjny:** natychmiastowy `stop()`/`emergencyStop()` napędów przy
  błędzie bezpieczeństwa lub fladze `stop_motors` w migawce,
- **SDO (opcjonalna, slow control path):** przełączanie trybów, konfiguracja,
  homing, kasowanie błędów — rzadkie, mogą pozostać synchroniczne.

Dzięki temu **odstęp między kolejnymi komendami do napędu nie zależy ani od
czasu obliczeń, ani od czasu odpowiedzi SDO** — w wariancie PDO `io_thread_`
jest w pełni nieblokujący.

---

## 5. Wymiana danych między wątkami

Dwa małe, kopiowalne struktury + dwa dedykowane mutexy (prostsze i bezpieczniejsze
niż struktury lock-free; przy 200 Hz koszt `lock_guard` + memcpy jest zaniedbywalny):

```cpp
// Produkowana przez calc_thread_, konsumowana przez io_thread_.
struct TrackingCommand {
    uint64_t seq{0};                 // numer sekwencji (wykrywanie nowej migawki)
    bool     velocity_mode{true};
    double   rate1{0.0}, rate2{0.0}; // deg/s servo (tryb prędkościowy)
    bool     pos_update_pending{false};
    double   pos1{0.0}, pos2{0.0};   // deg servo (tryb pozycyjny)
    double   pos_vel{0.0}, accel{0.0};
    bool     flip_pending{false};
    double   flip1{0.0}, flip2{0.0};
    bool     stop_motors{false};
};

// Produkowana przez io_thread_, konsumowana przez calc_thread_.
struct TrackingTelemetry {
    double   pos1{0.0}, pos2{0.0};   // pozycje rzeczywiste (deg servo)
    double   vel1{0.0}, vel2{0.0};   // prędkości rzeczywiste
    bool     motor_enabled[2]{false,false};
    bool     motor_fault[2]{false,false};
    int      safety_state{0};        // hal::SafetyStatus::State
    double   temperature{20.0}, pressure{1013.25}, humidity{0.5};
    bool     valid{false};
};
```

Członkowie `Impl`:

```cpp
std::unique_ptr<std::shared_mutex> command_mutex_;   // TrackingCommand
std::unique_ptr<std::shared_mutex> telemetry_mutex_; // TrackingTelemetry
TrackingCommand  tracking_command_;
TrackingTelemetry tracking_telemetry_;
```

**Zasady:**

- `calc_thread_` czyta `tracking_telemetry_` (shared_lock), liczy, potem
  nadpisuje `tracking_command_` (unique_lock) i inkrementuje `seq`.
- `io_thread_` czyta `tracking_command_` (shared_lock — krótko, tylko kopia
  migawki), wykonuje wysyłkę PDO bez żadnej blokady, potem zapisuje
  `tracking_telemetry_` (unique_lock — krótko).
- W wariancie PDO `tracking_telemetry_` jest dodatkowo zasilana bezpośrednio
  przez callback TPDO1 (pozycja/status), a `io_thread_` tylko ją publikuje;
  blokujący odczyt SDO nie występuje w gorącej ścieżce.
- Żaden z tych mutexów nie jest trzymany podczas I/O ani podczas obliczeń —
  blokady obejmują **wyłącznie** skopiowanie/wstawienie migawki.

Opcjonalnie (jeśli pomiary wykażą kontencję): **podwójne buforowanie** z
`std::atomic<uint64_t> seq` i wymianą wskaźników — na początek wystarczy mutex.

---

## 6. Nowa konfiguracja

Rozszerzyć [`TrackingConfig`](../../include/config/tracking_config.h:26) o:

```cpp
int io_update_hz{200};      // częstotliwość wątku komunikacyjnego (domyślnie 200 Hz)
int calc_update_hz{50};     // częstotliwość wątku obliczeniowego (domyślnie 50 Hz)
int telemetry_decimation{5};// co ile iteracji io_thread_ czyta sensory (5 → ~40 Hz)
int safety_check_decimation{1}; // co ile iteracji io_thread_ sprawdza safety monitor

// Wariant PDO (ścieżka główna):
bool pdo_fast_path{true};           // użyj TPDO1 (odczyt) + RPDO2 (zapis) zamiast SDO
bool pdo_velocity_mapping{false};   // przemapuj RPDO na 6040h+60FFh (target velocity)
bool sdo_slow_control{true};        // opcjonalna, wolna ścieżka SDO dla rzadkich operacji
```

`tracking_update_ms` zostaje zachowany jako wartość kompatybilności wstecznej,
ale `calc_update_hz` (1 / `tracking_update_ms`) jest preferowanym źródłem.
Domyślne 50 Hz calc + 200 Hz io zachowuje dzisiejsze tempo obliczeń, a
komunikację podnosi 4×. Gdy `pdo_fast_path=false`, `io_thread_` wraca do
dotychczasowej ścieżki SDO (wariant kompatybilności).

---

## 7. Cykl życia wątków

1. **Start** — w [`startTracking()`](../../src/controllers/mount_controller.cpp:1144),
   po ustawieniu `tracking_active_ = true`, zamiast jednego `work_thread_`
   tworzone są dwa wątki:
   ```cpp
   {
       std::lock_guard<std::shared_mutex> tlock(*thread_mutex_);
       calc_thread_ = std::thread(&Impl::trackingCalcLoop, this, mode, ...);
       io_thread_   = std::thread(&Impl::trackingIoLoop, this);
   }
   ```
2. **Stop** — [`stop()`](../../src/controllers/mount_controller.cpp:2779) ustawia
   `tracking_active_ = false` (atomic, już istnieje:
   [`7470`](../../src/controllers/mount_controller.cpp:7470)) i przechodzi do
   `IDLE`; obie pętle wychodzą w najbliższej iteracji. Dołączenie następuje w
   `joinWorkThreadLocked()` (rozszerzonym o `calc_thread_` i `io_thread_`).
3. **Shutdown** — destruktor/`shutdown()` joinuje oba wątki przed zniszczeniem
   `hal_interface_` (dotychczasowy problem use-after-free już rozwiązany dla
   `work_thread_`; te same reguły stosują się do nowych wątków).

---

## 8. Synchronizacja, ryzyka i zapobieganie deadlockom

| Ryzyko | Przeciwdziałanie |
|---|---|
| Wyścig „calc pisze nowy cel, io czyta stary” | Migawka `TrackingCommand` + `seq`; io zawsze wysyła spójny, kompletny zestaw pól z jednej migawki |
| Zakleszczenie `state_mutex_` ↔ `command/telemetry_mutex_` | Ścisła hierarchia: `state_mutex_` (stan autorytatywny) jest najwyżej; `command_mutex_`/`telemetry_mutex_` nigdy nie są trzymane razem z `state_mutex_` w przeciwnym kierunku. Calc: weź `state_mutex_` → policz → zwolnij → weź `command_mutex_` → zapisz migawkę. Io: nigdy nie bierze `state_mutex_` poza krótkim ustawieniem `ERROR` przy awarii |
| `getStatus()`/`stop()` blokowane długimi obliczeniami | Obliczenia nie będą już trzymać `state_mutex_` przez cały blok CPU — calc może trzymać go krócej, a finalne pozycje lądują w migawce; dokładny zakres krytyczny ustalić podczas wdrożenia, zachowując niezmiennik „bez I/O pod `state_mutex_`” |
| Io wyśle komendę po tym, jak calc ustawił `stop_motors` | `stop_motors` jest częścią migawki; io sprawdza go **przed** każdym `setVelocity`/`setPosition` w danej iteracji |
| Podwójne sterowanie napędem (startTracking vs slew/park) | Bez zmian: `work_thread_` nadal obsługuje slew/park; wątek śledzenia (calc+io) i slew/park są wzajemnie wykluczone przez istniejący `thread_mutex_` + `joinWorkThreadLocked()` |
| Utrata ostatniej pozycji przy zatrzymaniu | Io w każdej iteracji aktualizuje `TrackingTelemetry`; `refreshPositionsFromHAL()` po `stop()` nadal czyta fizyczną pozycję jak dotąd |

---

## 9. Co pozostaje bez zmian

- Cała logika slewu (`runSlewMonitor`), parkowania i `work_thread_` dla tych
  operacji — podział dotyczy **wyłącznie** gałęzi śledzenia.
- Publiczne API `MountController` i `MountStatus` — bez zmian sygnatur.
- Główne zmienne stanu (`axis1_position_`, `axis1_rate_`, `tracking_target_ra_hours_`,
  flagi meridian flip) — nadal chronione przez `state_mutex_`/`rate_mutex_`/
  `env_mutex_`; calc jest ich głównym producentem, io tylko czyta telemetrię.
- Semantyka `stop()`, `clearErrors()`, gamepada i LX200.

---

## 10. Przyrostowe fazy wdrożenia

1. **Faza A — refaktoryzacja bez zmiany zachowania:** wyciągnąć obecne bloki
   I/O i CPU do metod `trackingComputeStep()` / `trackingIoStep()`, ale nadal
   wywoływać je z jednego wątku. Cel: zero regresji, łatwe testy jednostkowe
   czystych obliczeń.
2. **Faza B — migawki:** dodać `TrackingCommand`/`TrackingTelemetry` i mutexy;
   pojedyncza pętla zapisuje/czyta migawki zamiast bezpośrednich zmiennych.
3. **Faza C — podział wątków:** uruchomić `calc_thread_` i `io_thread_`,
   przenieść cykl życia (start/stop/join) i rozszerzyć `TrackingConfig`.
4. **Faza D — strojenie:** pomiary jitteru iteracji, dobór domyślnych
   `io_update_hz`/`calc_update_hz`, ewentualne podwójne buforowanie.
5. **Faza E — PDO telemetria (odczyty asynchroniczne):** zasilić
   `tracking_telemetry_` z callbacku TPDO1 (status 6041h + pozycja 6064h) i
   usunąć blokujące odczyty z gorącej ścieżki; `io_thread_` czyta tylko cache.
6. **Faza F — PDO zapisy pozycyjne (asynchroniczne):** wysyłać cel pozycyjny
   śledzenia/flipa przez `canopen_send_frame()` (RPDO2: controlword 6040h +
   target position 607Ah) zamiast `setPositionTarget()` przez SDO; nadzór
   dostarczenia przez status 6041h + heartbeat.
7. **Faza G — wariant prędkościowy + SDO slow control (opcjonalna):**
   przemapować RPDO na 6040h + 60FFh dla trybu prędkościowego
   (`pdo_velocity_mapping`); SDO pozostawić wyłącznie jako wolną, opcjonalną
   ścieżkę dla przełączania trybów/konfiguracji/homingu/kasowania błędów.
   Flaga `pdo_fast_path=false` zachowuje stary wariant SDO.
8. **Faza H — dokumentacja:** aktualizacja
   [`docs/pl/watki_kontrolera.md`](../../docs/pl/watki_kontrolera.md) (sekcja 3.4
   i dodatek A) o nowe wątki, częstotliwości i ścieżkę PDO.

---

## 11. Czy zmiana poprawi płynność sterowania?

**Tak, ale w przewidywalnym i zależnym od trybu zakresie.** Płynność zależy tu
od dwóch różnych rzeczy: (a) determinizmu komend wysyłanych do napędu oraz
(b) wewnętrznej pętli napędu.

### 11.1 Gdzie poprawa będzie wyraźna

| Scenariusz | Dlaczego poprawa |
|---|---|
| **ALT-AZ / CASUAL** (prędkości zależne od pozycji, [`mount_controller.cpp:2061`](../../src/controllers/mount_controller.cpp:2061)) | Zadana prędkość zmienia się co iterację; dziś realny odstęp między `setVelocity()` skacze (20 ms nominalnie + czas I/O + czas CPU → 25–40 ms). Stałe 5 ms w `io_thread_` daje równomierne, 4× częstsze aktualizacje prędkości → mniej schodków. |
| **Korekcje guidera** (`applyGuiderCorrection`) | Krótsza latencja między decyzją a komendą; offset jest konsumowany w kolejnym kroku obliczeniowym, a io wysyła go szybciej. |
| **Meridian flip** | Cele flipa trafiają do napędu natychmiast po obliczeniu, bez czekania na zakończenie bieżącego bloku CPU. |

### 11.2 Gdzie poprawa będzie minimalna lub żadna

| Scenariusz | Dlaczego |
|---|---|
| **EQUATORIAL, tryb prędkościowy** (SIDEREAL/SOLAR/LUNAR) | Prędkość jest stała; napęd trzyma ją własnym regulatorem prędkości (kHz). Częstsze `setVelocity()` o identycznej wartości nic nie wnosi — dziś wysyłka jest już warunkowana progiem zmiany `1e-6` ([`mount_controller.cpp:2681`](../../src/controllers/mount_controller.cpp:2681)). |
| **EQUATORIAL, tryb pozycyjny (domyślny)** | `setPosition()` jest wysyłane tylko co `POS_UPDATE_INTERVAL = 50` iteracji (~1 s) ([`mount_controller.cpp:2417`](../../src/controllers/mount_controller.cpp:2417)); płynność między kolejnymi celami zapewnia wewnętrzny generator trajektorii napędu, a nie host. |

### 11.3 Co tak naprawdę wygładza ruch silnika

Ruch mechaniczny wygładza **wewnętrzna pętla trajektorii/PID samego napędu**
(STM42M / MF7025v2 pracują wewnętrznie z częstotliwością kHz), a nie częstotliwość
hosta. Zmiana poprawia więc przede wszystkim:

1. **determinizm** — komendy idą co stałe 5 ms zamiast „20 ms + czas obliczeń”,
2. **reakcję na zmiany setpointów** (ALT-AZ/CASUAL, guider, flip),
3. **izolację awarii** — zawieszenie jednej transakcji SDO nie blokuje ani
   obliczeń, ani kolejnych komend (watchdog pozostaje skuteczny).

### 11.4 Wniosek

Poprawa płynności będzie **wyraźna dla ALT-AZ/CASUAL i guidera**, a **niewielka
dla standardowego śledzenia sideralnego na montażu ekwatorialnym** (stała
prędkość lub 1 Hz aktualizacje pozycji). Aby zmaksymalizować zysk także w
trybie pozycyjnym, należałoby dodatkowo skrócić `POS_UPDATE_INTERVAL` — to
osobna, świadoma decyzja (większy ruch po magistrali CAN), niezależna od samego
podziału wątków.

---

## 12. Czy wątek obsługujący sprzęt będzie wątkiem czasu rzeczywistego?

**W wariancie bazowym — nie.** `io_thread_` to zwykły `std::thread` w domyślnej
polityce `SCHED_OTHER` (CFS), tak jak wszystkie dotychczasowe wątki projektu.
Dla 200 Hz to wystarczające, ponieważ:

1. **Determinizm ruchu zapewnia napęd, nie host** — firmware STM42M / MF7025v2
   realizuje wewnętrzne pętle trajektorii/PID z częstotliwością kHz.
2. **Komunikacja idzie przez SocketCAN** (stos sieciowy jądra + softirq) — nie
   ma twardych gwarancji czasu rzeczywistego; blokujący odczyt SDO i tak
   wprowadza nieoznaczoność niezależną od polityki wątku.
3. **Budżet 5 ms przy 200 Hz** — zwykły wątek z podniesionym priorytetem
   (`nice -10 … -20`) ma na to duży zapas, o ile odczyty statusów są
   dekorelowane (decymacja) od wysyłki komend.

### 12.1 Opcjonalne wzmocnienie do `SCHED_FIFO`

Jeśli pomiary jitteru wykażą przekroczenia, projekt pozostawia możliwość
podniesienia `io_thread_` do wątku RT:

| Krok | Opis | Uwaga |
|---|---|---|
| `pthread_setschedparam(SCHED_FIFO)` | priorytet np. 40–60 | wymaga root / `CAP_SYS_NICE` lub limitu `ulimit -r` |
| `mlockall(MCL_CURRENT\|MCL_FUTURE)` | blokada pamięci procesu | bez tego page-fault niszczy determinizm przy pierwszym dotknięciu strony |
| Rozdzielenie „ticker” od I/O | osobny, minimalny wątek RT tylko wystawia najnowszą komendę do napędu; blokujące odczyty statusów/telemetrii w osobnym wątku nie-RT | kluczowe — blokujący socket w wątku `SCHED_FIFO` może zagłodzić system |
| Priorytet poniżej IRQ jądra | trzymać się z dala od 99 (migration/watchdog jądra) | typowo 40–60 w pełni wystarcza |

### 12.2 Wniosek

Wariant bazowy **nie jest wątkiem czasu rzeczywistego** i dla 200 Hz nie musi
być — realna deterministyka leży w napędzie i magistrali CAN. Podniesienie do
`SCHED_FIFO` to osobny, opcjonalny krok, sensowny wyłącznie po rozdzieleniu
wysyłki komend od blokujących odczytów statusowych.

---

## 13. Gdzie naprawdę tkwi opóźnienie — analiza operacji CANopen

Odpowiedź na pytanie „czy najdłuższymi operacjami są odczyty z CANopen”:
**częściowo tak, ale z dwoma istotnymi zastrzeżeniami.**

### 13.1 Najgorszy przypadek — tak, to SDO

Wymiana SDO ma twardy limit czasu oczekiwania `timeout_us = 500 ms`
([`canopen.cpp:57`](../../lib/canopen_wrapper/src/canopen.cpp:57)) i
`kMaxRetries = 2` z odstępem `50 ms`
([`canopen.cpp:41`](../../lib/canopen_wrapper/src/canopen.cpp:41)). Pojedyncza
transakcja SDO do **węzła, który nie odpowiada**, może więc blokować do
**3 × 500 ms + 2 × 50 ms ≈ 1,6 s**. To zdecydowanie najdłuższa pojedyncza
operacja — sam kod to dokumentuje („Reads can block for up to ~1 s per axis”,
[`mount_controller.cpp:3326`](../../src/controllers/mount_controller.cpp:3326)).
SDO jest przy tym serializowane **per węzeł** — zawieszony węzeł blokuje tylko
swoją kolejkę, nie całą magistralę
([`canopen.cpp:10`](../../lib/canopen_wrapper/src/canopen.cpp:10)).

### 13.2 Ale większość „odczytów” w pętli śledzenia to cache

W gorącej ścieżce pętli śledzenia wiele odczytów **nie jest** transakcją CAN:

| Operacja | Co faktycznie robi |
|---|---|
| `hal_safety_monitor_->getStatus()` | czyta `parent_->lastStatus(axis)` — cache z wątku statusu HAL, nie SDO ([`canopen_hal.cpp:415`](../../src/hal/canopen_hal/canopen_hal.cpp:415)) |
| `hal_safety_monitor_->checkLimits()` | **no-op**, zwraca `true` ([`canopen_hal.cpp:434`](../../src/hal/canopen_hal/canopen_hal.cpp:434)) |
| `hal_axis*_motor_->getActualPosition()` / `getActualVelocity()` | zwraca zapamiętaną wartość: CanOpenMotor — atomiki ([`canopen_hal.cpp:121`](../../src/hal/canopen_hal/canopen_hal.cpp:121)), MF7025v2 — wartość z `updateStatus()` ([`mf7025v2_hal.cpp:243`](../../src/hal/mf7025v2_hal/mf7025v2_hal.cpp:243)) |
| `refreshPositionsFromHAL()` | czyta ww. cache, nie enkoder ([`mount_controller.cpp:3334`](../../src/controllers/mount_controller.cpp:3334)) |

Rzeczywiste odczyty CAN (status, enkoder, kąt wieloobrotowy) wykonują **osobne
wątki**: pętla statusu CanOpenHAL
([`canopen_hal.cpp:621`](../../src/hal/canopen_hal/canopen_hal.cpp:621)) i
`MfMotor::pollLoop()` (20 Hz, [`mf7025v2_hal.cpp:856`](../../src/hal/mf7025v2_hal/mf7025v2_hal.cpp:856)),
które zasilają te cache asynchronicznie.

### 13.3 Realnie blokujące operacje CAN w pętli śledzenia to zapisy

- `setPosition()` / `setVelocity()` → zapis SDO (lub komenda MF z echem i
  retry). MF7025v2 dodatkowo ponawia z odstępem 20 ms
  ([`mf7025v2_hal.cpp:97–111`](../../src/hal/mf7025v2_hal/mf7025v2_hal.cpp:97)).
- Bezpośrednie odczyty SDO (`getPositionData()` w
  [`CanOpenEncoder::read()`](../../src/hal/canopen_hal/canopen_hal.cpp:334))
  są poza gorącą ścieżką śledzenia.

### 13.4 Wniosek

Najdłuższym pojedynczym opóźnieniem jest **SDO do nieodpowiadającego węzła
(~1,6 s)**, a w pętli śledzenia materializuje się ono głównie przez **zapisy**
(`setPosition`/`setVelocity`) i ewentualne bezpośrednie odczyty SDO — nie przez
typową „telemetrię odczytu”, która w większości jest obsługiwana cache’ami w
osobnych wątkach. Dlatego w podziale wątków to właśnie **wysyłka komend**
(io_thread_) musi być chroniona timeoutem/decymacją, a odczyty telemetryczne
mogą pozostać w tle.

---

## 14. Operacje synchroniczne vs asynchroniczne w CANopen — czy przejście na PDO pomoże?

> **Wariant PDO (TPDO1 + RPDO2) został zintegrowany jako ścieżka główna w
> §3–§6 i fazach E–G wdrożenia (§10); SDO pozostaje opcjonalną, wolną ścieżką
> sterowania.** Ta sekcja dokumentuje stan istniejący i uzasadnienie.

**Tak — i dobra wiadomość: infrastruktura PDO jest już w dużej mierze
zbudowana, tylko nie jest podpięta do gorącej ścieżki śledzenia.**

### 14.1 Co już jest w kodzie

Wrapper [`canopen.h`](../../lib/canopen_wrapper/include/canopen/canopen.h:5) ma:

- **synchroniczne SDO** (`canopen_sdo_read/write_expedited`) — blokują do
  ~1,6 s (timeout 500 ms + 2 retry),
- **asynchroniczne PDO** — odbiór przez callback
  ([`canopen_pdo_cb`](../../lib/canopen_wrapper/include/canopen/canopen.h:79))
  i wysyłkę `canopen_send_frame()` (fire-and-forget,
  [`canopen.h:108`](../../lib/canopen_wrapper/include/canopen/canopen.h:108)),
- dedykowany wątek czytnika, który demultipleksuje ramki.

`CanOpenInterface` już konfiguruje PDO w `configurePDO()`:

| Kanał | COB-ID | Zawartość | Status |
|---|---|---|---|
| **TPDO1** (odczyt) | 0x180+node | status 6041h + pozycja 6064h, event timer 10 ms ([`canopen_interface.cpp:465–474`](../../src/controllers/canopen_interface.cpp:465)) | skonfigurowany, parsowany w callbacku ([`canopen_interface.cpp:441`](../../src/controllers/canopen_interface.cpp:441)) |
| **RPDO2** (zapis) | 0x300+node | controlword 6040h + target position 607Ah ([`canopen_interface.cpp:476–483`](../../src/controllers/canopen_interface.cpp:476)) | skonfigurowany, **niewykorzystany** w ścieżce komend |

Tymczasem faktyczna ścieżka komend nadal idzie przez **SDO**:

- `setPositionTarget()` / `setVelocityTarget()` → `writeSDO4` (blokujące,
  [`canopen_interface.cpp:248–292`](../../src/controllers/canopen_interface.cpp:248)),
- telemetria → `getDriveStatus()` / `getPositionData()` (SDO,
  [`canopen_interface.cpp:307`](../../src/controllers/canopen_interface.cpp:307),
  [`canopen_interface.cpp:345`](../../src/controllers/canopen_interface.cpp:345)).

### 14.2 Co dałoby przejście na tryb asynchroniczny (PDO)

1. **Asynchroniczne odczyty (TPDO1):** zasilić `TrackingTelemetry` prosto z
   callbacku PDO — napęd sam wypycha status+pozycję co 10 ms; `io_thread_`
   czyta tylko cache, bez żadnej transakcji CAN. Eliminuje resztę blokujących
   odczytów.
2. **Asynchroniczne zapisy (RPDO2):** pozycyjny cel śledzenia wysyłać przez
   `canopen_send_frame()` (controlword + target position) zamiast
   `setPositionTarget()` przez SDO. To usuwa **główną** przyczynę blokady —
   1,6 s na zawieszonym węźle. Dla trybu prędkościowego trzeba by przemapować
   RPDO na 6040h + 60FFh (target velocity) albo zostawić rzadkie zmiany
   prędkości na SDO.
3. **SDO zostaje jako „slow control path”:** przełączanie trybów, konfiguracja,
   homing, kasowanie błędów — rzadkie operacje mogą pozostać synchroniczne.

### 14.3 Zastrzeżenia

| Zastrzeżenie | Opis |
|---|---|
| **PDO jest bez potwierdzenia** | brak ACK na poziomie SDO — poprawność dostarczenia trzeba weryfikować przez status 6041h z TPDO1 + heartbeat (oba już odbierane asynchronicznie) |
| **Zakres sprzętu** | dotyczy HAL CANopen (STM42M/STMP42SXI). MF7025v2 używa własnego protokołu (0x9A/0x9C/0x92), a nie CiA 301 PDO — tam pozostaje polling w osobnym wątku |
| **Relacja do podziału wątków** | komplementarna: PDO czyni `io_thread_` w pełni nieblokującym; sam podział wątków izoluje opóźnienie już przy obecnym SDO |

### 14.4 Rekomendacja

Kolejność prac (odzwierciedlona w fazach §10): **najpierw podział wątków**
(izolacja opóźnienia, niskie ryzyko), **potem asynchroniczne odczyty TPDO1**
(proste, eliminuje blokujące odczyty), **na końcu RPDO2 dla zapisów
pozycyjnych** (największy zysk latencyjny, ale wymaga weryfikacji mapowania i
nadzoru poprawności dostarczenia). SDO pozostaje jako opcjonalna, wolna ścieżka
sterowania (`sdo_slow_control`), a `pdo_fast_path=false` daje wariant
kompatybilności z obecnym zachowaniem.
