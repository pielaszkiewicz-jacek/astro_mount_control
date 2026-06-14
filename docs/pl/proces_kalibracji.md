# Proces kalibracji montażu astronomicznego

## Spis treści

- [Proces kalibracji montażu astronomicznego](#proces-kalibracji-montażu-astronomicznego)
  - [Spis treści](#spis-treści)
  - [1. Wprowadzenie](#1-wprowadzenie)
  - [2. Architektura systemu kalibracji](#2-architektura-systemu-kalibracji)
  - [3. Inicjalizacja pozycji i typy enkoderów](#3-inicjalizacja-pozycji-i-typy-enkoderów)
    - [Enkodery absolutne (`encoders_absolute: true`)](#enkodery-absolutne-encoders_absolute-true)
    - [Enkodery inkrementalne (`encoders_absolute: false`)](#enkodery-inkrementalne-encoders_absolute-false)
    - [Wpływ na proces kalibracji](#wpływ-na-proces-kalibracji)
  - [4. Kalibracja Bootstrap (wstępna)](#4-kalibracja-bootstrap-wstępna)
    - [4.1 Tryby kalibracji](#41-tryby-kalibracji)
    - [4.2 Algorytm Wahba/SVD](#42-algorytm-wahbasvd)
    - [4.3 Wsparcie dla typów montaży](#43-wsparcie-dla-typów-montaży)
    - [4.4 Przepływ kalibracji bootstrap](#44-przepływ-kalibracji-bootstrap)
  - [5. Kalibracja TPOINT (precyzyjna)](#5-kalibracja-tpoint-precyzyjna)
    - [5.1 Termy błędów TPOINT](#51-termy-błędów-tpoint)
    - [5.2 Progresywna ekspansja termów](#52-progresywna-ekspansja-termów)
    - [5.3 Algorytm dopasowania QR](#53-algorytm-dopasowania-qr)
    - [5.4 Przepływ kalibracji TPOINT](#54-przepływ-kalibracji-tpoint)
  - [6. Baza danych obiektów referencyjnych](#6-baza-danych-obiektów-referencyjnych)
  - [7. Zakładka Tests — mechanizmy testowania](#7-zakładka-tests--mechanizmy-testowania)
    - [7.1 Wybór typu montażu](#71-wybór-typu-montażu)
    - [7.2 Orientacja CASUAL (kąty osi → kwaternion)](#72-orientacja-casual-kąty-osi--kwaternion)
    - [7.3 Kąty Eulera i macierz rotacji](#73-kąty-eulera-i-macierz-rotacji)
    - [7.4 Transformacja współrzędnych obiektu referencyjnego](#74-transformacja-współrzędnych-obiektu-referencyjnego)
    - [7.5 Fizyczny slew serwomechanizmów](#75-fizyczny-slew-serwomechanizmów)
    - [7.6 Iniekcja błędów kalibracyjnych](#76-iniekcja-błędów-kalibracyjnych)
    - [7.7 Korekcje astrometryczne](#77-korekcje-astrometryczne)
    - [7.8 Rotacja pola widzenia](#78-rotacja-pola-widzenia)
    - [7.9 Akcje testowe](#79-akcje-testowe)
    - [7.10 Konsola wyjściowa](#710-konsola-wyjściowa)
  - [8. API gRPC dla kalibracji](#8-api-grpc-dla-kalibracji)
    - [Bootstrap](#bootstrap)
    - [TPOINT](#tpoint)
    - [Pomocnicze](#pomocnicze)
  - [9. System prędkości i przełożenie przekładni](#9-system-prędkości-i-przełożenie-przekładni)
    - [Wartości domyślne (dla `gear_ratio = 360:1`)](#wartości-domyślne-dla-gear_ratio--3601)
    - [Przełącznik osi w UI](#przełącznik-osi-w-ui)
    - [Format kątów DMS/HMS w UI](#format-kątów-dmshms-w-ui)
  - [10. Pliki źródłowe](#10-pliki-źródłowe)

---

## 1. Wprowadzenie

System kalibracji montażu astronomicznego realizuje dwustopniowy proces korekcji błędów wskazywania:

1. **Kalibracja Bootstrap** — wstępne, zgrubne wyrównanie. Określa orientację montażu względem nieba poprzez rozwiązanie problemu Wahby (SVD), znajdując optymalną macierz rotacji 3×3 między układem montażu a lokalnym układem horyzontalnym (ENU).

2. **Kalibracja TPOINT** — precyzyjny model błędów geometrycznych. Dopasowuje do 40+ parametrów opisujących błędy mechaniczne i optyczne montażu (nieprostopadłość osi, fleksja tubusu, błędy enkoderów, refrakcja itp.) przy użyciu dekompozycji QR.

Kalibracja **nie modyfikuje** pozycji serwomechanizmów — pracuje wyłącznie na zapisanych pomiarach.

---

## 2. Architektura systemu kalibracji

```
┌─────────────────────────────────────────────────────────────────┐
│                       WEB UI (Zakładki)                         │
│  ┌───────────────┐  ┌─────────────────┐  ┌───────────────────┐  │
│  │  Calibration  │  │      Tests      │  │     Database      │  │
│  │  (pomiary,    │  │  (symulacja,    │  │  (wyszukiwanie    │  │
│  │   kalibracja) │  │   testy błędów) │  │   obiektów)       │  │
│  └──────┬────────┘  └───────┬─────────┘  └────────┬──────────┘  │
│         │                   │                     │             │
│         └───────────────────┼─────────────────────┘             │
│                             │ gRPC                              │
└─────────────────────────────┼───────────────────────────────────┘
                              │
┌─────────────────────────────┼───────────────────────────────────┐
│                  C++ Backend (MountController)                  │
│                             │                                   │
│  ┌──────────────────────────┼────────────────────────────────┐  │
│  │              MountController::Impl                        │  │
│  │                                                           │  │
│  │  ┌─────────────────┐  ┌─────────────────┐                 │  │
│  │  │ Bootstrap       │  │ TPOINT          │                 │  │
│  │  │ • Wahba/SVD     │  │ • QR fitting    │                 │  │
│  │  │ • 3+ pomiarów   │  │ • 3–28+ pomiarów│                 │  │
│  │  │ • Kwaternion Q  │  │ • 40+ parametrów│                 │  │
│  │  └─────────────────┘  └─────────────────┘                 │  │
│  │                                                           │  │
│  │  ┌──────────────────────────────────────────────────────┐ │  │
│  │  │        AstronomicalCalculations (SOFA)               │ │  │
│  │  │  • equatorialToHorizontal / horizontalToEquatorial   │ │  │
│  │  │  • applyNutation / applyAtmosphericRefraction        │ │  │
│  │  │  • equatorialToMountOrientation (CASUAL)             │ │  │
│  │  └──────────────────────────────────────────────────────┘ │  │
│  └───────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

---

## 3. Inicjalizacja pozycji i typy enkoderów

Pozycje serwomechanizmów są ustawiane **tylko raz** podczas [`initialize()`](src/controllers/mount_controller.cpp:270), w zależności od typu enkoderów:

### Enkodery absolutne (`encoders_absolute: true`)

```cpp
// Linia 273-287: Odczyt rzeczywistej pozycji fizycznej z hardware'u
if (config_.encoders_absolute) {
    if (hal_axis1_encoder_ && hal_axis2_encoder_) {
        auto enc1 = hal_axis1_encoder_->read();
        auto enc2 = hal_axis2_encoder_->read();
        axis1_position_ = enc1.position_deg;
        axis2_position_ = enc2.position_deg;
        encoder_absolute_ = true;
    } else {
        // Fallback: brak enkodera → (0, 0)
        axis1_position_ = 0.0;
        axis2_position_ = 0.0;
    }
}
```

Pozycja startowa pochodzi bezpośrednio z fizycznych enkoderów — mount "wie" gdzie jest po restarcie.

### Enkodery inkrementalne (`encoders_absolute: false`)

```cpp
// Linia 288-301: Start od skonfigurowanej pozycji parkowania
else {
    axis1_position_ = config_.park_position_axis1;  // domyślnie 0.0°
    axis2_position_ = config_.park_position_axis2;  // domyślnie 90.0° (NCP)
}
```

Po restarcie mount zaczyna od pozycji parkowania. Kalibracja bootstrap (Wahba/SVD) później określi offset rotacji między układem montażu a rzeczywistym układem horyzontalnym, absorbując nieznany offset enkodera.

### Wpływ na proces kalibracji

| Typ enkodera | Pozycja startowa | Wymagana kalibracja bootstrap |
|---|---|---|
| Absolutny | Z hardware'u | Opcjonalna (do korekcji drobnych błędów) |
| Inkrementalny | Z pozycji parkowania | **Wymagana** (do ustalenia orientacji) |

---

## 4. Kalibracja Bootstrap (wstępna)

Kalibracja bootstrap określa wstępną orientację montażu poprzez rozwiązanie problemu Wahby — znalezienie optymalnej macierzy rotacji **R** między układem montażu a układem horyzontalnym (ENU).

### 4.1 Tryby kalibracji

| Tryb | Wartość | Opis |
|---|---|---|
| `BOOTSTRAP_MANUAL` | 0 | Użytkownik ręcznie wskazuje obiekty (gamepad), dodaje pomiary przez UI |
| `BOOTSTRAP_HYBRID` | 1 | Pierwsze 3 pomiary manualne, potem automatyczne slew-y |
| `BOOTSTRAP_AUTOMATIC` | 2 | W pełni automatyczny z plate solverem (wymaga enkoderów absolutnych) |

### 4.2 Algorytm Wahba/SVD

Algorytm (zaimplementowany w [`runBootstrapCalibration()`](src/controllers/mount_controller.cpp:2923)):

```
Dla każdego pomiaru:
  1. Konwersja (observed_ra, observed_dec) → rzeczywisty horyzontalny (alt, az)
     z uwzględnieniem refrakcji atmosferycznej
  2. Konwersja (alt, az) → wektor jednostkowy ENU:
     horiz_vec = [sin(az)·cos(alt), cos(az)·cos(alt), sin(alt)]ᵀ
  3. Konwersja pozycji montażu (mount_ha, mount_dec) → wektor w układzie montażu:
     mount_vec = [sin(a2)·cos(a1), cos(a2)·cos(a1), sin(a1)]ᵀ
  4. Akumulacja macierzy kowariancji:
     B = Σ mount_vec · horiz_vecᵀ

SVD(B) = U · S · Vᵀ
Optymalna rotacja: R = V · Uᵀ (z korektą znaku dla det(R) = +1)

Konwersja R → kwaternion Q = [qx, qy, qz, qw]
```

**Wymagania pomiarowe:**
- Minimum 2 pomiary (zalecane ≥ 3 dla stabilności numerycznej)
- Pomiary powinny być rozłożone po niebie (collinear measurements → ill-conditioned SVD)
- Condition number macierzy B musi być ≥ 1e-6

### 4.3 Wsparcie dla typów montaży

Algorytm Wahba/SVD działa uniwersalnie dla wszystkich typów montaży:

| Typ montażu | mount_vec | horiz_vec | Rotacja R |
|---|---|---|---|
| EQUATORIAL | (HA, Dec) → wektor | (RA, Dec) @ czas → ENU | Absorbuje offset enkodera |
| ALT_AZ | (Az, Alt) → wektor | (RA, Dec) @ czas → ENU | Aproksymuje offset alt |
| CASUAL | (axis1, axis2) → wektor | (RA, Dec) @ czas → ENU | Pełna rotacja 3-DOF |

Dla ALT_AZ: offset enkodera wysokości jest translacją na sferze, nie czystą rotacją. Wahba/SVD znajduje najlepszą aproksymację rotacyjną. Dla offsetów > 10° zalecana jest kalibracja TPOINT po bootstrapie.

### 4.4 Przepływ kalibracji bootstrap

```
┌──────────────────────────────────────────────────────────────┐
│ 1. Wybór trybu (MANUAL / HYBRID / AUTOMATIC)                 │
│    → SetBootstrapMode RPC                                    │
├──────────────────────────────────────────────────────────────┤
│ 2. Dodawanie pomiarów                                        │
│    • Wyszukaj obiekt w bazie danych                          │
│    • Wybierz obiekt → auto-wypełnienie RA/Dec                │
│    • SlewToCoordinates → mount ustawia się na obiekt         │
│    • AddBootstrapMeasurement → zapisuje observed ra/dec      │
│      oraz mount_ha/mount_dec (z enkoderów)                   │
│    • Powtórz dla ≥ 3 obiektów rozłożonych po niebie          │
├──────────────────────────────────────────────────────────────┤
│ 3. Uruchomienie kalibracji                                   │
│    → RunBootstrapCalibration RPC                             │
│    → Wahba/SVD → kwaternion Q                                │
│    → mount_orientation_ = Q                                  │
│    → bootstrap_calibrated_ = true                            │
├──────────────────────────────────────────────────────────────┤
│ 4. Weryfikacja                                               │
│    → RMS error w arcsec (błąd residualny)                    │
│    → Alignment error (całkowity błąd wskazywania)            │
│    → Ready for TPOINT (≥ 3 pomiary)                          │
└──────────────────────────────────────────────────────────────┘
```

---

## 5. Kalibracja TPOINT (precyzyjna)

Kalibracja TPOINT buduje precyzyjny model błędów geometrycznych montażu, dopasowując do 40+ parametrów metodą najmniejszych kwadratów (QR decomposition).

### 5.1 Termy błędów TPOINT

| Symbol | Nazwa | Opis | Jednostka |
|---|---|---|---|
| IH | Index Error HA | Stały offset w osi godzinnej | arcsec |
| ID | Index Error Dec | Stały offset w osi deklinacji | arcsec |
| NP | Axis Non-Perpendicularity | Nieprostopadłość osi HA/Dec | arcsec |
| CH | Collimation Error | Błąd kolimacji (offset optyczny) | arcsec |
| MA | Polar Altitude Error (ME) | Błąd wysokości osi polarnej | arcsec |
| ME | Polar Azimuth Error (MA) | Błąd azymutu osi polarnej | arcsec |
| TF | Tube Flexure (HA) | Fleksja tubusu zależna od HA | arcsec/rad |
| HF | Tube Flexure (Dec) | Fleksja tubusu zależna od Dec | arcsec/rad |
| RF | Refraction Coefficient | Współczynnik korekcji refrakcji | — |
| TR | Tube Rotation | Rotacja tubusu | arcsec |

Dodatkowe termy (pełny zestaw ~40 parametrów):
- Harmoniczne błędu ślimacznicy (worm period error + 8 harmonicznych)
- Harmoniczne enkoderów HA i Dec
- Współczynniki temperaturowe (fleksja, enkodery, gear error)
- Współczynniki refrakcji (temperaturowy, ciśnieniowy)

### 5.2 Progresywna ekspansja termów

System automatycznie zwiększa liczbę dopasowywanych parametrów w miarę dostępności pomiarów:

| Poziom | Min. pomiarów | Dopasowywane termy |
|---|---|---|
| **Level 0** | 3–9 | IH, ID, NP (direct QR, 3 param) |
| **Level 1** | 10–11 | IH (Index Error tylko, ~1 param) |
| **Level 2** | 12–13 | IH, NP, MA, ME, TF, HF (~6 param) |
| **Level 3** | 14–19 | Level 2 + CH + TR (~8 param) |
| **Level 4** | 20–27 | Level 3 + RF (standard terms, ~14 param) |
| **Level 5** | 28+ | Pełny zestaw zdefiniowany przez użytkownika (do ~40 param) |

Górnym ograniczeniem jest `tpoint_enabled_terms` z konfiguracji — kontroler nigdy nie włącza więcej termów niż użytkownik jawnie dopuścił.

### 5.3 Algorytm dopasowania QR

```cpp
// Level 0 (3-9 pomiarów) — bezpośredni QR (src/controllers/mount_controller.cpp:3456)
// Budowa macierzy układu:
//   Dla każdego pomiaru i:
//     A[2i,   0] = 1.0     (IH — stały offset RA)
//     A[2i,   1] = 0.0     (ID — nie wpływa na RA)
//     A[2i,   2] = 0.0     (NP — nie wpływa na RA)
//     A[2i+1, 0] = 0.0     (IH — nie wpływa na Dec)
//     A[2i+1, 1] = 1.0     (ID — stały offset Dec)
//     A[2i+1, 2] = ha_deg  (NP · HA)
//
//     b[2i]   = (observed_ra - expected_ra) · 15  [deg]
//     b[2i+1] = observed_dec - expected_dec       [deg]
//
// Rozwiązanie: ColPivHouseholderQR(A).solve(b) → [IH, ID, NP]

// Level 1-5 (≥10 pomiarów) — TPointModel::fitModel()
// Używa wewnętrznego solvera QR z progresywną ekspansją termów
```

### 5.4 Przepływ kalibracji TPOINT

```
┌─────────────────────────────────────────────────────────────┐
│ 1. Wymaganie wstępne: bootstrap_calibrated = true           │
│    (dla precyzyjnych współrzędnych enkoderów)               │
├─────────────────────────────────────────────────────────────┤
│ 2. Dodawanie pomiarów (analogicznie do bootstrap)           │
│    • AddTPointMeasurement → pełne dane:                     │
│      - observed_ra/dec (z plate solvers)                    │
│      - expected_ra/dec (z katalogu + proper motion)         │
│      - mount_ha/dec (z skalibrowanych enkoderów)            │
│      - temperatura, ciśnienie, wilgotność                   │
│      - proper motion, paralaksa, epoka                      │
│    • Minimum 3 pomiary, zalecane ≥ 20                       │
├─────────────────────────────────────────────────────────────┤
│ 3. Uruchomienie kalibracji                                  │
│    → RunTPointCalibration RPC                               │
│    → Progresywna ekspansja termów (Level 0→5)               │
│    → QR fitting → parametry modelu                          │
│    → tpoint_calibrated_ = true                              │
├─────────────────────────────────────────────────────────────┤
│ 4. Weryfikacja                                              │
│    → RMS error [arcsec]                                     │
│    → Max residual [arcsec]                                  │
│    → Chi-squared                                            │
│    → Condition number                                       │
│    → Outlier detection (> 5·RMS)                            │
│    → Parametry uncertainty                                  │
└─────────────────────────────────────────────────────────────┘
```

---

## 6. Baza danych obiektów referencyjnych

Zakładka **Database** w UI dostarcza wyszukiwarkę obiektów astronomicznych dla pomiarów kalibracyjnych.

- **API**: [`/api/db/search`](web/proxy/server.js) → gRPC `SearchObjects`
- **Kryteria wyszukiwania**: nazwa, katalog (NGC, IC, Messier), współrzędne, typ obiektu
- **Wyniki**: RA [h], Dec [°], magnituda V, typ obiektu, ID katalogowe

Przepływ użycia:
1. Wpisz nazwę lub ID katalogowy w polu wyszukiwania
2. Wybierz obiekt z listy wyników
3. Współrzędne RA/Dec są automatycznie wypełniane
4. Kliknij "Slew & Measure" aby skierować montaż na obiekt i dodać pomiar
5. Lub "Add Measurement" aby dodać pomiar bez slewa (używa bieżącej pozycji)

---

## 7. Zakładka Tests — mechanizmy testowania

Zakładka **Tests** ([`debugTest.js`](web/public/js/components/debugTest.js)) zapewnia zestaw narzędzi do symulacji, testowania i debugowania procesu kalibracji bez konieczności fizycznego montażu.

### 7.1 Wybór typu montażu

Selektor [`#debug-mount-type`](web/public/index.html) umożliwia przełączanie między:

| Typ | Wartość | Etykiety osi |
|---|---|---|
| EQUATORIAL | 0 | HA (oś godzinna) / Dec (deklinacja) |
| ALT_AZ | 1 | Az (azymut) / Alt (wysokość) |
| CASUAL | 3 | Axis1 (altitude-like) / Axis2 (azimuth-like) |

Zmiana typu automatycznie:
- Pokazuje/ukrywa sekcję orientacji CASUAL
- Aktualizuje etykiety osi
- Ustawia predefiniowane kąty Eulera (EQUATORIAL: pitch = 90°−lat, ALT_AZ: identity)

### 7.2 Orientacja CASUAL (kąty osi → kwaternion)

Sekcja [`#debug-casual-orient`](web/public/index.html) pozwala zdefiniować orientację montażu CASUAL poprzez:

1. **Altitude axis1** — wysokość osi 1 w stopniach (0° = horyzont, 90° = zenit)
2. **Azimuth axis1** — azymut osi 1 w stopniach (0° = północ, 90° = wschód)

Algorytm (identyczny z C++ [`MountOrientation::setFromAxisAngles()`](src/controllers/mount_controller.cpp:173)):
```javascript
// Konwersja ENU axis1 direction → kwaternion Q
// Q · (0,0,1) · Q* = ENU_axis1
// ENU_axis1 = (cos(alt)·cos(az), cos(alt)·sin(az), sin(alt))
// v = (0,0,1) × ENU_axis1 = (-east, north, 0)
// s = 1 + up
// Q = conjugate(Q_from_a_to_b) / norm
```

Po kliknięciu **Apply Orientation** kwaternion jest wysyłany do kontrolera przez `SetMountOrientation` RPC.

### 7.3 Kąty Eulera i macierz rotacji

Sekcja Euler Angles zapewnia wizualną reprezentację macierzy rotacji 3×3:

- **Konwencja**: ZYX intrinsic (yaw → pitch → roll)
- **R = Rz(yaw) · Ry(pitch) · Rx(roll)**
- Macierz jest wyświetlana w tabeli 3×3 i automatycznie konwertowana na kwaternion
- Przyciski: **Apply to Controller** (wysyła kwaternion), **Reset to Mount Defaults**

Predefiniowane wartości dla typów montaży:
- **EQUATORIAL**: yaw = 0°, pitch = 90° − latitude, roll = 0°
- **ALT_AZ**: yaw = 0°, pitch = 0°, roll = 0° (identity)
- **CASUAL**: użytkownik definiuje wszystkie trzy kąty

### 7.4 Transformacja współrzędnych obiektu referencyjnego

Sekcja Reference Object umożliwia transformację współrzędnych niebieskich (RA/Dec) na współrzędne w układzie teleskopu:

```
Kroki algorytmu:
1. RA/Dec → wektor jednostkowy w układzie niebieskim (ICRS)
2. Wektor niebieski → ENU (przez LST i szerokość geograficzną)
3. ENU → układ montażu (przez macierz rotacji R)
4. Wektor w układzie montażu → kąty osi (axis1, axis2)
```

Przyciski:
- **Transform** — oblicza i wyświetla współrzędne w układzie teleskopu
- **Slew to Telescope Frame** — wysyła komendy fizycznego slewa do serwomechanizmów
- **Import from Calibration** — importuje obiekt wybrany w zakładce Calibration

### 7.5 Fizyczny slew serwomechanizmów

Funkcja [`slewToTelescopeFrame()`](web/public/js/components/debugTest.js:739):

1. Pobiera aktualną pozycję z live statusu (`App.getLastState()`)
2. Oblicza offset: `target - current`
3. Normalizuje offset osi 1 do [-180°, 180°] dla najkrótszej ścieżki
4. Wysyła `moveAxisRelative(axisId, offset)` dla obu osi

### 7.6 Iniekcja błędów kalibracyjnych

Sekcja Calibration Error Injection pozwala symulować błędy mechaniczne dla testowania TPOINT:

| Pole | Opis |
|---|---|
| Polar Alt Error | Błąd wysokości osi polarnej [°] |
| Polar Az Error | Błąd azymutu osi polarnej [°] |
| HA/Dec Non-Perp | Nieprostopadłość osi [°] |
| Collimation | Błąd kolimacji [°] |
| Tube Flexure | Fleksja tubusu [°] |
| Backlash | Luz mechaniczny [°] |

Przycisk **Inject Errors** modyfikuje konfigurację (latitude + polar_alt_error, longitude + polar_az_error) przez `UpdateConfiguration`. Przycisk **Clear Errors** resetuje konfigurację do fabrycznej i zeruje pola formularza.

### 7.7 Korekcje astrometryczne

Sekcja Astrometric Corrections implementuje pełny pipeline korekcji astronomicznych po stronie JavaScript (bez wywołań backendu):

1. **Proper motion** — ruch własny gwiazdy od epoki J2000 do bieżącej daty
2. **Precession** (IAU 2006) — precesja współrzędnych (3-kątowa formuła: ζ, z, θ)
3. **Nutation** (IAU 2000A) — nutacja (dominujące 7 termów: 18.6-letni węzeł księżycowy, półroczny, dwutygodniowy, miesięczny)
4. **Annual aberration** — aberracja roczna (prędkość orbitalna Ziemi)
5. **Atmospheric refraction** (Saemundsson) — refrakcja atmosferyczna

Wyniki: `Δα` i `Δδ` w sekundach kątowych, apparent RA/Dec, wysokość i refrakcja.

### 7.8 Rotacja pola widzenia

Sekcja Field Rotation oblicza parametry rotacji pola:

- **Parallactic angle** q = atan2(sin(HA), tan(lat)·cos(dec) − sin(dec)·cos(HA))
- **Field rotation rate** = −cos(lat)·cos(az)/cos(alt) · ω_earth [°/h]
- Dla CASUAL: korekcja o yaw montażu z macierzy rotacji

Przyciski: **Compute**, **Enable/Disable**, **Refresh Derotator Status**.

### 7.9 Akcje testowe

| Przycisk | Akcja |
|---|---|
| **Slew to Equatorial** | Slew na RA=10.5h, Dec=41.27° (okolice Andromedy) |
| **Slew to Horizontal** | Slew na Alt=45°, Az=180° |
| **Start Tracking** | Tracking Polaris (RA=2.53h, Dec=89.26°), SIDEREAL |
| **Meridian Flip Setup** | Konfiguruje meridian flip (enabled, delay=0) |
| **Inject Errors** | Aplikuje błędy kalibracyjne z formularza |
| **Clear Errors** | Resetuje konfigurację do fabrycznej |
| **Dump State** | Wyświetla pełny stan kontrolera i konfigurację |
| **Reset Simulation** | Pełny reset: konfiguracja, pozycje (0,0), orientacja (identity), mount type (CASUAL) |

### 7.10 Konsola wyjściowa

Wszystkie operacje logują szczegółowe informacje do konsoli [`#debug-output`](web/public/index.html):
- Timestamp w formacie ISO 8601
- Prefix `[ERROR]` dla błędów
- Auto-scroll do najnowszego wpisu
- Przycisk **Clear Output** do wyczyszczenia

---

## 8. API gRPC dla kalibracji

### Bootstrap

| RPC | Request | Response | Opis |
|---|---|---|---|
| `AddBootstrapMeasurement` | `BootstrapMeasurement` | `Empty` | Dodaje pomiar bootstrap |
| `RunBootstrapCalibration` | `Empty` | `BootstrapCalibrationResult` | Uruchamia kalibrację Wahba/SVD |
| `GetBootstrapStatus` | `Empty` | `BootstrapStatus` | Pobiera status kalibracji |
| `ClearBootstrapMeasurements` | `Empty` | `Empty` | Czyści wszystkie pomiary |
| `SetBootstrapMode` | `BootstrapModeRequest` | `Empty` | Ustawia tryb (MANUAL/HYBRID/AUTO) |
| `RunAutomaticBootstrap` | `AutoBootstrapRequest` | `Empty` | Uruchamia automatyczny bootstrap |

### TPOINT

| RPC | Request | Response | Opis |
|---|---|---|---|
| `AddTPointMeasurement` | `Measurement` | `Empty` | Dodaje pomiar TPOINT |
| `RunTPointCalibration` | `Empty` | `Empty` | Uruchamia dopasowanie QR |
| `GetTPointParameters` | `Empty` | `TPointParameters` | Pobiera parametry modelu |
| `ClearTPointMeasurements` | `Empty` | `Empty` | Czyści wszystkie pomiary |

### Pomocnicze

| RPC | Opis |
|---|---|
| `SearchObjects` | Wyszukiwanie obiektów w bazie danych |
| `SetMountOrientation` | Ustawienie kwaternionu orientacji (CASUAL) |
| `GetMountOrientation` | Pobranie aktualnego kwaternionu |
| `UpdateConfiguration` | Aktualizacja konfiguracji (merge z istniejącą) |
| `SlewToCoordinates` | Slew na współrzędne equatorialne |
| `ControlAxis` / `moveAxisRelative` | Sterowanie pojedynczą osią |

---

## 9. System prędkości i przełożenie przekładni

Wszystkie prędkości w systemie (`max_slew_rate`, `max_tracking_rate`) wyrażone są w **stopniach na sekundę wału serwosilnika**. Przelicznik na prędkość osi teleskopu:

```
prędkość_teleskopu [°/s] = prędkość_serwa [°/s] / gear_ratio
```

### Wartości domyślne (dla `gear_ratio = 360:1`)

| Parametr konfiguracji | Serwo | Teleskop | Znaczenie |
|---|---|---|---|
| `max_slew_rate` | 720.0 °/s | 2.0 °/s | Maks. prędkość podczas slewa |
| `max_tracking_rate` | 1.504 °/s | 0.004178 °/s (15.04 "/s) | Prędkość śledzenia gwiazdowego |
| `slew_acceleration` | 1.0 °/s² | 0.0028 °/s² | Przyspieszenie slewa |
| `tracking_acceleration` | 0.001 °/s² | 2.8×10⁻⁶ °/s² | Przyspieszenie śledzenia |

### Przełącznik osi w UI

Zakładka Sterowanie zawiera checkbox **"Oś teleskopu (°/s)"**:

- **Odznaczony**: suwak i krok w jednostkach **serwosilnika**
- **Zaznaczony**: suwak i krok w jednostkach **osi teleskopu** — wartości automatycznie mnożone przez `gear_ratio` przed wysłaniem do API

Przełożenia (`ha_axis_params.gear_ratio`, `dec_axis_params.gear_ratio`) są pobierane z backendu i stosowane osobno dla każdej osi.

### Format kątów DMS/HMS w UI

Wszystkie pola kątowe w interfejsie (RA, Dec, pozycje osi, kąty Eulera, współrzędne geograficzne) obsługują format **stopnie/minuty/sekundy** (DMS) i **godziny/minuty/sekundy** (HMS):

| Typ pola | Przykład wyświetlania | Akceptowane formaty wprowadzania |
|---|---|---|
| RA (godziny) | `10h 30m 45.1s` | `10h30m45.1s`, `10:30:45.1`, `10 30 45.1`, `10.5125` |
| Dec (stopnie) | `+41° 16' 12.3"` | `+41°16'12.3"`, `41:16:12.3`, `41 16 12.3`, `41.27` |
| Kąt (stopnie) | `152° 33' 00.0"` | `152°33'0"`, `152:33:0`, `152 33 0`, `152.55` |

Format DMS/HMS jest używany zarówno przy wyświetlaniu jak i przy edycji — nie ma przełączania na format dziesiętny podczas focusu. Implementacja w [`utils.js`](web/public/js/utils.js) (funkcje `parseDMS`, `parseHMS`, `formatAngleDeg`, `formatRA`, `formatDec`, `enhanceAngleInput`).

---

## 10. Pliki źródłowe

| Plik | Odpowiedzialność |
|---|---|
| [`src/controllers/mount_controller.cpp`](src/controllers/mount_controller.cpp) | Implementacja `runBootstrapCalibration()`, `runTPointCalibration()`, `addBootstrapMeasurement()`, `addTPointMeasurement()` |
| [`include/controllers/mount_controller.h`](include/controllers/mount_controller.h) | Deklaracje API kalibracji, `MountOrientation`, `BootstrapMode`, `AxisPhysicalParameters` |
| [`src/api/service_impl.cpp`](src/api/service_impl.cpp) | Handlery gRPC dla wszystkich RPC kalibracyjnych |
| [`include/models/tpoint_model.h`](include/models/tpoint_model.h) | Interfejs `TPointModel` |
| [`src/models/tpoint_model.cpp`](src/models/tpoint_model.cpp) | Implementacja dopasowania QR, termy błędów, `applyCorrections()` |
| [`include/core/astronomical_calculations.h`](include/core/astronomical_calculations.h) | Transformacje współrzędnych, refrakcja, nutacja |
| [`web/public/js/components/calibration.js`](web/public/js/components/calibration.js) | UI zakładki Calibration |
| [`web/public/js/components/debugTest.js`](web/public/js/components/debugTest.js) | UI zakładki Tests — symulacja i testowanie |
| [`web/public/js/components/database.js`](web/public/js/components/database.js) | UI zakładki Database — wyszukiwanie obiektów |
| [`proto/mount_controller.proto`](proto/mount_controller.proto) | Definicje komunikatów protobuf |
