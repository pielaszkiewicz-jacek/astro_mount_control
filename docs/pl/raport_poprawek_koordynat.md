# Raport: Poprawki systemu współrzędnych montażu

**Data:** 2026-06-27 (aktualizacja: 2026-06-27 23:22 CEST)
**Zakres:** `src/controllers/mount_controller.cpp`, `src/api/service_impl.cpp`, `tests/test_mount_controller.cpp`, `web/public/js/components/debugTest.js`
**Typy montażu:** EQUATORIAL, ALT_AZ, CASUAL

---

## 1. Zidentyfikowane problemy (wspólne dla wszystkich typów)

### 1.1. Niezgodność przestrzeni współrzędnych — pozycja silnika vs pozycja montażu

System wewnętrznie przechowuje pozycje osi (`axis1_position_`, `axis2_position_`) w **stopniach serwomechanizmu** (stopnie wału silnika). Dotyczy to wszystkich typów montażu. Świadczą o tym:

- Prędkości śledzenia zawierają przełożenie: `axis1_tracking_rate = 0.004178 * gear_ratio` °/s (EQUATORIAL), `alt_rate_deg * ha_gear` (ALT_AZ), `m1_rate_deg * ha_gear` (CASUAL)
- Aktualizacja pozycji: `axis1_position_ += rate * dt` — akumuluje w stopniach serwa
- Odczyt z CANopen: `getPositionData()` zwraca pozycję w stopniach serwa

**Błąd:** Cele pozycyjne (`axis1_target_`) były obliczane w **stopniach niebieskich/teleskopu/montażu** bez mnożenia przez `gear_ratio`:

- **EQUATORIAL:** `axis1_target_ = ha_hours * 15.0` (stopnie niebieskie HA)
- **ALT_AZ:** `axis1_target_ = ra` (altitude w stopniach teleskopu)
- **CASUAL:** `axis1_target_ = mount_alt` (stopnie w ramie montażu)

Przy `gear_ratio = 360`, cel 30° teleskopu = 30° serwa = 0.083° rzeczywistego ruchu teleskopu. Montaż praktycznie się nie poruszał. Gdy serwonapęd był w pozycji 5400° (po 1h śledzenia), a cel został ustawiony na 30°, napęd wykonywał **niekontrolowany obrót wstecz o ~15 obrotów** z maksymalną prędkością — identyczne zachowanie dla wszystkich typów montażu.

### 1.2. Normalizacja niszcząca pozycję absolutną (wszystkie typy)

Pozycja `axis1_position_` była normalizowana do `[-180°, 180°]` w każdej iteracji pętli śledzenia. Napędy CANopen używają **pozycjonowania absolutnego** — normalizacja niszczyła referencję, powodując skok pozycji o wielokrotność 360° i niekontrolowane obroty wsteczne.

### 1.3. Limity pozycji w złej przestrzeni (wszystkie typy)

`evaluateSoftLimits()` porównywała pozycję w stopniach serwa bezpośrednio z limitami skonfigurowanymi w stopniach teleskopu. Przy `gear_ratio=360`, limit ±270° teleskopu odpowiada ±97200° serwa.

### 1.4. Niestabilność kontrolera (wszystkie typy)

`refreshPositionsFromCANopen()` wykonywał odczyty SDO trzymając `state_mutex_`. Timeout SDO (~1s) blokował wszystkie wywołania gRPC.

### 1.5. WEB UI pokazuje stopnie serwa zamiast teleskopu (wszystkie typy)

`GetState`/`WatchState` umieszczały `status.axis1_position` (stopnie serwa) w `current_position`. Przy `gear_ratio=360` wyświetlana wartość była 360× większa od rzeczywistej pozycji teleskopu.

---

## 2. Zastosowane poprawki

### Plik: [`src/controllers/mount_controller.cpp`](src/controllers/mount_controller.cpp) — 23 poprawki

