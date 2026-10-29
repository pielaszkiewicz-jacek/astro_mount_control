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
    - [3.3 Tryb automatyczny (AUTO-BOOTSTRAP)](#33-tryb-automatyczny-auto-bootstrap)
      - [3.3.1 Przebieg z potwierdzaniem pomiarów](#331-przebieg-z-potwierdzaniem-pomiarów)
      - [3.3.2 Pełna automatyka z plate solverem](#332-pełna-automatyka-z-plate-solverem)
      - [3.3.3 Konfiguracja plate solvera (ASTAP)](#333-konfiguracja-plate-solvera-astap)
        - [3.3.3.1 Instalacja ASTAP](#3331-instalacja-astap)
        - [3.3.3.2 Bazy gwiazd ASTAP](#3332-bazy-gwiazd-astap)
        - [3.3.3.3 Obliczanie pola widzenia (FOV)](#3333-obliczanie-pola-widzenia-fov)
        - [3.3.3.4 Testowanie astap\_cli z linii poleceń](#3334-testowanie-astap_cli-z-linii-poleceń)
        - [3.3.3.5 Jak proxy wywołuje ASTAP](#3335-jak-proxy-wywołuje-astap)
        - [3.3.3.6 Rozwiązywanie problemów z ASTAP](#3336-rozwiązywanie-problemów-z-astap)
    - [3.4 Weryfikacja wyniku](#34-weryfikacja-wyniku)
  - [4. Kalibracja TPOINT (precyzyjna)](#4-kalibracja-tpoint-precyzyjna)
    - [4.1 Zbieranie pomiarów](#41-zbieranie-pomiarów)
    - [4.2 Uruchomienie kalibracji](#42-uruchomienie-kalibracji)
    - [4.3 Interpretacja wyników](#43-interpretacja-wyników)
  - [5. Przykład pełnej sesji kalibracyjnej](#5-przykład-pełnej-sesji-kalibracyjnej)
    - [Sesja (ok. 45 minut)](#sesja-ok-45-minut)
  - [6. Rozwiązywanie problemów](#6-rozwiązywanie-problemów)
    - [Bootstrap nie chce się uruchomić](#bootstrap-nie-chce-się-uruchomić)
    - [Auto-bootstrap / plate solver](#auto-bootstrap--plate-solver)
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

1. **Uruchom kontroler** — `./build/bin/astro_mount_controller config/mount/default.json`
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

Po uruchomieniu kontrolera enkodery absolutne zgłaszają fizyczną pozycję osi we **własnym układzie napędu** (licznik wieloobrotowy). Wartość ta jest poprawna względem punktu zerowego napędu, ale **nie jest związana z układem niebieskim** — traktowana wprost jako pozycja na niebie prowadzi do błędnego wskazywania, a w skrajnym przypadku do osiągnięcia limitów kątów i przejścia w stan ERROR.

Operacja **Home** rozwiązuje ten problem: ustawia wewnętrzny punkt referencyjny kontrolera tak, aby zgadzał się z fizycznym położeniem teleskopu. **Home nie porusza montażem** — użytkownik musi najpierw ręcznie skierować teleskop na znany obiekt.

#### Jak to działa?

Funkcja `Home(axis1, axis2)` przyjmuje współrzędne w **stopniach teleskopu** (po przełożeniu przekładni). Kontroler:

1. **Akceptuje komendę tylko w stanie IDLE lub ERROR** — w stanach SLEWING / TRACKING operacja jest odrzucana (log `home() rejected`).
2. Zeruje liczniki pozycji napędów (best-effort: MF7025v2 `SetZeroRAM`, STM42M `2031h:01h` „set origin”).
3. Odczytuje **świeżą** pozycję z enkoderów (bezpośredni odczyt SDO, nie buforowaną wartość monitora HAL) i oblicza przesunięcie referencyjne: `home_offset = pozycja_zadana − pozycja_fizyczna`.
4. Zapisuje zadaną pozycję jako bieżącą pozycję serw (osie `axis1_position_/axis2_position_` oraz `raw_servo_axis*`), zeruje prędkości osi, ponownie inicjalizuje filtr Kalmana, resetuje flagi meridian flip (`meridian_flipped=false`, `pier_side=1`, `time_to_meridian=24h`) i usuwa stan ERROR (przechodzi w IDLE).

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

> **⚠️ Ograniczenie napędów CANopen:** Dla napędów CANopen z **enkoderami inkrementalnymi** slew pozycyjny (`SlewToCoordinates` / `SlewToHorizontal`) oraz śledzenie są **odrzucane przed wykonaniem Home** — kontroler loguje `mount not homed`. Licznik napędu nie jest wtedy wyzerowany, więc komenda absolutna mogłaby wysłać oś tysiące obrotów od pozycji fizycznej. Wykonaj najpierw **Home** (zeruje liczniki i ustala `home_offset`), a dopiero potem slewy. Po kalibracji bootstrap slewy dodatkowo korzystają z macierzy obrotu.

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

### 3.3 Tryb automatyczny (AUTO-BOOTSTRAP)

Automatyczny bootstrap jest **orkiestrowany przez serwer proxy** (moduł
[`web/proxy/auto-bootstrap.js`](../../web/proxy/auto-bootstrap.js)): proxy
wykonuje slew do każdej wskazanej gwiazdy, czeka na zakończenie slewa, a
następnie — w zależności od trybu — czeka na potwierdzenie operatora albo
samodzielnie rozwiązuje pole przez plate solver. Po zebraniu pomiarów proxy
uruchamia kalibrację bootstrap i opcjonalnie TPOINT.

> **Ważne:** orkiestracja żyje w procesie Node.js (proxy). Po aktualizacji
> kodu proxy należy go zrestartować, a stronę przeładować twardo (Ctrl+F5).

W sekcji **Automatic Bootstrap**:

1. W polu **Target Stars** wpisz listę gwiazd oddzielonych przecinkami,
   np. `Capella, Vega, Altair, Spica, Arcturus, Deneb`. Gwiazdy są
   rozwiązywane po nazwie z wbudowanego katalogu jasnych gwiazd.
2. Ustaw **Min Measurements** (w interfejsie domyślnie 5, dopuszczalne minimum 3) i **Max Error (arcsec)** (w interfejsie domyślnie 30; gdy pole pozostanie puste, backend przyjmuje 60). Liczba gwiazd na liście musi być ≥ **Min Measurements** — inaczej auto-bootstrap zakończy się błędem.
3. Opcjonalnie zaznacz **Proceed to TPOINT** — po bootstrapie system zbierze
   pomiary TPOINT na tych samych gwiazdach i uruchomi kalibrację TPOINT.
4. Opcjonalnie zaznacz **Plate solve (pełna automatyka)** — zamiast
   potwierdzania pomiarów system użyje kamery i plate solvers (patrz 3.3.2).
5. Opcjonalnie zaznacz **Track after slew** — po każdym slew system od razu
   przejdzie w śledzenie celu (obiekt pozostaje wyśrodkowany podczas
   centrowania/plate solvingu).
6. Opcjonalnie zaznacz **Track after calibration** — po zakończeniu kalibracji
   system rozpocznie śledzenie ostatniego mierzonego obiektu.
7. Kliknij **Start Auto Bootstrap**. Pasek postępu pokazuje bieżący stan.

Opcje **Track after slew** i **Track after calibration** są zapisywane w
konfiguracji kontrolera (sekcja `calibration` w pliku JSON) i można je zmieniać
także w zakładce **Settings** → grupa **Calibration / Auto-Bootstrap**.

Tryb bootstrap jest ustawiany automatycznie: **HYBRID** dla enkoderów
inkrementalnych, **AUTOMATIC** dla enkoderów absolutnych lub gdy włączony jest
plate solve.

#### 3.3.1 Przebieg z potwierdzaniem pomiarów

Domyślny tryb (bez plate solvers). Dla każdej gwiazdy:

1. Proxy wysyła komendę slew (stan **Slewing...**).
2. Po zakończeniu slewa stan zmienia się na **Potwierdź Pomiar** — w sekcji
   postępu pojawia się przycisk **Potwierdź Pomiar**.
3. Wycentruj gwiazdę w okularze/kamerze (gamepad lub przyciski kierunkowe).
4. Kliknij **Potwierdź Pomiar** — system odczyta bieżącą pozycję montażu,
   doda pomiar i przejdzie do kolejnej gwiazdy.
5. Po wszystkich gwiazdach system uruchomi kalibrację bootstrap.

Przycisk **Anuluj** przerywa sesję w dowolnym momencie.

#### 3.3.2 Pełna automatyka z plate solverem

Gdy zaznaczona jest opcja **Plate solve (pełna automatyka)**, operator nie
musi potwierdzać pomiarów. Dla każdej gwiazdy po slew system:

1. Wykonuje ekspozycję kamerą (zapis FITS na dysk).
2. Rozwiązuje obraz przez plate solver.
3. Dodaje pomiar ze **współrzędnymi z rozwiązania** (observed = rozwiązane
   RA/Dec, expected = współrzędne katalogowe).

Wymaga to skonfigurowania kamery i plate solvers — patrz 3.3.3.

> **Uwaga:** wbudowana kamera jest symulowana i generuje syntetyczne obrazy
> FITS z nagłówkiem WCS zgodnym z bieżącym wskazaniem montażu. Dzięki temu
> cała pętla działa end-to-end bez zewnętrznego solwera (fallback z nagłówka
> FITS). Dla prawdziwej kamery rozwiązanie zapewnia ASTAP.

#### 3.3.3 Konfiguracja plate solvera (ASTAP)

Plate solver integruje zewnętrzny program **ASTAP** (`astap_cli`) z kamerą.
Pełna automatyka (opcja **Plate solve (pełna automatyka)**) dla każdej gwiazdy:
robi zdjęcie, rozwiązuje pole przez ASTAP i dodaje pomiar na podstawie
rozwiązanych współrzędnych — operator nie musi nic potwierdzać.

**Dlaczego ASTAP?** Wybór solwera wynika z wymagań pracy w obserwatorium:

- **Działa w pełni lokalnie (offline)** — brak zależności od internetu, w przeciwieństwie do chmurowego astrometry.net.
- **Szybki blind solve** — rozwiązuje pole w kilka–kilkanaście sekund przy poprawnym `-fov`; z podpowiedzią pozycji (`-ra`/`-spd`) jeszcze szybciej.
- **Lekka integracja** — pojedyncza binarka `astap_cli` wywoływana przez [`web/proxy/plate-solver.js`](../../web/proxy/plate-solver.js) przez `execFile`; wynik to standardowy plik `.wcs` z `CRVAL1`/`CRVAL2`.
- **Bezpieczna degradacja** — gdy ASTAP jest niedostępny, proxy odczytuje `CRVAL1`/`CRVAL2` bezpośrednio z nagłówka FITS (fallback; działa dla kamery symulowanej).
**Alternatywy wobec ASTAP:**

| Solver | Typ | Zalety | Wady |
|---|---|---|---|
| **astrometry.net** (chmura: nova.astrometry.net) | online | Bardzo dokładny, dobrze radzi sobie z blind solve | Wymaga internetu; wolny (przesyłanie pliku) |
| **astrometry.net** (lokalna instalacja / ansvr na Windows) | offline | Dokładny, open source | Duże pliki indeksów (setki GB), wolniejszy, skomplikowana instalacja |
| **PlateSolve2 / PlateSolve3** (PlaneWave) | offline | Bardzo szybki, popularny w NINA/SGP | Tylko Windows, wymaga katalogów APM (np. UCAC4) |
| **All Sky Plate Solver (ASPS)** | offline | Opakowanie astrometry.net pod Windows | Tylko Windows, wymaga indeksów |
| **PixInsight ImageSolver** | offline | Bardzo dokładny (Gaia) | Płatny program, zintegrowany w PixInsight |
| **Solver w KStars/Ekos** | offline | Wbudowany w Ekos | Wymaga lokalnego astrometry.net i indeksów |

Dla tego projektu ASTAP pozostaje rekomendowany: jest darmowy, działa offline, ma lekki tryb CLI i szybko rozwiązuje małe pola widzenia typowe dla kamer podpiętych do montażu. Gdy ASTAP nie jest dostępny, proxy i tak działa dzięki fallbackowi z nagłówka FITS.

##### 3.3.3.1 Instalacja ASTAP

ASTAP to darmowy program do astrometrii dostępny na
<https://www.hnsky.org/astap.htm>. Składa się z GUI (`astap`) oraz trybu
wiersza poleceń (`astap_cli`) — auto-bootstrap używa wyłącznie **`astap_cli`**
(GUI nie jest wymagane do działania, bywa tylko wygodne do jednorazowego
ustawienia bazy gwiazd).

**Linux (amd64) — dokładne kroki:**

1. Pobierz archiwum z wersją CLI (w środku jest pojedynczy plik `astap_cli`):
   ```bash
   curl -L -o /tmp/astap_cli.zip \
     "https://sourceforge.net/projects/astap-program/files/linux_installer/astap_command-line_version_Linux_amd64.zip/download"
   ```
2. Rozpakuj i zainstaluj do `/usr/local/bin`:
   ```bash
   cd /tmp
   unzip -o astap_cli.zip
   sudo cp astap_cli /usr/local/bin/astap_cli
   sudo chmod +x /usr/local/bin/astap_cli
   ```
3. Zweryfikuj instalację:
   ```bash
   astap_cli -help
   ```
   Poprawnie zainstalowany program wypisuje wersję (np. `ASTAP astrometric
   solver version CLI-...`) oraz listę przełączników.

**Linux (Debian/Ubuntu) — instalacja pakietu z GUI** (opcjonalnie, gdy wolisz
menedżer pakietów):
   ```bash
   curl -L -o /tmp/astap.deb \
     "https://sourceforge.net/projects/astap-program/files/linux_installer/astap_amd64.deb/download"
   sudo apt install /tmp/astap.deb
   ```

> **Uwaga — CPU/architektura:** wersja `amd64` wymaga procesora z **SSE4.1**.
> Dla starszych CPU lub ARM (np. Raspberry Pi) pobierz odpowiednią wersję z
> sekcji **Linux installer** na stronie ASTAP (`astap_i386`, `astap_aarch64`,
> `astap_armhf`).

**Windows/macOS:** na stronie ASTAP wybierz wersję command-line dla swojego
systemu, rozpakuj i podaj pełną ścieżkę do `astap_cli` w zmiennej `ASTAP_BIN`
(patrz 3.3.3.5) albo dodaj katalog z binarką do `PATH`.

##### 3.3.3.2 Bazy gwiazd ASTAP

ASTAP potrzebuje **bazy gwiazd** — bez niej dopasowanie gwiazd na zdjęciu do
katalogu nie jest możliwe. Stare bazy `H17/H18/G17/G18` są już nieaktualne —
obecnie dostępne są:

| Baza | Charakterystyka | Kiedy użyć |
|---|---|---|
| `D80` | najgłębsza (najwięcej gwiazd), ok. 1.25 GB | małe pola, FOV < 0.5° |
| `D50` | średnia | pola ~0.5–2° (typowy wybór) |
| `D20` | jasne gwiazdy | pola ~1–6° |
| `D05` | najmniejsza, tylko jasne gwiazdy | szerokie pola ≥ 0.6° |
| `G05` | Gaia, szerokie pole | FOV > 6° |
| `W08` | bardzo szerokie pole | FOV > 20° |
| `V50` / `V05` | baza fotometryczna Johnson-V | **tylko fotometria** — nie do plate solvingu |

Dla typowej kamery o krótkiej ogniskowej (FOV rzędu 1–2°) wystarczy **jedna
baza**: `D50` (ewentualnie `D20`).

**Instalacja — Debian/Ubuntu (pakiet `.deb`):**
```bash
curl -L -o /tmp/d50.deb \
  "https://sourceforge.net/projects/astap-program/files/star_databases/d50_star_database.deb/download"
sudo apt install /tmp/d50.deb
```

**Instalacja — dowolna dystrybucja (ZIP, dostępny dla D80/D50):**
```bash
curl -L -o /tmp/d50.zip \
  "https://sourceforge.net/projects/astap-program/files/star_databases/d50_star_database.zip/download"
sudo mkdir -p /opt/astap/star_database
sudo unzip -o /tmp/d50.zip -d /opt/astap/star_database
```

**Wskazanie bazy dla `astap_cli` (bez GUI):**

- przy ręcznym teście podaj ścieżkę katalogu: `astap_cli -d /opt/astap/star_database ...`
- albo wybierz bazę skrótem: `astap_cli -D d50 ...`

> **💡 Wskazówka — proxy:** moduł [`web/proxy/plate-solver.js`](../../web/proxy/plate-solver.js)
> nie przekazuje flag `-d`/`-D`, więc przy pracy automatycznej baza musi być
> znaleziona przez ustawienia ASTAP. Po instalacji pakietu `.deb` baza zwykle
> jest widoczna od razu; w razie potrzeby ustaw ścieżkę w GUI `astap`
> (Settings → Alignment → Star database location) — `astap_cli` używa tego
> samego ustawienia.

##### 3.3.3.3 Obliczanie pola widzenia (FOV)

Parametr `ASTAP_FOV_DEG` odpowiada przełącznikowi `-fov` i oznacza **średnicę
pola widzenia** (przekątną kadru) w stopniach. Można też podać `0` — ASTAP
dobierze ją automatycznie z nagłówka FITS.

Dla prostokątnej matrycy użyj przekątnej:

```
FOV_diag ≈ 57.3 × (przekątna matrycy [mm] / ogniskowa [mm])
```

Przykłady:

| Kamera (rozmiar matrycy) | Przekątna matrycy | Ogniskowa | FOV_diag ≈ |
|---|---|---|---|
| Matryca 11 × 8 mm (np. IMX533) | 13.6 mm | 400 mm | 1.95° |
| Matryca 17.6 × 13.3 mm (APS-C) | 22.1 mm | 800 mm | 1.58° |
| Matryca 22.3 × 14.9 mm (APS-C) | 26.8 mm | 250 mm | 6.1° |

- `ASTAP_FOV_DEG` podawaj jako **średnicę** (przekątną) kadru — np. dla
  matrycy 11 × 8 mm przy ogniskowej 400 mm wpisz `1.9` (albo `0` = auto).
- Dopuszczalna tolerancja to ok. 5%; wartość nie musi być idealna.
- Jeśli nie znasz dokładnych wymiarów matrycy, oszacuj FOV robiąc zdjęcie
  znanego obszaru nieba i porównując odstępy gwiazd.

##### 3.3.3.4 Testowanie astap_cli z linii poleceń

Przed uruchomieniem auto-bootstrapu przetestuj ASTAP ręcznie na zapisanym
zdjęciu FITS:

```bash
astap_cli -f zdjecie.fits -fov 1.9 -r 15 -d /opt/astap/star_database
```

albo z wyborem bazy skrótem:

```bash
astap_cli -f zdjecie.fits -fov 1.9 -r 15 -D d50
```

Opis najważniejszych przełączników (zgodny z `astap_cli -help`):

| Przełącznik | Znaczenie |
|---|---|
| `-f <plik>` | plik FITS do rozwiązania |
| `-fov <stopnie>` | **średnica** pola widzenia w stopniach (`0` = auto) |
| `-r <stopnie>` | promień obszaru poszukiwań |
| `-ra <godziny>` | podpowiedź RA (opcjonalnie; bez niej blind solve) |
| `-spd <stopnie>` | **south pole distance** = 90° − Dec (uwaga: to nie jest sama deklinacja) |
| `-d <katalog>` | ścieżka do katalogu bazy gwiazd |
| `-D <skrót>` | wybór bazy skrótem (`d80`, `d50`, `d20`, `d05`, `g05`, ...) |
| `-update` | zapisz rozwiązanie do nagłówka FITS (używane przez proxy) |

Po udanym rozwiązaniu obok zdjęcia pojawia się plik **`zdjecie.wcs`**.
Sprawdź go:

```bash
cat zdjecie.wcs | grep -E "CRVAL1|CRVAL2|SOLUTION"
```

- `CRVAL1` / `CRVAL2` — rozwiązane RA/Dec w stopniach.
- Brak pliku `.wcs` oznacza nieudane rozwiązanie (sprawdź FOV, bazę gwiazd,
  ostrość i ekspozycję zdjęcia).

##### 3.3.3.5 Jak proxy wywołuje ASTAP

Moduł [`web/proxy/plate-solver.js`](../../web/proxy/plate-solver.js) wykonuje
następujące kroki dla każdej gwiazdy:

1. Wywołuje strumieniowe `StartExposure` z `save_to_disk: true` i czeka na
   `saved_path` — ścieżkę zapisanego pliku FITS.
2. Uruchamia `astap_cli` z argumentami `-f <fits> -fov <FOV> -r <radius> -update`
   oraz opcjonalną podpowiedzią `-ra <RA_h>` i `-spd <Dec>` na podstawie
   bieżącego wskazania montażu. Uwaga: kod przekazuje Dec wprost jako `-spd`,
   choć ASTAP oczekuje south pole distance (90° − Dec) — dla typowych pól
   rozwiązanie i tak się udaje, ale to znana nieścisłość integracji. Proxy nie
   podaje flag `-d`/`-D`, więc baza musi być widoczna w ustawieniach ASTAP.
3. Odczytuje plik `<fits>.wcs` i wyciąga `CRVAL1`/`CRVAL2` (RA/Dec w stopniach),
   konwertując RA na godziny (÷ 15).
4. Zwraca rozwiązane współrzędne jako **observed** pomiaru; **expected** to
   współrzędne katalogowe gwiazdy.

**Fallback z nagłówka FITS:** gdy ASTAP nie jest zainstalowany albo nie
wyprodukuje pliku `.wcs`, solver odczyta `CRVAL1`/`CRVAL2` bezpośrednio z
nagłówka FITS. Symulowana kamera
([`camera/src/camera_service_impl.cpp`](../../camera/src/camera_service_impl.cpp))
zapisuje poprawny FITS z syntetycznym polem gwiazd i WCS zgodnym z pozycją
montażu, więc cały pipeline można przetestować **bez fizycznego sprzętu i bez
ASTAP**.

**Konfiguracja przez zmienne środowiskowe** (ustawiane przy starcie proxy):

| Zmienna | Domyślnie | Opis |
|---|---|---|
| `ASTAP_BIN` | `astap_cli` | Ścieżka do pliku wykonywalnego ASTAP |
| `ASTAP_EXPOSURE_S` | `5.0` | Czas ekspozycji kamery [s] |
| `ASTAP_FOV_DEG` | `1.0` | Średnica pola widzenia kamery [°] (przekątna kadru; `0` = auto) |
| `ASTAP_RADIUS_DEG` | `15.0` | Promień obszaru poszukiwań [°] |
| `ASTAP_TIMEOUT_MS` | `120000` | Limit czasu rozwiązania [ms] |

Przykład uruchomienia proxy z pełną konfiguracją:

```bash
cd web/proxy
ASTAP_BIN=/usr/local/bin/astap_cli \
ASTAP_FOV_DEG=1.2 \
ASTAP_EXPOSURE_S=5 \
ASTAP_RADIUS_DEG=15 \
ASTAP_TIMEOUT_MS=120000 \
node server.js
```

##### 3.3.3.6 Rozwiązywanie problemów z ASTAP

| Problem | Prawdopodobna przyczyna | Rozwiązanie |
|---|---|---|
| `ASTAP solve failed` | ASTAP nie zainstalowany lub zła ścieżka | Sprawdź `ASTAP_BIN`, uruchom `astap_cli -help` |
| Brak pliku `.wcs` po próbie | Zła baza gwiazd / zły FOV | Zainstaluj bazę `D50`/`D20` (dla małych pól `D80`), popraw `ASTAP_FOV_DEG` |
| "No star database found" | Baza nie znaleziona przez `astap_cli` | Uruchom GUI `astap` i ustaw ścieżkę bazy, albo przy teście ręcznym podaj `-d <katalog>` / `-D <skrót>` |
| Rozwiązanie trwa >120 s | Zbyt duży promień / zły FOV | Zmniejsz `ASTAP_RADIUS_DEG`, popraw FOV |
| Zła pozycja w rozwiązaniu | FOV mocno niezgodny z rzeczywistością | Oblicz FOV ze wzoru w 3.3.3.3 |
| Zdjęcie bez gwiazd | Zbyt krótka ekspozycja / nieostre | Zwiększ `ASTAP_EXPOSURE_S`, sprawdź fokus |
| "Camera exposure completed without a saved image path" | Kamera nie zapisała FITS | Użyj symulowanej kamery albo prawdziwej z zapisem na dysk |

> **💡 Wskazówka — debugowanie:** uruchom proxy z widocznym logiem
> (`node server.js` w katalogu `web/proxy`) i obserwuj komunikaty przy stanie
> **Plate solving**. W razie błędu wywołaj `astap_cli` ręcznie na tym samym
> pliku FITS — zobaczysz pełny komunikat ASTAP.

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

Kliknij **Run TPOINT Calibration**. System automatycznie dobiera poziom złożoności modelu — progresywnie rozszerza zestaw składników wraz z liczbą pomiarów, nie przekraczając składników włączonych w konfiguracji (`tpoint.enabled_terms`, domyślnie `0xFFFF` = wszystkie):

| Pomiarów | Poziom | Składniki modelu | Orientacyjna dokładność |
|---|---|---|---|
| 3–9 | Level 0 (bezpośredni QR) | IH, ID, NP — 3 parametry (do modelu instalowane są tylko IH i ID; NP celowo pomijany) | ~10–60" |
| 10–11 | Level 1 (minimalny) | `INDEX_ERROR` (IH) — 1 parametr | ~5–30" |
| 12–13 | Level 2 (średni) | `INDEX_ERROR`, `AXIS_NONPERP`, `POLAR_ALT`, `POLAR_AZ`, `TUBE_FLEXURE_HA`, `TUBE_FLEXURE_DEC` — 9 parametrów | ~2–10" |
| 14–19 | Level 3 (rozszerzony średni) | Level 2 + `COLLIMATION_ERROR` + `TUBE_ROTATION` — 11 parametrów | ~2–10" |
| 20–27 | Level 4 (standardowy) | Level 3 + `REFRACTION` — 14 parametrów | <1–5" |
| 28+ | Level 5 (pełny wg konfiguracji) | Wszystkie włączone składniki, w tym `WORM_ERROR`, `ENCODER_ERROR_HA`, `ENCODER_ERROR_DEC` — do ~43 parametrów | <1" |

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
- Zwiększ liczbę pomiarów: **≥20** dla pełnego zestawu standardowych składników (Level 4), **≥28** dla wszystkich składników z konfiguracji (Level 5)

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
| "Need at least 2 measurements" | Komunikat statusu bywa mylący — kalibracja bootstrap faktycznie wymaga **co najmniej 3 pomiarów** (przy 2 zwraca błąd). Dodaj ≥3 pomiary gwiazd rozłożonych po niebie. |
| "SVD ill-conditioned" | Pomiary są zbyt blisko siebie — wybierz gwiazdy z różnych stron nieba |
| Przycisk jest szary | Sprawdź czy mount jest w stanie IDLE (nie SLEWING/TRACKING) |

### Auto-bootstrap / plate solver

| Problem | Rozwiązanie |
|---|---|
| "Nie znaleziono gwiazdy: X" | Podaj nazwę jasnej gwiazdy z wbudowanego katalogu (np. `Capella`, `Vega`, `Altair`, `Spica`, `Arcturus`, `Deneb`). Nazwa jest porównywana bez uwzględniania wielkości liter. |
| "Wymagane co najmniej N gwiazd, podano M" | Zwiększ **Min Measurements** listę gwiazd — podaj co najmniej tyle gwiazd, ile wynosi `min_measurements` (domyślnie 5). |
| "Bootstrap mode must be HYBRID or AUTOMATIC" | Przestarzały błąd — nowsza wersja ustawia tryb automatycznie. Zaktualizuj frontend i proxy. |
| "Mount entered ERROR state while slewing" | Kliknij **Clear Errors** i sprawdź soft-limity / zasilanie napędów. |
| "Camera exposure completed without a saved image path" | Kamera nie zapisała FITS — przebuduj/uruchom nowszą wersję kontrolera (symulowana kamera zapisuje FITS), albo podepnij prawdziwą kamerę z zapisem na dysk. |
| "ASTAP solve failed" | Sprawdź `ASTAP_BIN`, instalację ASTAP i bazę gwiazd. Bez ASTAP działa fallback z nagłówka FITS (tylko symulowana kamera). |
| Silniki nie ruszają po starcie auto-bootstrap | Zrestartuj proxy Node.js — orkiestracja mieszka w proxy; po zmianie kodu stary proces nie wykonuje slewów. |

### TPOINT daje słabe wyniki

| Problem | Rozwiązanie |
|---|---|
| RMS > 10" | Zwiększ liczbę pomiarów (≥20 dla Level 4, ≥28 dla pełnych składników), unikaj obiektów <20° nad horyzontem |
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
