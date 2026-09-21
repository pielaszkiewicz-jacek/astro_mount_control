# Analiza matematyczna i numeryczna śledzenia (tracking)
## Kontroler montażu ↔ sterownik INDI

Dokument opisuje pełny łańcuch obliczeń pozycji od fizycznego napędu (MF7025v2)
przez kontroler montażu aż do sterownika INDI, wraz z niezmiennikami
matematycznymi, wykrytymi błędami numerycznymi i pozostałymi ryzykami.

---

## 1. Architektura przepływu pozycji

```
napęd (0x92 multi-turn, servo stopnie)
   │  getActualPosition() / getActualVelocity()
   ▼
MF7025v2 HAL (invert_direction → transparentne)
   │
   ▼
MountController::refreshPositionsFromHAL()
   │  raw_servo_axis* = pos + home_offset
   │  axis*_position_   = pos + home_offset  (gdy !tracking_active_)
   ▼
MountController::getStatus()
   │  raw_tel_axis* = raw_servo / gear_ratio
   │  fold Dec, normalizacja [0°, 360°)
   │  current_ra = LST − HA, current_dec = fold(Dec)
   ▼
gRPC GetState → ControllerState (current_ra, current_dec, telescope_axis1/2)
   │
   ▼
INDI: IndiPropertyMapper::toIndiRaDec()
   │  preferuje current_ra/current_dec
   │  fallback: telescope_axis1/2 + własne LST
   ▼
INDI: setEquatorialCoords() → NewRaDec(JNow) + EQUATORIAL_J2000
```

Ścieżka komendy:

```
INDI: Goto / SetTrackEnabled
   │  toGrpcCoordinates() → Coordinates (JNow, bez precesji)
   ▼
gRPC SlewToCoordinates / TrackObject
   ▼
MountController::slewToEquatorial() / startTracking()
   │  HA = LST − RA (najkrótsza ścieżka)
   │  Dec = resolveDecTarget() (strona piera/360°)
   │  axis*_target = telescope_stopnie × gear − home_offset
   ▼
HAL setPosition() / setVelocity() → napęd
```

---

## 2. Układy współrzędnych i transformacje

### 2.1 Montaż paralaktyczny (EQUATORIAL)

Jednostki:

- **Teleskopowe stopnie** — kąty osi na niebie (HA, Dec).
- **Servo stopnie** — kąty wału silnika = teleskopowe stopnie × `gear_ratio`.
  Dla `gear_ratio = 360`: `1° teleskopowe = 360° servo`.

Podstawowe zależności:

```
HA [h]  = LST [h] − RA [h]
RA [h]  = LST [h] − HA [h]
Dec [°] = kąt osi Dec (astronomicznie −90°…+90°)
```

### 2.2 Normalizacja i „fold" deklinacji

W [`getStatus()`](src/controllers/mount_controller.cpp:3718) odczyt Dec jest
normalizowany:

```
if (Dec >  90°) Dec = 180° − Dec
if (Dec < −90°) Dec = −180° − Dec
Dec = fmod(Dec, 360);  if (Dec < 0) Dec += 360
```

Własność: `fold(Dec)` odwzorowuje fizyczną Dec w przedział astronomiczny
`[−90°, 90°]`. Dec = 120° (za biegunem) jest równoważne Dec = 60° po drugiej
stronie piera — ta sama pozycja na niebie.

### 2.3 Ramy czasowe: JNow vs J2000

- Kontroler liczy `HA = LST − RA` w układzie **pozornym (JNow)**.
- [`IndiPropertyMapper::toGrpcCoordinates()`](indi/IndiPropertyMapper.cpp:22)
  przepuszcza współrzędne bez precesji (`apply_precession=false`, `epoch=0.0`).
- INDI raportuje `NewRaDec` w JNow, a własność `EQUATORIAL_J2000` jest
  precesowana (`jnowToJ2000`) wyłącznie do wyświetlania.

Dzięki temu nie ma podwójnej precesji JNow↔J2000 (błąd ~0,4°).

---

## 3. Ścieżka odczytu (readback)