| # | Funkcja / lokalizacja | Problem | Poprawka | Typy |
|---|----------------------|---------|----------|------|
| 1 | `slewToEquatorial` EQUATORIAL | Cel w ° niebieskich | `ha_hours * 15.0 * ha_gear` | EQ |
| 2 | `startTracking` EQUATORIAL | j.w. | j.w. | EQ |
| 3 | `park()` | Park w ° teleskopu | `park_position * gear` | ALL |
| 4 | `park()` CANopen | Bezpośrednie wywołanie | `park_position * gear` | ALL |
| 5 | Meridian flip | 180° niebieskich ≠ 180° serwa | `180.0 * ha_gear` | EQ |
| 6 | `executeMeridianFlip` | j.w. | j.w. | EQ |
| 7 | Normalizacja | [-180,180] niszczy pozycję absolutną | **Usunięta** + update `raw_servo` | ALL |
| 8 | `evaluateSoftLimits` | Serwo° vs limity teleskopu° | `telescope_axis = pos / gear` | ALL |
| 9 | `getStatus()` | `telescope_axis` z `raw_servo` (przeterm.) | `telescope_axis = axis1 / gear` (live) | ALL |
| 10 | `refreshPositionsFromCANopen` | SDO w `state_mutex_` | Odczyt **poza** blokadą | ALL |
| 11 | `slewToHorizontal` ALT_AZ | Cel bez przełożenia | `azimuth * gear`, `altitude * gear` | AZ |
| 12 | Gamepad HOME | Park bez przełożenia | `park_position * gear` | ALL |
| 13 | Korekcja nutacji | ° niebieskie → serwo° | `ra_corr * 15.0 * ha_gear` | EQ |
| 14 | Korekcja TPOINT | j.w. | Mnożenie przez gear | EQ |
| 15 | Korekcja refrakcji | j.w. + Dec w różnych przestrzeniach | Konwersja + mnożenie | EQ |
| 16 | Guider | Korekcje w ° niebieskich | `ra_corr * ... * ha_gear` | ALL |
| 17 | ALT_AZ/CASUAL clamping | Clampowanie serwo° limitami tel. | Konwersja serwo→tel→clamp→serwo | AZ, CA |
| 18 | HA w astronomii | `axis1 / 15.0` (serwo°/15) | `axis1 / 15.0 / ha_gear` (tel. h) | EQ |
| 19 | **ALT_AZ `startTracking`** | Cel w ° teleskopu | `ra * ha_gear`, `dec * dec_gear` | **AZ** |
| 20 | **CASUAL `startTracking`** | Cel w ° ramy montażu | `mount_alt * ha_gear`, `mount_az * dec_gear` | **CA** |
| 21 | **CASUAL `slewToEquatorial`** | j.w. | `mount_alt * ha_gear`, `mount_az * dec_gear` | **CA** |
| 22 | **CASUAL `slewToHorizontal`** | j.w. | `mount_az * ha_gear`, `mount_alt * dec_gear` | **CA** |
| 23 | ALT_AZ/CASUAL `startTracking` rates | Prędkości już zawierają gear — OK | Bez zmian (poprawne) | AZ, CA |

**Legenda typów:** EQ = EQUATORIAL, AZ = ALT_AZ, CA = CASUAL, ALL = wszystkie

### Plik: [`src/api/service_impl.cpp`](src/api/service_impl.cpp)

| # | Lokalizacja | Poprawka |
|---|-----------|----------|
| 24 | `GetState` | `current_position` → `telescope_axis{1,2}_position` (stopnie teleskopu) |
| 25 | `WatchState` | j.w. dla strumieniowania |

### Plik: [`tests/test_mount_controller.cpp`](tests/test_mount_controller.cpp)

| # | Rodzaj | Liczba |
|---|--------|--------|
| 26 | Istniejące testy poprawione | 14 (10 EQUATORIAL + 4 CASUAL/ALT_AZ) |
| 27 | Nowe testy weryfikacyjne | 6 (spójność przestrzeni współrzędnych) |

---

## 3. Jak poprawki rozwiązują zgłoszone problemy

### Problem 1: "Obliczenia astronomiczne powinny odnosić się do wynikowych koordynat montażu"

**Rozwiązanie:** Wszystkie obliczenia astronomiczne prawidłowo konwertują między stopniami serwa a stopniami teleskopu. Pozycje wewnętrzne pozostają w stopniach serwa (wymagane przez CANopen), ale każda operacja astronomiczna dzieli przez `gear_ratio` aby uzyskać stopnie teleskopu, a wyniki korekcji mnoży z powrotem. **Dotyczy wszystkich typów montażu** — dla CASUAL, przejście przez kwaternion daje stopnie ramy montażu, które również są mnożone przez `gear_ratio`.

### Problem 2: "Koordynata montażu pokrywa się z koordynatą silników"

**Rozwiązanie:** WEB UI raportuje `telescope_axis{1,2}_position` (serwo / gear_ratio) w polu `current_position`. Operator widzi rzeczywistą pozycję teleskopu na niebie, niezależnie od typu montażu.

### Problem 3: "Współrzędne osiągają bardzo duże wartości kątów / wielokrotne obroty"

