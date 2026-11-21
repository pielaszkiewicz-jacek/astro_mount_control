# Gruntowna analiza mechanizmu śledzenia, slewu i kalibracji

**Data:** 2026-11-21
**Zakres:** EQUATORIAL, ALT_AZ, CASUAL + wszystkie ścieżki kalibracji
**Metoda:** ponowna analiza kodu źródłowego + logi produkcyjne (`/var/log/astro-mount/astro-mount.log`, `~/astro-mount-controller/logs/mount.log`)

---

## 1. Źródła i metoda

Analiza objęła:

- [`src/controllers/mount_controller.cpp`](../../src/controllers/mount_controller.cpp) — maszyna stanów, slew, śledzenie, kalibracje, flip, rewind,
- [`include/core/mount_kinematics.h`](../../include/core/mount_kinematics.h) — wyznaczanie prędkości,
- [`src/hal/canopen_hal/canopen_hal.cpp`](../../src/hal/canopen_hal/canopen_hal.cpp) i [`src/controllers/canopen_interface.cpp`](../../src/controllers/canopen_interface.cpp) — warstwa CANopen,
- [`indi/astro_mount_driver.cpp`](../../indi/astro_mount_driver.cpp) i [`indi/IndiPropertyMapper.cpp`](../../indi/IndiPropertyMapper.cpp) — integracja INDI,
- logi produkcyjne z sesji 2026-11-21 (objawy: `Position rewind ... zero did not take effect`, `HAL safety monitor triggered: state=ERROR`, zamrożona pozycja napędu, `Dec-axis desync`).

---

## 2. Architektura i jednostki

### 2.1 Jednostki
- **Stopnie teleskopowe** — kąt osi na niebie (HA/Dec, Alt/Az, kąty ramy CASUAL).
- **Stopnie serwo** — kąt wału silnika = stopnie teleskopowe × `gear_ratio` (dla STM42: 360:1).
- **Prędkości** — zawsze stopnie serwo/s; prędkość gwiazdowa [`kSiderealDegPerSec`](../../include/core/mount_kinematics.h:29) ≈ 0.004178074 °/s teleskopowo → 1.5041 °/s serwo przy gear 360.

### 2.2 Ramy pozycji
- Cel absolutny: `raw = teleskop × gear − home_offset` (rama surowego napędu).
- Odczyt fizyczny: `raw_servo = napęd + home_offset`.
- Własność round-trip: `resolveDecTarget(fold(p), p) = p` — gwarantuje zerowy ruch przy „track current position", o ile `home_offset` jest spójny.

### 2.3 Wątki
- `calc` (50 Hz) — integracja, KF, korekcje, limity, flip, cele.
- `io writer` (200 Hz) — emisja poleceń pozycji/prędkości.
- `io reader` (200 Hz) — telemetria (PDO cache, safety, czujniki).

---

## 3. Slew — analiza per typ

### 3.1 EQUATORIAL — [`slewToEquatorial()`](../../src/controllers/mount_controller.cpp:816)
1. `HA = LST − RA`, najkrótsza ścieżka względem bieżącej pozycji.
2. Cel HA = `HA×15×gear − home_offset`.
3. Dec przez [`resolveDecTarget()`](../../src/controllers/mount_controller.cpp:9411) — wybór równoważnika strony piera najbliższego napędowi.
4. Walidacja limitów miękkich → `setPosition`.

**Zagrożenia:**
- Dec blisko ±90°: „najbliższy równoważnik" może być po przeciwnej stronie piera (cel ~180° obok).
- Błędny `home_offset` → cel o całe obroty od napędu.

### 3.2 ALT_AZ — [`slewToHorizontal()`](../../src/controllers/mount_controller.cpp:1111)
- Oś 1 = wysokość (bez zawijania), oś 2 = azymut (`wrapShortestPath`).
- Zenit: azymut niezdefiniowany; `cos(alt)` przycięty do `cos(89.5°)`.