### 3.1 HAL MF7025v2

Pozycja silnika pochodzi z odczytu absolutnego multi-turn **0x92** (co
`absolute_position_poll_ms = 100 ms`). Zero jest **prawdziwą pozycją**, a nie
„nieznaną". Prędkość pochodzi z **0x9C** (rozdzielczość 1 dps).

`invert_direction` jest transparentne: neguje jednocześnie komendę i odczyt,
więc logicznie się znosi (`getActualPosition()` zwraca pozycję logiczną).

### 3.2 Kontroler

[`refreshPositionsFromHAL()`](src/controllers/mount_controller.cpp:3890):

```cpp
raw_servo_axis* = pos + home_offset        // ZAWSZE (autorytatywne)
axis*_position_  = pos + home_offset        // tylko gdy !tracking_active_
```

Ważne własności:

1. `raw_servo_axis*` ma **jedynego piszącego** — HAL. Pętla trackingu nie
   nadpisuje go przy prawdziwym sprzęcie (poprawka).
2. `axis*_position_` w trakcie trackingu należy do pętli trackingu.

[`getStatus()`](src/controllers/mount_controller.cpp:3706) liczy:

```cpp
raw_tel_axis* = raw_servo / gear
current_ra    = LST − HA          (z raw_tel_axis1)
current_dec   = fold(raw_tel_axis2)
```

Dla HAL MF7025V2/SIMULATED zero nie jest zastępowane pozycją parkową
([`halPositionIsAuthoritative()`](src/controllers/mount_controller.cpp:7255)).

### 3.3 INDI

[`IndiPropertyMapper::toIndiRaDec()`](indi/IndiPropertyMapper.cpp:114) preferuje
`current_ra/current_dec` (poprawione przez kontroler), a fallback na
`telescope_axis1/2` jest używany tylko, gdy obie wartości są równe zero.

---

## 4. Ścieżka komendy (slew / track)

### 4.1 `slewToEquatorial()` / `startTracking()`

```
ha_hours  = LST − ra                    // [−12, +12]
current_ha_hours = axis1_position_ / (gear × 15)
ha_delta  = wrap(ha_hours − current_ha_hours, ±12)
ha_hours  = current_ha_hours + ha_delta  // najkrótsza ścieżka
axis1_target = ha_hours × 15 × gear − home_offset_axis1

dec_resolved = resolveDecTarget(dec, axis2_position_/gear)
axis2_target = dec_resolved × gear − home_offset_axis2
```

[`resolveDecTarget()`](src/controllers/mount_controller.cpp:7269) wybiera
równoważnik Dec najbliższy bieżącej fizycznej pozycji spośród:

```
{d, 180−d, d−360, d+360, 180−d−360, 180−d+360}
```

### 4.2 Pętla trackingu

- **Position mode** (domyślny, `equatorial_tracking_velocity_mode=false`):
  co ~50 iteracji wysyła `setPosition(nowy_cel_HA, stały_cel_Dec)`.
  Cel HA = `LST − RA_cel` + lead ~2 s. Cel Dec = rozwiązany cel z
  `startTracking()` (stały — Dec obiektu nie zmienia się w śledzeniu gwiazdowym).
- **Velocity mode** (eksperymentalny): `setVelocity(stopa_gwiazdowa × gear)`
  z korektą dryfu.

Stopa gwiazdowa (teleskopowo): `0.004178074 °/s`; w servo: `× gear`.

---

## 5. Niezmienniki matematyczne (round-trip)

**Własność kluczowa:** pozycja raportowana, wysłana z powrotem jako cel, musi
dawać ruch zerowy.

### 5.1 Oś HA/RA

Raport: `ra = LST − HA`.
Komenda: `HA_cel = wrap(LST − ra − current_HA, ±12) + current_HA`.

Ponieważ `current_HA = axis1_position_/(gear×15)` jest tą samą wartością, z
której policzono raport (gdy nie śledzimy), `LST − ra = HA`, więc
`HA_cel = HA`. ✓ (mod 24 h, obsłużone przez wrap).

### 5.2 Oś Dec