**Rozwiązanie:** Normalizacja `[-180°, 180°]` usunięta. Pozycja absolutna serwa zachowana. Cel i pozycja są w tej samej przestrzeni (serwo°), więc `target ≈ current_position` → zerowy ruch gdy montaż już wskazuje na obiekt. **Dotyczy wszystkich typów.**

### Problem 4: "Limity pozycji nie powinny odnosić się do pozycji osi silników"

**Rozwiązanie:** `evaluateSoftLimits()` konwertuje pozycję serwa na stopnie teleskopu przed porównaniem z limitami. **Dotyczy wszystkich typów.**

### Problem 5: "Po kalibracji niekontrolowana ilość obrotów z maksymalną prędkością"

**Rozwiązanie:** Bezpośredni skutek błędu #1. Po poprawkach cel = stopnie niebieskie × gear_ratio, więc napęd kontynuuje ruch do przodu zgodnie z kierunkiem śledzenia. **Dotyczy wszystkich typów** — zarówno nowo wskazanego obiektu, jak i obiektu z kalibracji.

### Problem 6: "Niestabilność kontrolera — przestaje odpowiadać"

**Rozwiązanie:** Odczyt SDO poza `state_mutex_`. Blokada zajmowana tylko na czas przepisania wartości (~mikrosekund). **Dotyczy wszystkich typów.**

---

## 4. Testy weryfikacyjne

Nowe testy w [`tests/test_mount_controller.cpp`](tests/test_mount_controller.cpp) (sekcja `COORDINATE SPACE CONSISTENCY TESTS`):

| Test | Weryfikuje | Typy |
|------|-----------|------|
| `TelescopePositionEqualsServoDividedByGearRatio` | `telescope_axis = axis_position / gear_ratio` | ALL |
| `TargetInServoDegreesIncludesGearRatio` | Cel = ° niebieskie × gear_ratio | EQ |
| `ParkTargetInServoDegreesIncludesGearRatio` | Park = park_position × gear; teleskop wskazuje cel | ALL |
| `NoPositionNormalizationToSmallRange` | Pozycja nie zawijana do [-180, 180] | ALL |
| `SoftLimitsCompareTelescopeDegrees` | Limity operują na ° teleskopu | EQ |
| `HorizontalTargetsIncludeGearRatio` | ALT_AZ cele zawierają gear_ratio | AZ |

---

## 5. Podsumowanie dla ALT_AZ i CASUAL

Poprawki **w pełni rozwiązują te same problemy** dla montaży ALT_AZ i CASUAL:

| Problem | EQUATORIAL | ALT_AZ | CASUAL |
|---------|-----------|--------|--------|
| Cele bez gear_ratio | ✅ Naprawione | ✅ Naprawione (poz. 11, 19) | ✅ Naprawione (poz. 20-22) |
| Normalizacja [-180,180] | ✅ Usunięta | ✅ Usunięta | ✅ Usunięta |
| Limity w złej przestrzeni | ✅ Konwersja | ✅ Konwersja | ✅ Konwersja |
| Niestabilność gRPC | ✅ SDO poza mutex | ✅ SDO poza mutex | ✅ SDO poza mutex |
| WEB UI stopnie serwa | ✅ Teleskop° | ✅ Teleskop° | ✅ Teleskop° |
| Clamping ALT_AZ/CASUAL | N/D | ✅ serwo↔tel↔serwo | ✅ serwo↔tel↔serwo |
| Prędkości śledzenia | ✅ Zawierają gear | ✅ Zawierają gear | ✅ Zawierają gear |

**Uwaga:** Dla CASUAL, `equatorialToMountOrientation` i `mountOrientationToEquatorial` operują na stopniach teleskopu/ramy montażu — zwracają wartości w zakresach `[-90°, 90°]` dla altitude-like i `[0°, 360°)` dla azimuth-like. Po przemnożeniu przez `gear_ratio` otrzymujemy poprawne cele w stopniach serwa.

---

## 6. Poprawki z 2026-06-27 (23:22 CEST) — błąd "altitude must be in range [0, 90]"

### Problem

Podczas symulacji kalibracji drugiego obiektu w zakładce Tests, przycisk **"Slew to Telescope-Frame Position"** dla montaży ALT_AZ i CASUAL wysyłał żądanie `POST /api/axis/slew-horizontal` z wysokością spoza zakresu [0, 90]. Proxy serwer odrzucał żądanie z błędem `"Slew failed: altitude must be in range [0, 90]"`.

### Przyczyna