### 3.3 CASUAL
- `RA/Dec → equatorialToMountOrientation(Q)`; konwencja `EQUATORIAL_LIKE` → oś1 HA-like (zawijana), oś2 Dec-like; `ALT_AZ_LIKE` → oś1 wysokość, oś2 azymut.
- Przed kalibracją Q≈I — cele mogą być daleko od fizycznej pozycji (stąd ostrzeżenie w UI o „Slew & Add Measurement").

---

## 4. Śledzenie — analiza per typ

### 4.1 Prędkości — [`MountKinematics::trackingRates()`](../../include/core/mount_kinematics.h:78)
- EQUATORIAL: stałe (sidereal/solar/lunar/custom), Dec=0.
- ALT_AZ/CASUAL: zależne od pozycji; CASUAL = ALT_AZ obrócone kwaternionem Q.

### 4.2 Tryb pozycyjny (domyślny)
- EQUATORIAL ([`mount_controller.cpp:3293`](../../src/controllers/mount_controller.cpp:3293)): nowy cel `HA = LST − RA + lead`; profil prędkości z dolnym ograniczeniem [`trackingRateCap()`](../../src/controllers/mount_controller.cpp:8714); catch-up `exactStopVelocity`.
- ALT_AZ/CASUAL ([`mount_controller.cpp:3771`](../../src/controllers/mount_controller.cpp:3771)): cel rekomputowany w pętli zamkniętej; azymut zawijany; wysokość przycinana.

### 4.3 Tryb prędkościowy
- EQUATORIAL ([`mount_controller.cpp:3953`](../../src/controllers/mount_controller.cpp:3953)): `v = v_nom + drift_trim` (clamp ±1 °/s).
- ALT_AZ/CASUAL ([`mount_controller.cpp:4079`](../../src/controllers/mount_controller.cpp:4079)): wygładzanie α=0.3 + korekcja dryfu.

---

## 5. Kalibracje — wpływ na tor sterowania

| Ścieżka | Zmiana stanu | Ryzyko ruchu |
|---|---|---|
| Bootstrap ([`runBootstrapCalibration()`](../../src/controllers/mount_controller.cpp:5214)) | kwaternion Q (CASUAL) | skokowa zmiana ramy → kolejny cel w innym miejscu; odrzucana w TRACKING/FLIP |
| TPOINT ([`runTPointCalibration()`](../../src/controllers/mount_controller.cpp:5670)) | parametry modelu | inwersja [`computeTPointMountTarget()`](../../src/controllers/mount_controller.cpp:1711) może dać zły cel przy rozbieżnym ficie (próg jakości RMS≤120″) |
| ST4/guider | offsety pozycyjne | korekcja jednorazowa, nie kumuluje się |
| PID (pidcal) | nastawy napędu | złe nastawy → oscylacje/brak ruchu przy małych prędkościach |
| Auto-bootstrap (proxy + C++) | sekwencja + auto-track | po kalibracji `auto_track_after_calibration` uruchamia śledzenie ostatniego celu |

---

## 6. Stabilność sterowania

1. **Sawtooth stop–start** — napęd dogania cel i staje; zwalczane przez `lead` + CiA402 bit5 (change-setpoint-immediately) + `exactStopVelocity`.
2. **Oscylacja catch-up** — histereza + prędkość skalowana z residuum zamiast twardego progu.
3. **Drift-trim** — przy złym sprzężeniu prędkości nasyca się do ±1 °/s.
4. **Korekcje astronomiczne** — jako delty, resetowane przy starcie i po flipie.

---

## 7. Przypadki brzegowe — niekontrolowane obroty

### 7.1 Przepełnienie licznika multi-turn (potwierdzone w logach)
- Log: `Position rewind: axis 0 zero did not take effect (counter still 4737046.6°)`.
- Napęd stanął przy ~1.72 mld zliczeń (limit ±2e9 z `607Bh`), bit „operation enabled" migotał, monitor bezpieczeństwa → `ERROR`, śledzenie przerwane.
- Przyczyna: `zeroPosition()` nie dawał zbocza (VDI „set origin" nieskonfigurowane).
- Naprawione: [`zeroPosition()`](../../src/hal/canopen_hal/canopen_hal.cpp:142) toggluje VDI; [`canopen_vdi_set_zero.sh`](../../scripts/canopen_vdi_set_zero.sh) konfiguruje VDI2=funkcja 33 + EEPROM.

### 7.2 Rozjazd ramy po restarcie (potwierdzone w logach)
- Enkodery inkrementalne → po restarcie start z pozycji parkowej, utrata `home_offset`.
- Log: `Dec-axis desync detected` / `Delta axis2=-263°`, `vel2=239°/s` — oś Dec goni odległy cel.
- Wymaga re-Home przed śledzeniem.

### 7.3 Fold Dec / pier-side
- Przy spójnym `home_offset` domknięcie działa; przy rozjeździe cel ląduje po drugiej stronie piera.

### 7.4 Meridian flip
- HA+12h, Dec→180°−Dec; pomijany blisko bieguna; po flipie reset korekcji/KF.
- Ryzyko powtarzalnego flipa przy błędnym `meridian_flipped_`.

### 7.5 Zenit (ALT_AZ/CASUAL)
- `az_rate ∝ 1/cos(alt)` — cap `4×trackingRateCap` w [`computeMountFrameRates()`](../../src/controllers/mount_controller.cpp:8717).

### 7.6 Degeneracja kalibracji
- Bootstrap: warunek `cond ≥ 1e-6` (SVD).
- TPOINT: próg jakości + limit iteracji inwersji + fallback na cel nieskorygowany.

### 7.7 NaN/Inf i PDO
- Strażnicy `isfinite`; PDO cache guard skoku pozycji; RPDO2 mapping (6040h+607Ah) z fallbackiem SDO.

---

## 8. Małe przemieszczenie → duży obrót

1. EQUATORIAL blisko bieguna (RA niezdefiniowana).
2. ALT_AZ/CASUAL blisko zenitu (azymut).
3. Wybór przeciwnej strony piera (Dec ~180°).
4. Wrap HA o pełny obrót przy rozjeździe okna multi-turn.
5. Skokowa zmiana Q po kalibracji CASUAL.

---

## 9. Wznawianie rotacji z inną prędkością po zatrzymaniu

- Position mode: nowy `startTracking` startuje z profilem + catch-up (krótki szybki ruch).
- Velocity mode: `drift_trim` resetowany dopiero przy nowym `startTracking`.
- Rewind w IDLE: przesuwa `home_offset`; następny absolutny cel liczony z nowego okna.
- Meridian flip resume: reset korekcji/KF → inna dynamika pierwszych iteracji.

---

## 10. Wpływ na INDI

- Pozycja: preferowany `current_ra/dec`; po utracie Home INDI pokazuje pozycję parkową zamiast fizycznej.
- TrackState: `ERROR` → INDI raportuje IDLE; `ClearErrors` przywraca IDLE, ale nie wznawia śledzenia.
- Sync: uruchamia bootstrap; podczas TRACKING odrzucany → sync musi być w IDLE.
- Po restarcie kontrolera klient INDI musi się ponownie połączyć (gRPC).
- Każdy skok ramy (Home/restart/kalibracja/rewind) jest widoczny jako skok `current_ra/dec`.

---

## 11. Plan wdrożenia poprawek

### Priorytet P0 — bezpieczeństwo i poprawność (natychmiast)
1. **Re-Home po restarcie** — udokumentować i wymusić w UI/INDI komunikat „wymagany Home" (kontroler już odrzuca slew/track przed Home; dodać jawny status `homed=false` do INDI).
2. **Blokada śledzenia przy desyncu Dec** — już istnieje (`dec_desync_count_ > 30` → ERROR); dodać wcześniejsze ostrzeżenie i auto-stop przy `vel2 > max_tracking_rate` (wykrycie gonienia odległego celu).
3. **Rewind tylko w IDLE** — już jest; dodać test, że rewind nie startuje w TRACKING.

### Priorytet P1 — odporność
4. **Auto-detekcja jednostek `max_tracking_rate`** — zamiast tylko ostrzeżenia, logować jednoznacznie i nie dopuszczać do spadku poniżej sidereal (zrobione przez `trackingRateCap`).
5. **Persistencja `home_offset`** — zapisywać `home_offset` w stanie, aby restart z enkoderami inkrementalnymi nie gubił ramy (lub wymuszać Home).
6. **Strażnik `en` flicker** — zliczać przejścia `enabled` w trakcie śledzenia; po N przejściach w krótkim czasie przejść w ERROR z komunikatem „drive enable flicker" zamiast czekać na safety monitor.

### Priorytet P2 — komfort i diagnostyka
7. **Logowanie diagnostyczne slewu** — pełne `slewToEquatorial/Horizontal` (requested/current/resolved) na poziomie INFO.
8. **Telemetria INDI** — dodać `homed`, `home_offset`, `rewind_last` do statusu, aby KStars mógł pokazać stan.
9. **Skrypt weryfikacji VDI** — rozszerzyć `canopen_vdi_set_zero.sh` o test „set zero point" (zapis 2031h i odczyt 6064h).

---

## 12. Plan testów (do dodania)

### Testy jednostkowe — koordynaty i zawijanie
- `resolveDecTarget` dla wszystkich przypadków fold (już częściowo w `test_mount_coordinates`).
- `wrapShortestPath` przy `raw` ujemnym i wieloobrotowym (regresja „single-revolution rotation").

### Testy kontrolera — regresje
- `AutoTrackAfterBootstrapCalibration` (dodany) i `AutoTrackAfterTPointCalibration`.
- `TrackingRateCapFloorsAtSidereal` (dodany).
- `SlewRejectedBeforeHome` — EQUATORIAL z enkoderami inkrementalnymi: slew/track przed Home → `false`.
- `DecDesyncStopsTracking` — wstrzyknięcie rozjazdu Dec → stan ERROR w ciągu N iteracji.
- `DriveEnableFlickerTriggersError` — symulacja naprzemiennego `enabled` → ERROR.
- `RewindSkippedWhileTracking` — rewind nie zeruje licznika w TRACKING.
- `MeridianFlipNoRepeat` — flip nie powtarza się cyklicznie (flaga `meridian_flipped_`).
- `ZenithClampKeepsRateFinite` — ALT_AZ przy alt=89.9° nie daje nieograniczonego azymutu.
- `TPointDegenerateFitRejected` — zdegenerowany fit nie aktywuje modelu (częściowo jest).

### Testy integracyjne / HAL
- `ZeroPositionTogglesVdi` — `CanOpenMotor::zeroPosition` wysyła sekwencję 0→2→0 (mock interfejsu).
- `PdoPositionMappingFallback` — gdy mapping PDO zawiedzie, pozycja idzie przez SDO.
- `RewindPreservesTelescopeFrame` — po zerowaniu licznika raportowana pozycja teleskopowa bez zmian.

### Testy INDI
- `SyncRejectedWhileTracking` — INDI `Sync` podczas TRACKING zwraca błąd (guard stanu).
- `SetTrackEnabledUsesCurrentPosition` — po utracie Home/restarcie INDI nie wysyła pozycji parkowej.
- `ErrorRecoveryDoesNotResumeTracking` — po `ClearErrors` stan IDLE, bez automatycznego śledzenia.

---

## Podsumowanie

Rdzeń śledzenia/slewu jest stabilny numerycznie (strażnicy, KF, zawijanie), a główne zaobserwowane awarie wynikają z **utraty spójności ramy** (Home/restart/rewind) oraz **przepełnienia licznika multi-turn** napędów CANopen. Kluczowe poprawki wdrożono (auto-track po kalibracji, podłoga `max_tracking_rate`, naprawa `zeroPosition` + konfiguracja VDI). Pozostałe rekomendacje P0/P1/P2 oraz testy regresyjne należy wdrożyć wg planu z §11–§12.
