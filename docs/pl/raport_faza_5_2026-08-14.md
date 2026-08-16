# Raport — Faza 5: Stabilność i porządki (P13, P10, P12, P14, P15)

**Data:** 2026-08-14
**Cel:** wykonanie Fazy 5 z [`kompleksowa_analiza_projektu_2026-08-14.md`](kompleksowa_analiza_projektu_2026-08-14.md).

---

## 1. P13 — per-klientowy `WatchStatus` derotatora

**Problem:** współdzielony `watching_`/`watch_thread_` — dwóch klientów streamujących konfliktowało (rozłączenie jednego zatrzymywało stream drugiego).

**Zmiana** ([`derotator/src/derotator_service_impl.cpp`](../../derotator/src/derotator_service_impl.cpp)):
- `WatchStatus` używa teraz **wyłącznie lokalnej pętli** `while (!context->IsCancelled())` + `writer->Write()` (gRPC anuluje kontekst przy rozłączeniu klienta) — bez współdzielonego członka.
- Usunięto `watching_`/`watch_thread_` z nagłówka i destruktora.
- Każdy klient strumieniuje na własnym wątku gRPC → brak wzajemnych zakłóceń.

## 2. P10 — podpięcie `ConfigMonitor` do `main.cpp`

**Zmiana** ([`src/main.cpp`](../../src/main.cpp)):
- Utworzenie `config::ConfigMonitor(config_file, 2000)` + callback logujący **„Configuration file changed — restart the controller to apply changes.”**
- `config_monitor->start()` po wczytaniu konfiguracji; `stop()`/`reset()` w teardown (ścieżka normalna i wyjątków).
- Hot-reload kontrolerów nie jest bezpieczny — zmiany konfiguracji z UI nie są już cicho ignorowane, lecz jawnie zgłaszane.

## 3. P12 — spójne poświadczenia SSL dla `power_stub`

**Zmiana** ([`src/main.cpp`](../../src/main.cpp)):
- `power_stub` używa teraz `SslCredentials` (z `ssl_cert_path` jako root trust) gdy `network_config.enable_ssl`, a `InsecureChannelCredentials` w przeciwnym razie — zgodnie z resztą systemu (wcześniej zawsze insecure).

## 4. P14 — usunięcie martwych stübów HAL

Usunięto **8 plików**, które definiowały tylko TODO/klasy nigdy nieinstancjonowane (zero referencji w kodzie):

- ST4: `st4_gpio_libgpiod.cpp`, `st4_ftdi.cpp`, `st4_mcp2221.cpp`, `st4_arduino.cpp`
- Power: `power_i2c.cpp`
- Focuser: `focuser_zwo.cpp`, `focuser_moonlite.cpp`, `focuser_pegasus.cpp`

Oraz usunięto ich wpisy z [`CMakeLists.txt`](../../CMakeLists.txt) (`ST4_SOURCES`, `POWER_SOURCES`, `FOCUSER_SOURCES`). Pozostały faktycznie działające HAL: `st4_simulated`/`st4_gpio_sysfs`, `power_simulated`, `focuser_simulated`. (`focuser_service_impl` i `st4_guider_service_impl` tworzą HAL bezpośrednio — bezpiecznie.)

## 5. P15 — porządki

| Element | Działanie |
|---------|-----------|
| `alt_true` w [`calculateCasual`](../../src/models/field_rotation_model.cpp) | ✅ usunięty (martwy kod) |
| `setPositionCallback` | ✅ instalowany **raz** w konstruktorze `DerotatorServiceImpl` (wcześniej odtwarzany przy każdej aktualizacji pozycji) |
| puste `gui/` | ✅ dodano [`gui/README.md`](../../gui/README.md) (GUI Qt usunięte per N6; UI = web SPA) |
| snapshoty w `config/` | decyzja: **pozostawione** — to historyczne kopie konfiguracji użytkownika; usunięcie grozi utratą danych. Odnotowane w raporcie. |

## 6. Testy i weryfikacja

- **C++ build** ✅, **ctest 18/18** ✅
- **Proxy 32/32** ✅ (bez regresji)
- **Live (tryb emulacji):**
  - `Config monitor active on config/emulation.json` (P10 start)
  - po `touch config/emulation.json`: `[api] Configuration file changed, reloading...` + `[warning] [main] Configuration file changed — restart the controller to apply changes.` (**P10 potwierdzone**)
  - `GET /api/derotator/status` → 200 z poprawnymi danymi (derotator działa po zmianach P13)
  - porty zwolnione

## 7. Status Fazy 5

| Usterka | Status |
|---------|:------:|
| **P13** — konkurencja WatchStatus | ✅ per-klientowa pętla lokalna |
| **P10** — ConfigMonitor martwy | ✅ podpięty (log zmiany + wymóg restartu) |
| **P12** — power_stub insecure | ✅ SSL zgodnie z resztą |
| **P14** — stuby HAL | ✅ usunięte (martwy kod) |
| **P15** — porządki | ✅ `alt_true`, callback, `gui/`; snapshoty `config/` pozostawione (decyzja) |