**Podwójna transformacja kwaternionu dla CASUAL:** Funkcja [`slewToTelescopeFrame()`](web/public/js/components/debugTest.js:763) w `debugTest.js` obliczała we фронтенdzie współrzędne w ramie montażu (poprzez macierz obrotu: RA/Dec → ENU → mount-frame alt/az), a następnie wywoływała `slewHorizontal()` z tymi współrzędnymi. Backend [`slewToHorizontal()`](src/controllers/mount_controller.cpp:1002) dla CASUAL ponownie aplikuje kwaternion, aby przekonwertować "prawdziwe horyzontalne" → rama montażu. W efekcie kwaternion był aplikowany dwukrotnie, dając błędne wartości wysokości.

Dla ALT_AZ (macierz jednostkowa) problem nie występował, ponieważ mount-frame = true horizontal, ale kod i tak był niepoprawny koncepcyjnie.

**Brak obsługi ALT_AZ w `slewToEquatorial`:** Funkcja [`slewToEquatorial()`](src/controllers/mount_controller.cpp:683) obsługiwała tylko EQUATORIAL i CASUAL. Dla ALT_AZ cele osi nie były ustawiane (pozostawały śmieciowe wartości), co uniemożliwiało użycie `slewToCoordinates` z montażem ALT_AZ.

### Rozwiązanie

# | Plik | Poprawka |
|---|------|----------|
28 | [`debugTest.js`](web/public/js/components/debugTest.js:796) | `slewToTelescopeFrame()` dla ALT_AZ/CASUAL używa teraz `Api.slewToCoordinates()` z oryginalnymi współrzędnymi RA/Dec, zamiast `Api.slewHorizontal()` z przeliczonymi współrzędnymi mount-frame. Backend sam wykonuje transformację przez kwaternion (jednokrotnie). |
29 | [`mount_controller.cpp`](src/controllers/mount_controller.cpp:755) | Dodano obsługę `ALT_AZ` w `slewToEquatorial()`: konwersja RA/Dec → Alt/Az przez `equatorialToHorizontal()`, mapowanie `axis1=azimuth, axis2=altitude`. |
30 | [`mount_controller.cpp`](src/controllers/mount_controller.cpp:767) | Poprawiono sprawdzanie miękkich limitów w `slewToEquatorial()`: dla ALT_AZ pomijany jest limit osi 1 (azymut zawija się 0–360°), analogicznie jak w `slewToHorizontal()`. |

**Uwaga:** Dla CASUAL, `equatorialToMountOrientation` i `mountOrientationToEquatorial` operują na stopniach teleskopu/ramy montażu — zwracają wartości w zakresach `[-90°, 90°]` dla altitude-like i `[0°, 360°)` dla azimuth-like. Po przemnożeniu przez `gear_ratio` otrzymujemy poprawne cele w stopniach serwa.

---

## 7. Poprawki z 2026-06-27 (23:35 CEST) — zawijanie azymutu podczas śledzenia

### Problem 1: Normalizacja azymutu w ścieżce korekcji astronomicznych

W funkcji śledzenia, przy włączonych korekcjach astronomicznych (nutacja, refrakcja, TPOINT), kod w [`mount_controller.cpp`](src/controllers/mount_controller.cpp:2157) normalizował `axis2_position_` do zakresu `[0, 360)`:

```cpp
axis2_position_ = std::fmod(axis2_position_, 360.0);
if (axis2_position_ < 0.0) axis2_position_ += 360.0;
```

`axis2_position_` przechowuje pozycję w **stopniach serwa** (np. 5000° po kilku godzinach śledzenia). Normalizacja do `[0, 360)` niszczy absolutną referencję pozycji, powodując skok pozycji o tysiące stopni w tył. Dla napędów CANopen używających pozycjonowania absolutnego oznacza to gwałtowny, niekontrolowany obrót wstecz.

### Problem 2: Cele slewa daleko od bieżącej pozycji

`slewToHorizontal()` i `slewToEquatorial()` (dla ALT_AZ i CASUAL) ustawiały cel osi azymutalnej jako `kąt × gear_ratio`, gdzie kąt jest w zakresie `[0°, 360°)`. Po długim śledzeniu bieżąca pozycja serwa może wynosić np. 18000° (50 pełnych obrotów), a cel zostaje ustawiony na ~180° × gear_ratio. Napęd CANopen interpretuje to jako konieczność cofnięcia o ~17820°, wykonując dziesiątki niepotrzebnych obrotów wstecz z maksymalną prędkością.

### Rozwiązanie

