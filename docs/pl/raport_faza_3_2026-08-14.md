# Raport — Faza 3: Poprawność derotatora (P5, P6)

**Data:** 2026-08-14
**Cel:** wykonanie Fazy 3 z [`kompleksowa_analiza_projektu_2026-08-14.md`](kompleksowa_analiza_projektu_2026-08-14.md):
1. **P5** — przekazanie `mount_type` przez łańcuch `main.cpp → setMountPosition → DerotatorController` oraz `MountPositionUpdate.mount_type`; w `getFieldRotation()` wybór modelu (EQUATORIAL/ALT_AZ/CASUAL).
2. **P6** — integracja pozycji w `Tmc5160Derotator` (`current_position_deg` przestaje tkwić na 0.0 po `setAngle`/`setRate`).
3. **Testy jednostkowe** dla P5 (równikowy → 0) i P6 (position po `setAngle`).

---

## 1. P5 — wybór modelu field-rotation wg typu montażu

### Zmiany

| Plik | Zmiana |
|------|--------|
| [`include/controllers/derotator_controller.h`](../../include/controllers/derotator_controller.h) | Nowy `enum class MountKind { EQUATORIAL=0, ALT_AZ=1, CASUAL=2 }`; `setMountPosition(..., MountKind, quaternion)`; gettery `getMountKind()`; pola `mount_type_`, `mount_orientation_` |
| [`src/controllers/derotator_controller.cpp`](../../src/controllers/derotator_controller.cpp) | `setMountPosition` zapisuje typ + kwaternion; **`getFieldRotation()` wybiera model**: EQUATORIAL → `calculateEquatorial()` (0), ALT_AZ → `calculateAltAz()`, CASUAL → `calculateCasual()` z kwaternionem (wcześniej zawsze alt-az) |
| [`derotator/include/derotator_service_impl.h`](../../derotator/include/derotator_service_impl.h) | `setMountPosition(..., int mount_type, quaternion)`; pola `mount_type_`, `mount_orientation_` |
| [`derotator/src/derotator_service_impl.cpp`](../../derotator/src/derotator_service_impl.cpp) | `UpdateMountPosition` czyta `mount_type` (double → int); `populateFieldRotation` wypełnia pole `mount_type` („EQUATORIAL”/„ALT_AZ”/„CASUAL”) |
| [`src/main.cpp`](../../src/main.cpp) | Przekazuje `mount_type` (mapowanie `config::MountType`) i `mount_cfg.mount_orientation.quaternion` do `setMountPosition` |

Efekt: dla domyślnego montażu równikowego derotator zgłasza teraz **0** rotacji pola (wcześniej niezerowa prędkość z modelu alt-az).

## 2. P6 — integracja pozycji w Tmc5160Derotator

### Zmiany

- [`include/hal/derotator_tmc5160.h`](../../include/hal/derotator_tmc5160.h) — **nowy plik**: klasa przeniesiona z `derotator_tmc5160.cpp` do nagłówka (testowalność), dodano:
  - `integratePosition()` — prędkościowy: `position_ += VACTUAL·dt`; pozycyjny: slew do `XTARGET` z `max_speed`; wywoływany przy `getStatus()`/`isMoving()` (P6).
  - `position_mode_` — rozróżnia tryb pozycji i prędkości (poprawny stan `moving`).
  - **Tryb symulacji** (`"simulate": true` lub pusty config path) — `connect()`/`initialize()` działają w pamięci, `writeRegister` no-op → HAL działa bez SPI (emulacja + testy).
- [`src/hal/derotator_hal/derotator_tmc5160.cpp`](../../src/hal/derotator_hal/derotator_tmc5160.cpp) → `#include "hal/derotator_tmc5160.h"` (klasa w nagłówku).
- [`derotator/src/derotator_factory.cpp`](../../derotator/src/derotator_factory.cpp) → włącza nagłówek `hal/derotator_tmc5160.h` zamiast `.cpp`.

## 3. Testy

### Nowy test — [`tests/test_derotator.cpp`](../../tests/test_derotator.cpp) (6 testów)

| Grupa | Test | Weryfikacja |
|-------|------|-------------|
| DerotatorControllerP5Test | `EquatorialReturnsZero` | EQUATORIAL → `angle=0`, `rate=0` |
| | `AltAzMatchesParallacticModel` | ALT_AZ → zgodność z `calculateAltAz` |
| | `CasualIdentityMatchesAltAz` | CASUAL (identyczność) → zgodność z `calculateCasual` |
| | `CasualAppliesOrientationQuaternion` | kwaternion 45° wokół osi celowania → zmiana kąta ≈45° |
| Tmc5160DerotatorP6Test | `PositionIntegratesAfterSetAngle` | po `setAngle(30)` pozycja dochodzi do celu, `moving=false` |
| | `VelocityIntegratesRate` | `setRate(2)` → pozycja ≈ 2·Δt; `setRate(0)` → `moving=false` |

Zarejestrowany w [`CMakeLists.txt`](../../CMakeLists.txt) jako `DerotatorTest`.

### Wyniki

```
100% tests passed, 0 tests failed out of 17   (C++ ctest, w tym nowy DerotatorTest)
```

### Weryfikacja end-to-end (tryb emulacji, montaż równikowy)

```
GET /api/derotator/field-rotation
{"current_angle_deg":0,"current_rate_arcsec_s":0,"predicted_angle_10min":0,"mount_type":"EQUATORIAL"}
```
**P5 potwierdzone**: montaż równikowy raportuje 0 rotacji pola (przed zmianą zwracał niezerową prędkość alt-az); pole `mount_type` wypełnione.

## 4. Status Fazy 3

| Usterka | Status |
|---------|:------:|
| **P5** — zawsze model alt-az dla montażu równikowego | ✅ naprawione (wybór modelu wg `mount_type`) |
| **P6** — `position_` nigdy nie aktualizowane w TMC5160 | ✅ naprawione (integracja pozycji + tryb symulacji) |
| Testy P5/P6 | ✅ 6 testów w `DerotatorTest` |