Raport: `d = fold(p)`.
Komenda: `resolveDecTarget(d, p)`.

Twierdzenie: `resolveDecTarget(fold(p), p) = p` dla `p ∈ [−180°, 180°]`.

Dowód (przypadki):

| Zakres p        | d = fold(p)   | kandydat równy p        |
|-----------------|---------------|--------------------------|
| [−90, 90]       | p             | `d` (= p)                |
| (90, 180]       | 180 − p       | `180 − d` (= p)          |
| [−180, −90)     | −180 − p      | `180 − d − 360` (= p)    |

W każdym przypadku istnieje kandydat o odległości **0**, a więc jest on
wybrany jako minimum. ✓

Wniosek: obie osie domykają się — „Use current" + „Slew i Śledź" jest bezruchem.

---

## 6. Wykryte i naprawione problemy numeryczne

| # | Problem | Przyczyna | Poprawka |
|---|---------|-----------|----------|
| 1 | Pozycja skacze między 2+ wartościami | `raw_servo_axis*` zapisywane jednocześnie przez pętlę trackingu i `refreshPositionsFromHAL()` | Pętla trackingu zapisuje `raw_servo` tylko dla symulacji ([`mount_controller.cpp`](src/controllers/mount_controller.cpp:2020)) |
| 2 | Raportowane Dec=90° zamiast fizycznego 0° | Inicjalizacja z pozycji parkowej (32400° servo) i fallback w `getStatus()` | [`halPositionIsAuthoritative()`](src/controllers/mount_controller.cpp:7255) — honorowanie fizycznego zera |
| 3 | Oś Dec wirowała / cel 60–180° obok | Fold Dec w odczycie, brak domknięcia w komendzie | [`resolveDecTarget()`](src/controllers/mount_controller.cpp:7269) + stały cel Dec w pętli trackingu |
| 4 | Pozycja dryfowała 2× podczas ruchu | Całkowanie `velocity × 0.1 s` przy pollu 50 ms | Całkowanie z rzeczywistym `dt` ([`mf7025v2_hal.cpp`](src/hal/mf7025v2_hal/mf7025v2_hal.cpp:550)) |
| 5 | Tracking celu Goto zamiast bieżącej pozycji | `SetTrackEnabled` używał `m_targetRA/Dec` | Śledzenie bieżącej pozycji ([`astro_mount_driver.cpp`](indi/astro_mount_driver.cpp:1069)) |
| 6 | Podwójne odwrócenie osi | `invert_direction` (HAL) × `invert_axis` (kontroler) | Usunięto z konfiguracji (`false`) |
| 7 | Cel HA o pełny obrót (24 h) obok | Brak najkrótszej ścieżki | Wrap ±12 h w `slewToEquatorial`/`startTracking` |
| 8 | Nieprawidłowa Dec (>90°) od klienta | Brak walidacji wejścia | Odrzucenie + log ([`mount_controller.cpp`](src/controllers/mount_controller.cpp:551)) |

---

## 6a. Mapowanie zaobserwowanych objawów na przyczyny

Wszystkie objawy zgłoszone w trakcie testów mają odzwierciedlenie w tabeli
powyżej. Bezpośrednie mapowanie:

| Obserwowany objaw | Przyczyna (nr z tabeli) | Stan |
|---|---|---|
| „Dwie różne pozycje przeskakujące" | #1 konflikt zapisu `raw_servo` | naprawione |
| „Pozycja = ostatni obiekt kalibracji" | #5 cel Goto zamiast bieżącej pozycji + rozjazd układów (#1–#3) | naprawione |
| „Odjazd po podwójnym Slew i Śledź" | #2 parkowe 90° + #3 fold Dec + #7 brak najkrótszej ścieżki HA | naprawione |
| „Błąd limitu pozycji (axis2=134°)" | #3 fold Dec + #8 brak walidacji Dec | naprawione |
| „Niekontrolowany obrót" | #3 oś Dec wirowała (fold bez domknięcia w pętli trackingu) | naprawione |
| „Rozjazd układów współrzędnych montaż↔INDI" | #1/#2/#3 — złamany niezmiennik round-trip | naprawione (sekcja 5) |
| „Odwrócenie osi w konfiguracji" | #6 podwójne odwrócenie (`invert_direction` × `invert_axis`) | usunięte z konfiguracji |