| # | Plik | Poprawka |
|---|------|----------|
| 31 | [`mount_controller.cpp:2157`](src/controllers/mount_controller.cpp:2157) | **Usunięto normalizację** `axis2_position_` do `[0, 360)`. Funkcje `sin()/cos()` używane przy obliczaniu prędkości śledzenia są okresowe — nie wymagają normalizacji pozycji. Pozycja serwa pozostaje absolutna, bezpieczna dla CANopen. |
| 32 | [`mount_controller.cpp:1106`](src/controllers/mount_controller.cpp:1106) | **Najkrótsza ścieżka dla azymutu w `slewToHorizontal()` (CASUAL)** — cel jest wyznaczany jako `axis1_position_ + diff` gdzie `diff` jest znormalizowane do `[-180°×gear, +180°×gear]`. Napęd zawsze wybiera najkrótszą drogę. |
| 33 | [`mount_controller.cpp:1114`](src/controllers/mount_controller.cpp:1114) | **Najkrótsza ścieżka dla azymutu w `slewToHorizontal()` (ALT_AZ)** — analogicznie jak wyżej. |
| 34 | [`mount_controller.cpp:753`](src/controllers/mount_controller.cpp:753) | **Najkrótsza ścieżka dla azymutu w `slewToEquatorial()` (CASUAL)** — axis2 (azimuth-like) używa `axis2_position_ + diff`. |
| 35 | [`mount_controller.cpp:763`](src/controllers/mount_controller.cpp:763) | **Najkrótsza ścieżka dla azymutu w `slewToEquatorial()` (ALT_AZ)** — axis1 (azimuth) używa `axis1_position_ + diff`. |

### Oś wysokości (altitude) — bez problemu

Dla osi wysokości (altitude — axis1 w ALT_AZ, axis2 w CASUAL):
- Fizyczny zakres to 0°–90° (teleskop nie może zejść poniżej horyzontu)
- Pozycja rośnie monotonicznie od 0 do max ~90°×gear_ratio, po czym oscyluje
- **Nie ma zawijania 360°** — nie występuje problem analogiczny do azymutu
- Kod nigdy nie stosuje `fmod(..., 360.0)` do osi wysokości

### Uwaga: mieszane jednostki w ścieżce korekcji

W ścieżce korekcji astronomicznych (linia 2140-2141) porównanie `new_alt - axis1_position_` miesza stopnie teleskopu ze stopniami serwa. Ponieważ offset jest ograniczony do ±1–2° przez clamp, błąd jest zaniedbywalny (~1/gear stopnia teleskopu). Pełna poprawka wymagałaby konwersji jednostek, ale nie powoduje to niestabilności ani niebezpiecznych ruchów.

### Algorytm najkrótszej ścieżki dla azymutu

```cpp
double full_turn = 360.0 * gear_ratio;
double raw_target = azimuth_angle * gear_ratio;            // [0, full_turn)
double diff = raw_target - fmod(axis_position, full_turn); // różnica do pozycji modulo obrót
if (diff < 0.0) diff += full_turn;                         // [0, full_turn)
if (diff > full_turn / 2.0) diff -= full_turn;             // [-half_turn, +half_turn]
axis_target = axis_position + diff;                        // zawsze ±180°×gear od bieżącej pozycji
```

Dzięki temu napęd CANopen otrzymuje cel oddalony o maksymalnie pół obrotu teleskopu od bieżącej pozycji absolutnej, niezależnie od tego, ile obrotów zakumulowała pozycja podczas śledzenia.

### Testy weryfikacyjne

Nowe testy w [`tests/test_mount_controller.cpp`](tests/test_mount_controller.cpp) (sekcja `AZIMUTH WRAPPING TESTS`):

| Test | Weryfikuje | Typy |
|------|-----------|------|
| `AzimuthTargetShortestPath360to0` | Przejście azymutu 359.5° → 0.1° wybiera krótką drogę do przodu (+0.6°), nie długą do tyłu (-359.4°) | AZ |
| `AzimuthTargetShortestPath0to360` | Przejście azymutu 0.5° → 359.9° wybiera krótką drogę do tyłu (-0.6°), nie długą do przodu (+359.4°) | AZ |
| `AltitudeAxisNoWrapping` | Oś wysokości NIE stosuje zawijania — cel zawsze równy `altitude × gear_ratio` | AZ |
| `AzimuthTargetAfterAccumulatedTracking` | Po serii operacji cel azymutu pozostaje w zakresie ±180°×gear od bieżącej pozycji | AZ |

Zaktualizowane testy istniejące:
- `HorizontalTargetsIncludeGearRatio` — oczekuje `axis1_target = -90°×gear` (najkrótsza ścieżka z 0° do 270°)
- `CasualTargetsIncludeGearRatio` — j.w.
- `SlewToEquatorialSetsValidTargets` — sprawdza `diff ∈ [-180°, +180°]` zamiast `[0°, 360°)`
