# Instrukcja kalibracji montażu — przewodnik użytkownika

## Spis treści

- [Instrukcja kalibracji montażu — przewodnik użytkownika](#instrukcja-kalibracji-montażu--przewodnik-użytkownika)
  - [Spis treści](#spis-treści)
  - [1. Wprowadzenie](#1-wprowadzenie)
    - [Kiedy kalibrować?](#kiedy-kalibrować)
  - [2. Przygotowanie](#2-przygotowanie)
    - [2.5 Ustalenie punktu referencyjnego (Home)](#25-ustalenie-punktu-referencyjnego-home)
      - [Jak to działa?](#jak-to-działa)
      - [Sekcja Home w interfejsie](#sekcja-home-w-interfejsie)
      - [Zachowanie zależnie od typu montażu](#zachowanie-zależnie-od-typu-montażu)
      - [Procedura krok po kroku](#procedura-krok-po-kroku)
  - [3. Kalibracja Bootstrap (wstępna)](#3-kalibracja-bootstrap-wstępna)
    - [3.1 Tryb ręczny (MANUAL)](#31-tryb-ręczny-manual)
      - [Krok 1: Wybierz tryb](#krok-1-wybierz-tryb)
      - [Krok 2: Znajdź obiekt referencyjny](#krok-2-znajdź-obiekt-referencyjny)
      - [Krok 3: Skieruj teleskop na obiekt](#krok-3-skieruj-teleskop-na-obiekt)
      - [Krok 4: Dodaj pomiar](#krok-4-dodaj-pomiar)
      - [Krok 5: Uruchom kalibrację](#krok-5-uruchom-kalibrację)
    - [3.2 Tryb hybrydowy (HYBRID)](#32-tryb-hybrydowy-hybrid)
    - [3.3 Tryb automatyczny (AUTOMATIC)](#33-tryb-automatyczny-automatic)
    - [3.4 Weryfikacja wyniku](#34-weryfikacja-wyniku)
  - [4. Kalibracja TPOINT (precyzyjna)](#4-kalibracja-tpoint-precyzyjna)
    - [4.1 Zbieranie pomiarów](#41-zbieranie-pomiarów)
    - [4.2 Uruchomienie kalibracji](#42-uruchomienie-kalibracji)
    - [4.3 Interpretacja wyników](#43-interpretacja-wyników)
  - [5. Przykład pełnej sesji kalibracyjnej](#5-przykład-pełnej-sesji-kalibracyjnej)
    - [Sesja (ok. 45 minut)](#sesja-ok-45-minut)
  - [6. Rozwiązywanie problemów](#6-rozwiązywanie-problemów)
    - [Bootstrap nie chce się uruchomić](#bootstrap-nie-chce-się-uruchomić)
    - [TPOINT daje słabe wyniki](#tpoint-daje-słabe-wyniki)
    - [Ogólne problemy](#ogólne-problemy)
  - [7. Słownik pojęć](#7-słownik-pojęć)

---

## 1. Wprowadzenie

Kalibracja montażu astronomicznego to proces określania dokładnej orientacji montażu względem nieba. Składa się z dwóch etapów:

| Etap | Cel | Dokładność | Czas |
|---|---|---|---|
| **Bootstrap** | Wstępne wyrównanie — znalezienie orientacji montażu | ~1 arcmin | 5–15 min |
| **TPOINT** | Precyzyjny model błędów mechanicznych | <1 arcsec | 20–60 min |

Kalibracja **nie zmienia** fizycznej pozycji montażu — jedynie oblicza poprawki matematyczne. Po kalibracji montaż będzie celniej wskazywał obiekty na niebie.

### Kiedy kalibrować?

- Po pierwszym uruchomieniu systemu
- Po przeniesieniu montażu w inne miejsce
- Po wymianie lub regulacji elementów optycznych/mechanicznych
- Gdy zauważysz spadek dokładności wskazywania (obiekty nie są wyśrodkowane)

---

## 2. Przygotowanie

Przed rozpoczęciem kalibracji:

1. **Uruchom kontroler** — `./build/bin/astro_mount_controller config/default.json`
2. **Uruchom interfejs webowy** — otwórz `http://localhost:8080` w przeglądarce
3. **Sprawdź połączenie** — wskaźnik w prawym górnym rogu powinien pokazywać "Connected"
4. **Skonfiguruj lokalizację** — przejdź do zakładki **Settings**, rozwiń "Mount Location" i ustaw:
   - `Latitude` — szerokość geograficzna (np. `52.0` dla Warszawy)
   - `Longitude` — długość geograficzna (np. `21.0` dla Warszawy)
5. **Sprawdź typ enkoderów** — w zakładce **Settings** → "Mount Encoders":
   - Jeśli masz enkodery absolutne — zaznacz "Absolute Encoders"
   - Jeśli masz enkodery inkrementalne — pozostaw odznaczone

> **Uwaga:** Przy enkoderach inkrementalnych po restarcie kontrolera pozycja startowa to pozycja parkowania (domyślnie HA=0°, Dec=0°). Kalibracja bootstrap skoryguje ten offset.

6. **Wykonaj operację Home** — w zakładce **Control** znajdź sekcję **Home**, wprowadź współrzędne obu osi i kliknij przycisk 🏠 (szczegóły w [sekcji 2.5](#25-ustalenie-punktu-referencyjnego-home)).

---

### 2.5 Ustalenie punktu referencyjnego (Home)

Po uruchomieniu kontrolera, enkodery absolutne CANopen zgłaszają **losowe pozycje bezwzględne** — nie odpowiadają one rzeczywistemu położeniu montażu na niebie. Bez korekty, montaż w ciągu kilku sekund śledzenia osiągnie limity kątów i przejdzie w stan ERROR.

Operacja **Home** rozwiązuje ten problem: ustawia wewnętrzny punkt referencyjny kontrolera tak, aby zgadzał się z fizycznym położeniem teleskopu. **Home nie porusza montażem** — użytkownik musi najpierw ręcznie skierować teleskop na znany obiekt.

#### Jak to działa?

Funkcja `Home(axis1, axis2)` przyjmuje współrzędne w **stopniach teleskopu** (po przełożeniu przekładni). Kontroler konwertuje je wewnętrznie na stopnie serwa (`× gear_ratio`), zapisuje jako bieżącą pozycję i resetuje filtr Kalmana, flagi meridian flip oraz prędkości do zera.

Po wykonaniu Home:
- **Śledzenie** działa poprawnie — używa przyrostów względnych (`pozycja += prędkość × dt`)
- **Limity miękkie** są liczone od poprawnej pozycji teleskopu
- **Detekcja południka** (meridian flip) działa na podstawie poprawnego HA
- **Wyświetlanie w zakładce Status** pokazuje poprawne współrzędne teleskopu

#### Sekcja Home w interfejsie

W zakładce **Control** znajduje się wydzielona sekcja **Home**, zawierająca:

```
┌─────────────────────────────────────────────────┐
│ 🏠 Home                                          │
│                                                 │
│ Ustaw punkt referencyjny układu współrzędnych.  │
│ Home nie porusza montażem — przed użyciem       │
│ skieruj fizycznie teleskop na znany obiekt.     │
│                                                 │
│ Oś 1 (HA / Wysokość):                           │
│ ┌─────────────────────────┐                     │
│ │ 0.000                   │ [°]                │
│ └─────────────────────────┘                     │
│                                                 │
│ Oś 2 (Dec / Azymut):                            │
│ ┌─────────────────────────┐                     │
│ │ 90.000                  │ [°]                │
│ └─────────────────────────┘                     │
│                                                 │
│ [ 🏠 Home ]                                     │
└─────────────────────────────────────────────────┘
```

| Element | Opis |
|---|---|
| **Oś 1 (HA / Wysokość)** | Pole do wpisania współrzędnej pierwszej osi w stopniach teleskopu. Dla montażu **EQUATORIAL**: kąt godzinny (HA) w zakresie [−180°, 180°]. Dla **ALT_AZ / CASUAL**: wysokość w zakresie [0°, 90°]. |
| **Oś 2 (Dec / Azymut)** | Pole do wpisania współrzędnej drugiej osi w stopniach teleskopu. Dla montażu **EQUATORIAL**: deklinacja w zakresie [−90°, 90°]. Dla **ALT_AZ / CASUAL**: azymut w zakresie [0°, 360°). |
| **Przycisk Home 🏠** | Wysyła komendę Home z wartościami wpisanymi w pola. Po kliknięciu kontroler natychmiast aktualizuje wewnętrzną pozycję — sprawdź zakładkę **Status** aby zweryfikować. |

> **💡 Wskazówka:** Pola są wstępnie wypełnione bieżącymi współrzędnymi teleskopu z zakładki Status. Jeśli teleskop jest już fizycznie skierowany na znany obiekt, wystarczy tylko kliknąć przycisk.

#### Zachowanie zależnie od typu montażu

| Typ montażu | `axis1` (stopnie teleskopu) | `axis2` (stopnie teleskopu) | Przykład Home |
|---|---|---|---|
| **EQUATORIAL** | Kąt godzinny HA [-180°, 180°] | Deklinacja Dec [-90°, 90°] | `Home(0, 90)` — biegun NCP |
| **ALT_AZ** | Wysokość [0°, 90°] | Azymut [0°, 360°) | `Home(45, 180)` — płd, 45° |
| **CASUAL** | Wysokość w ramie montażu [0°, 90°] | Azymut w ramie montażu [0°, 360°) | wymaga bootstrapu |

#### Procedura krok po kroku

1. **Fizycznie skieruj teleskop na znany obiekt** — np. Polaris (Gwiazda Polarna) dla montażu equatorialnego, lub dowolną jasną gwiazdę o znanych współrzędnych
2. **Oblicz współrzędne teleskopu** dla tego obiektu:
   - **EQUATORIAL**: `HA = LST − RA` (w stopniach: `HA° = HA_godz × 15`), `Dec = deklinacja obiektu`
   - **ALT_AZ**: wysokość i azymut obiektu (można obliczyć w zakładce **Tests** → Reference Object → Transform)
3. **Wprowadź współrzędne** — w pola **Oś 1** i **Oś 2** sekcji Home wpisz obliczone wartości
4. **Kliknij przycisk Home 🏠** — kontroler ustawi wewnętrzny punkt referencyjny na podane współrzędne
   Alternatywnie przez API gRPC:
   ```
   grpcurl -d '{"axis1": 0, "axis2": 90}' ... astro_mount.MountControllerService/Home
   ```
5. **Zweryfikuj** — sprawdź zakładkę **Status**: pozycje teleskopu (`Telescope axis1/axis2`) powinny odpowiadać zadanym wartościom

> **⚠️ Ograniczenie CANopen absolutnego:** Po `Home()` NIE wykonuj bezpośrednio slewa pozycyjnego (`SlewToCoordinates` / `SlewToHorizontal`). Napędy CANopen używają absolutnego pozycjonowania — jeśli ich wewnętrzna pozycja bezwzględna różni się od ustawionej przez Home, slew może spowodować nieoczekiwany, gwałtowny obrót. **Najpierw wykonaj kalibrację Bootstrap** — po skalibrowaniu slewy będą bezpieczne, ponieważ kontroler przelicza współrzędne przez macierz obrotu.

> **💡 Wskazówka:** Home najlepiej wykonać od razu po fizycznym ustawieniu montażu na biegun niebieski. Dla montażu equatorialnego: skieruj teleskop na Polaris, oblicz HA (zazwyczaj bliskie 0°), Dec ≈ +89.3°, wprowadź `Oś 1 = 0`, `Oś 2 = 89.3` i kliknij Home.

---

## 3. Kalibracja Bootstrap (wstępna)

> **Wymagane:** Przed rozpoczęciem kalibracji bootstrap z enkoderami absolutnymi wykonaj operację **Home** ([sekcja 2.5](#25-ustalenie-punktu-referencyjnego-home)). Bez Home pomiary bootstrap będą wskazywały przypadkową orientację, co uniemożliwi poprawne dopasowanie.

Przejdź do zakładki **Calibration**. W górnej części znajduje się karta **"Initial Calibration (Bootstrap)"**.

### 3.1 Tryb ręczny (MANUAL)

Tryb domyślny — wymaga ręcznego wskazania każdego obiektu.

#### Krok 1: Wybierz tryb

W polu "Bootstrap Mode" wybierz **Manual** i kliknij **Set Mode**.

#### Krok 2: Znajdź obiekt referencyjny

W sekcji **"Add Reference Object"**:

1. Wpisz nazwę gwiazdy w polu wyszukiwania (np. `Vega`, `Polaris`, `Sirius`)
2. Kliknij **Search**
3. Z listy wyników kliknij **Select** przy wybranym obiekcie

Dla pierwszej kalibracji wybierz jasną gwiazdę widoczną gołym okiem.

#### Krok 3: Skieruj teleskop na obiekt

1. Kliknij **Slew & Measure** — montaż automatycznie obróci się w kierunku obiektu
2. Poczekaj aż slew się zakończy (status zmieni się na IDLE)
3. Jeśli teleskop nie wskazuje dokładnie obiektu — użyj pada kierunkowego w zakładce **Control** aby wyśrodkować obiekt

#### Krok 4: Dodaj pomiar

1. Kliknij **Add Measurement** (lub użyj **Slew & Measure** które robi to automatycznie)
2. Powtórz kroki 2–4 dla **co najmniej 3 różnych obiektów** rozłożonych po niebie

> **Wskazówka:** Wybieraj obiekty oddalone od siebie o co najmniej 60° — im bardziej rozproszone pomiary, tym dokładniejsza kalibracja.

#### Krok 5: Uruchom kalibrację

Gdy masz ≥3 pomiary, kliknij przycisk **Run Initial Calibration**.

```
✅ Bootstrap calibration completed
   Alignment error: 12.34"
   RMS residual: 8.56"
   (5 measurements)
```

### 3.2 Tryb hybrydowy (HYBRID)

Łączy podejście ręczne z automatycznym:

1. Ustaw tryb na **Hybrid** (przycisk **Set Mode**)
2. Dodaj **pierwsze 3 pomiary ręcznie** (jak w trybie MANUAL)
3. Po 3 pomiarach system automatycznie wykonuje slewy do kolejnych gwiazd z bazy
4. Nie musisz ręcznie wskazywać kolejnych obiektów — wystarczy potwierdzać pomiary

### 3.3 Tryb automatyczny (AUTOMATIC)

W pełni automatyczny — wymaga **enkoderów absolutnych**:

1. Ustaw tryb na **Automatic**
2. W sekcji "Automatic Bootstrap" ustaw parametry:
   - **Min Measurements**: 5 (zalecane)
   - **Max Error**: 30 arcsec (akceptowalny błąd)
3. Kliknij **Start Auto Bootstrap**
4. System automatycznie:
   - Wybiera gwiazdy z bazy danych
   - Wykonuje slewy
   - Dodaje pomiary
   - Uruchamia kalibrację
5. Postęp widoczny na pasku ⏳

### 3.4 Weryfikacja wyniku

Po zakończeniu kalibracji sprawdź:

| Wskaźnik | Dobry wynik | Do poprawy |
|---|---|---|
| **Alignment Error** | < 60" (1 arcmin) | > 120" |
| **RMS Residual** | < 30" | > 60" |
| **TPOINT Ready** | ✅ Yes | ❌ No (<3 pomiary) |

Jeśli wyniki są słabe:
- Dodaj więcej pomiarów (5–10 zamiast 3)
- Rozmieść pomiary równomiernie po niebie
- Sprawdź czy teleskop był dobrze wyśrodkowany na każdym obiekcie

---

## 4. Kalibracja TPOINT (precyzyjna)

TPOINT tworzy model matematyczny błędów mechanicznych montażu. Wymaga **ukończonej kalibracji bootstrap** (status "CALIBRATED").

### 4.1 Zbieranie pomiarów

Proces jest podobny do bootstrap:

1. W karcie **"Precise Pointing Model (TPOINT)"** wyszukaj obiekt
2. Kliknij **Slew & Measure** — teleskop ustawi się na obiekt
3. **Wyśrodkuj dokładnie** obiekt (użyj okularu lub kamery)
4. Kliknij **Add Measurement**
5. Powtórz dla **10–20 obiektów** rozłożonych po niebie

> **Wskazówka:** Dla najlepszych rezultatów zbierz pomiary z różnych stron nieba (wschód, zachód, zenit, nisko nad horyzontem). Unikaj obiektów poniżej 20° nad horyzontem — refrakcja atmosferyczna może zafałszować pomiary.

**Przykład dobrego zestawu pomiarów (10 gwiazd):**

| # | Gwiazda | RA | Dec | Strona nieba |
|---|---|---|---|---|
| 1 | Capella | 05h 17m | +46° 00' | Północny-wschód (wysoko) |
| 2 | Vega | 18h 37m | +38° 47' | Zenit |
| 3 | Altair | 19h 51m | +08° 52' | Południe |
| 4 | Arcturus | 14h 16m | +19° 11' | Zachód |
| 5 | Spica | 13h 25m | −11° 10' | Południowy-zachód (nisko) |
| 6 | Deneb | 20h 41m | +45° 17' | Północny-wschód |
| 7 | Regulus | 10h 08m | +11° 58' | Wschód |
| 8 | Antares | 16h 29m | −26° 26' | Południe (nisko) |
| 9 | Polaris | 02h 32m | +89° 16' | Biegun północny |
| 10 | Betelgeuse | 05h 55m | +07° 24' | Wschód (nisko) |

### 4.2 Uruchomienie kalibracji

Kliknij **Run TPOINT Calibration**. System automatycznie dobierze poziom złożoności modelu:

| Pomiarów | Model | Dokładność |
|---|---|---|
| 3–9 | Podstawowy (IH, ID, NP — 3 parametry) | ~10–60" |
| 10–13 | Minimalny (IH — 1 parametr) | ~5–30" |
| 14–19 | Rozszerzony (8 parametrów) | ~2–10" |
| 20+ | Pełny (do 40 parametrów) | <1" |

### 4.3 Interpretacja wyników

```
✅ TPOINT calibration completed
   RMS residual: 2.34"
   Max residual: 5.12"
   Chi-squared: 1.87
   Condition number: 12.3
```

| Wskaźnik | Znaczenie | Dobry | Słaby |
|---|---|---|---|
| **RMS residual** | Średni błąd dopasowania | < 3" | > 10" |
| **Max residual** | Największy błąd pojedynczego pomiaru | < 10" | > 30" (outlier) |
| **Chi-squared** | Jakość dopasowania (~1 = idealne) | 0.5–2.0 | > 5.0 |
| **Condition number** | Stabilność numeryczna | < 100 | > 1000 |

Jeśli wyniki są słabe:
- Sprawdź czy nie ma outlierów (pojedynczy pomiar z dużym błędem)
- Usuń pomiary z dużym błędem (Clear Measurements → zbierz nowe)
- Zwiększ liczbę pomiarów (minimum 20 dla pełnego modelu)

---

## 5. Przykład pełnej sesji kalibracyjnej

Poniżej kompletny scenariusz kalibracji od zera dla montażu equatorialnego z enkoderami inkrementalnymi:

### Sesja (ok. 45 minut)

```
19:00  Uruchomienie systemu
      ├── Ustaw lokalizację: lat=52.0°, lon=21.0°
      ├── Sprawdź typ enkoderów: incremental
      └── Pozycja startowa: HA=0°, Dec=0° (park)

19:02  HOME — ustaw punkt referencyjny
       ├── Fizycznie skieruj teleskop na Polaris
       ├── Oblicz HA: LST − RA(Polaris) ≈ 0°
       ├── Wprowadź w sekcji Home: Oś 1 = 0, Oś 2 = 89.3
       └── Kliknij przycisk 🏠 Home

19:05  BOOTSTRAP — tryb MANUAL
      ├── Szukaj: "Vega"      → Slew & Measure  ✅
      ├── Szukaj: "Arcturus"  → Slew & Measure  ✅
      ├── Szukaj: "Spica"     → Slew & Measure  ✅
      ├── Szukaj: "Capella"   → Slew & Measure  ✅
      ├── Szukaj: "Deneb"     → Slew & Measure  ✅
      └── Run Initial Calibration
          ✅ Alignment error: 15.2"
          ✅ RMS residual: 9.8"
          ✅ TPOINT Ready: Yes

19:20  TPOINT — zbieranie pomiarów
      ├── Capella     → Slew & Measure
      ├── Vega        → Slew & Measure
      ├── Altair      → Slew & Measure
      ├── Arcturus    → Slew & Measure
      ├── Spica       → Slew & Measure
      ├── Deneb       → Slew & Measure
      ├── Regulus     → Slew & Measure
      ├── Antares     → Slew & Measure
      ├── Polaris     → Slew & Measure
      ├── Betelgeuse  → Slew & Measure
      └── (10 pomiarów)

19:40  TPOINT — kalibracja
      └── Run TPOINT Calibration
          ✅ RMS residual: 1.87"
          ✅ Max residual: 4.23"
          ✅ Chi-squared: 1.42
          ✅ Condition: 15.6

19:45  Weryfikacja
      ├── Slew na M13 (gromada Herkulesa) — wyśrodkowany ✅
      ├── Slew na M31 (Galaktyka Andromedy) — wyśrodkowany ✅
      └── Tracking uruchomiony — obiekt stabilny w polu widzenia ✅
```

---

## 6. Rozwiązywanie problemów

### Bootstrap nie chce się uruchomić

| Problem | Rozwiązanie |
|---|---|
| "Need at least 2 measurements" | Dodaj minimum 2 pomiary (zalecane ≥3) |
| "SVD ill-conditioned" | Pomiary są zbyt blisko siebie — wybierz gwiazdy z różnych stron nieba |
| Przycisk jest szary | Sprawdź czy mount jest w stanie IDLE (nie SLEWING/TRACKING) |

### TPOINT daje słabe wyniki

| Problem | Rozwiązanie |
|---|---|
| RMS > 10" | Zwiększ liczbę pomiarów do ≥20, unikaj obiektów <20° nad horyzontem |
| Duży rozrzut | Sprawdź czy teleskop był dokładnie wyśrodkowany na każdym obiekcie |
| Chi² > 5 | Prawdopodobnie outlier — usuń pomiary i zbierz nowe |
| Condition > 1000 | Pomiary zbyt skorelowane — rozmieść je równomierniej po niebie |

### Ogólne problemy

| Problem | Rozwiązanie |
|---|---|
| "Mount controller not connected" | Sprawdź czy backend jest uruchomiony |
| Slewy nie działają | Kliknij **Clear Errors**, sprawdź stan kontrolera |
| Nie widać obiektu po slew | Użyj okularu szerokokątnego, sprawdź czy teleskop jest zgrubnie ustawiony na biegun |
| Prędkość slewa za duża/mała | Dostosuj `max_slew_rate` w Settings → Mount General |
| Kontroler się restartuje po zmianie konfiguracji | Normalne — po restarcie odczytuje pozycję z enkoderów |

---

## 7. Słownik pojęć

| Termin | Znaczenie |
|---|---|
| **Bootstrap** | Wstępna kalibracja określająca orientację montażu (problem Wahby) |
| **TPOINT** | Precyzyjny model błędów mechanicznych (dopasowanie QR) |
| **RMS residual** | Średni błąd kwadratowy dopasowania — im mniejszy, tym lepiej |
| **Alignment error** | Całkowity błąd wskazywania w sekundach kątowych |
| **Gear ratio** | Przełożenie przekładni (serwomotor : oś teleskopu), np. 360:1 |
| **Home** | Operacja ustawiająca punkt referencyjny układu współrzędnych — nie porusza fizycznie montażem, jedynie koryguje wewnętrzny stan kontrolera na podstawie znanego położenia teleskopu |
| **Enkoder absolutny** | Zna pozycję po restarcie bez kalibracji |
| **Enkoder inkrementalny** | Po restarcie zaczyna od zera — wymaga kalibracji bootstrap |
| **Slew** | Szybki obrót montażu do zadanego położenia |
| **Tracking** | Wolny obrót kompensujący ruch obrotowy Ziemi |
| **RA (Right Ascension)** | Rektascensja — odpowiednik długości geograficznej na niebie, w godzinach (0–24h) |
| **Dec (Declination)** | Deklinacja — odpowiednik szerokości geograficznej na niebie, w stopniach (−90° do +90°) |
| **HA (Hour Angle)** | Kąt godzinny — różnica między lokalnym czasem gwiazdowym a rektascensją |
| **DMS** | Format stopnie/minuty/sekundy (np. `41° 16' 12.3"`) |
| **HMS** | Format godziny/minuty/sekundy (np. `10h 30m 45.1s`) |