---

## 7. Pozostałe ryzyka i przypadki brzegowe

**Status wdrożenia:** pozycje 1 (osobliwość biegunowa), 4 (home offset) i 6
(precyzja double) są obsłużone w kodzie; pozycje 2 (histereza granicy folda),
3 (jednolite źródło LST) i 5 (synchronizacja Kalmana) pozostają **otwartymi**
rekomendacjami z sekcji 8.

1. **Osobliwość biegunowa (|Dec| ≈ 90°).** RA jest niezdefiniowana na biegunie;
   kod ma guard `NUTATION_POLE_GUARD_DEG = 0.1°`, ale ogólna precyzja w pobliżu
   bieguna jest z natury ograniczona.

2. **Granica folda Dec = 90°.** Przy dokładnie 90° fold jest tożsamością, ale
   numerycznie blisko granicy drobne fluktuacje odczytu mogą przeskakiwać Dec
   między 89.999° a 90.001° (skok konwencji, nie pozycji fizycznej).

3. **Spójność LST.** Kontroler używa `calculateLST(jd, config.longitude)`;
   INDI w fallbacku używa własnego `computeLst()` (longitude z `updateLocation`).
   Przy rozbieżnej konfiguracji lokalizacji fallback RA będzie przesunięty.
   Główna ścieżka (`current_ra/dec`) nie zależy od LST INDI.

4. **Home offset.** `axis_target = telescope × gear − home_offset`, a odczyt
   `raw_servo = pos + home_offset`. Operacja Home musi być spójna — przy
   niezgodnym `home_offset` oś Dec potrafi „uciekać" (zabezpieczone domknięciem
   z p. 5.2).

5. **Filtr Kalmana.** Aktualizowany tylko w pętli trackingu; przy pracy
   z prawdziwym HAL i position mode `axis*_position_` jest źródłem wewnętrznym,
   a fizyczna pozycja trafia do `raw_servo`. Rozjazd między nimi rośnie z czasem
   (dryf PID), ale krótkoterminowo jest pomijalny.

6. **Double-precision a długie śledzenie.** `axis1_position_` rośnie bez
   normalizacji (komentarz w [`mount_controller.cpp`](src/controllers/mount_controller.cpp:2023));
   mantysa 53-bitowa wystarcza na ~10 h śledzenia przy 1.5°/s servo bez utraty
   znaczących cyfr.

---

## 8. Rekomendacje

1. **Pojedyncze źródło prawdy pozycji.** *(otwarte)* Docelowo `getStatus()`
   powinien raportować wyłącznie `raw_servo_axis*` (fizyczny odczyt), a
   `axis*_position_` powinno być jedynie stanem wewnętrznym pętli trackingu.
2. **Synchronizacja `axis*_position_` z HAL po zatrzymaniu trackingu.**
   *(wdrożone)* [`startTracking()`](src/controllers/mount_controller.cpp:1517)
   i [`slewToEquatorial()`](src/controllers/mount_controller.cpp:574) wywołują
   `refreshPositionsFromHAL()` po `joinWorkThread()`, więc kolejny cel liczony
   jest z fizycznej pozycji.
3. **Wspólne źródło LST.** *(otwarte)* Przenieść `computeLst` do jednej
   klasy/modułu, aby kontroler i INDI zawsze używały tej samej wartości
   (eliminacja fallback RA).
4. **Testy round-trip.** *(wdrożone)* [`tests/test_mount_coordinates.cpp`](tests/test_mount_coordinates.cpp)
   sprawdza `foldDec()` i `resolveDecTarget(foldDec(p), p) == p` (3 testy).
5. **Logowanie diagnostyczne.** *(wdrożone)* `slewToEquatorial: RA=... Dec=...`
   oraz `slewToEquatorial Dec: requested/current/resolved` pozwala szybko
   zidentyfikować źródło nieprawidłowego celu.
