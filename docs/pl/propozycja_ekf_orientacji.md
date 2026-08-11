# Propozycja: ciągła aktualizacja macierzy obrotu (orientacji montażu) przez filtr Kalmana (EKF)

> **Status:** propozycja projektowa — bez zmian w kodzie.
> Zakres: podpięcie istniejącego filtra `models::KalmanFilter` (EKF) do działającego
> kontrolera w celu ciągłej aktualizacji macierzy obrotu (kwaternionu orientacji
> montażu) i uzyskanie dzięki temu wymiernych korzyści. Wdrożenie może nastąpić
> w późniejszym etapie.

## Spis treści

1. [Stan obecny](#1-stan-obecny)
2. [Jak mogłoby to wyglądać](#2-jak-mogłoby-to-wyglądać)
3. [Korzyści](#3-korzyści)
4. [Ryzyka i uwagi projektowe](#4-ryzyka-i-uwagi-projektowe)
5. [Opcjonalność i koszt wdrożenia (tryb EKF vs obecny)](#5-opcjonalność-i-koszt-wdrożenia-tryb-ekf-vs-obecny)
6. [Zmiany w Web UI i perspektywa użytkownika końcowego](#6-zmiany-w-web-ui-i-perspektywa-użytkownika-końcowego)
7. [Podsumowanie](#7-podsumowanie)

---

## 1. Stan obecny

- W aktualnie działającym kontrolerze **macierz obrotu (kwaternion orientacji
  montażu) NIE jest aktualizowana przez żaden filtr Kalmana**.
- Jedyny filtr uruchamiany w kontrolerze to lekki `PositionKalmanFilter`
  ([`mount_controller.cpp:42`](../../src/controllers/mount_controller.cpp:42))
  — stan `[pos1, pos2, rate1, rate2]`, aktualizowany w każdej iteracji pętli
  śledzenia ([`mount_controller.cpp:1815`](../../src/controllers/mount_controller.cpp:1815)).
  Wygładza **pozycje osi**, nie macierz obrotu.
- Filtr EKF ze stanem zawierającym kwaternion orientacji
  (`models::KalmanFilter` w [`kalman_filter.cpp`](../../src/models/kalman_filter.cpp:15))
  istnieje jako **przetestowana biblioteka**, ale nie jest instancjonowany w kodzie
  produkcyjnym (używany tylko w [`test_kalman_filter.cpp`](../../tests/test_kalman_filter.cpp)).
- Macierz obrotu (`mount_orientation_`) jest obecnie aktualizowana wyłącznie przez:
  - kalibrację bootstrap (Wahba/SVD) — [`mount_controller.cpp:3787`](../../src/controllers/mount_controller.cpp:3787),
  - jawne `setMountOrientation()` — [`mount_controller.cpp:5066`](../../src/controllers/mount_controller.cpp:5066),
  - wczytywanie stanu/konfiguracji (`loadState`, [`mount_controller.cpp:4860`](../../src/controllers/mount_controller.cpp:4860)).

---

## 2. Jak mogłoby to wyglądać

### 2.1 Integracja

W `MountController::Impl` obok istniejącego `position_kf_` (filtr pozycji)
pojawiłby się nowy członek:

```cpp
std::unique_ptr<models::KalmanFilter> orientation_ekf_;
```

Klasa `models::KalmanFilter` **już istnieje i jest przetestowana**
([`kalman_filter.h`](../../include/models/kalman_filter.h:25),
[`kalman_filter.cpp`](../../src/models/kalman_filter.cpp:15)):

- stan: `[kwaternion orientacji (4) + parametry TPoint (N) + prędkości (2) + środowisko (3)]`,
- metody `predict(dt)` i `update(measurement, H, R)`,
- renormalizacja kwaternionu ([`kalman_filter.cpp:123`](../../src/models/kalman_filter.cpp:123)),
- adaptacyjny szum Q/R,
- test spójności `getConsistencyTest()`,
- `saveState/loadState` ([`kalman_filter.cpp:1002`](../../src/models/kalman_filter.cpp:1002)).

Trzeba by ją tylko **podpiąć do strumienia danych kontrolera**.

### 2.2 Źródła pomiarów (wejścia EKF)

- **Pomiary gwiazd** z istniejących pipeline'ów:
  - `addBootstrapMeasurement(...)` — [`mount_controller.cpp:3579`](../../src/controllers/mount_controller.cpp:3579),
  - pomiary TPoint,
  jako innowacja (residuum: obserwowane vs oczekiwane RA/Dec w układzie horyzontalnym).
- **Pozycje/prędkości osi z HAL** w pętli śledzenia — pomiary ciągłe
  (dokładnie tam, gdzie dziś działa `PositionKalmanFilter`).
- Opcjonalnie: czujnik inercyjny (gyro/akcelerometr), jeśli występuje w sprzęcie.

### 2.3 Cykl działania

1. **Inicjalizacja** — `orientation_ekf_` startuje z bieżącego `mount_orientation_`
   i niepewności z konfiguracji (`initial_orientation_uncertainty`);
   re-inicjalizacja po `Home`, po kalibracji bootstrap, po `setMountOrientation()`.
2. **W każdej iteracji pętli śledzenia/slewu** — `predict(dt)`
   (kinematyczny ruch kwaternionu `dq/dt = 0.5·ω⊗q`) + `update(...)` z pomiarami pozycji.
3. **Przy pomiarach gwiazd** — `update(...)` z większą wagą (mniejszy szum pomiaru R)
   — mocna korekta orientacji.
4. **Zapis zwrotny** — po każdym update (lub dopiero po wykryciu zbieżności /
   przekroczeniu progu zmiany) skopiowanie kwaternionu ze stanu EKF do
   `mount_orientation_` (ścieżką `setMountOrientation` /
   [`mount_controller.cpp:3787`](../../src/controllers/mount_controller.cpp:3787)),
   aby wszystkie transformacje CASUAL używały udoskonalonej macierzy.
5. **Ochrona przed rozbieżnością** — bramkowanie innowacji (test Mahalanobisa),
   renormalizacja kwaternionu, test spójności `getConsistencyTest()`;
   w razie wykrycia rozbieżności — fallback do kalibracji bootstrap (Wahba/SVD).

### 2.4 Punkt zapisu stanu

EKF ma już `saveState/loadState` — jego stan (kwaternion + kowariancja) można by
włączyć do proponowanego wcześniej mechanizmu punktów kontrolnych (checkpoint,
patrz [`propozycja_odzyskiwania_po_awarii.md`](propozycja_odzyskiwania_po_awarii.md)),
co zapewnia ciągłość orientacji po restarcie/awarii.

---

## 3. Korzyści

1. **Ciągła samokorekta bez przerywania pracy** — orientacja poprawia się w trakcie
   normalnego śledzenia/slewu; nie trzeba zatrzymywać montażu i ponownie kalibrować
   po każdej sesji.
2. **Wyższa dokładność wskazywania i śledzenia** — filtr optymalnie łączy wszystkie
   pomiary, tłumi szum enkoderów i zmiany od flexury; możliwa poprawa do poziomu
   podłukowego (sub-arcsecond).
3. **Jedno spójny estymator** — orientacja, parametry TPoint, prędkości i środowisko
   estymowane łącznie, z wykorzystaniem korelacji między nimi (np. offset enkodera
   „sprzęga się” z orientacją) — zamiast osobnych, jednorazowych rozwiązań.
4. **Obsługa montaży o dowolnej orientacji (CASUAL)** — losowo ustawiony montaż może
   być **uczony i doszlifowywany online**, a nie tylko wyznaczony jednorazowo metodą
   Wahba/SVD.
5. **Kom pensacja dryfu** — zmiany termiczne / flexura w trakcie długiej sesji są
   śledzone w czasie, zamiast pozostawać stałym błędem.
6. **Mniej ręcznych kalibracji** — EKF doszlifowuje między rzadkimi kalibracjami
   bootstrap/TPoint; wystarczy mniej gwiazd na start.
7. **Odporność na szum i braki pomiarów** — adaptacyjne Q/R i bramkowanie innowacji
   (już zaimplementowane) sprawiają, że pojedynczy zły pomiar nie psuje orientacji.
8. **Ciągłość po awarii/restarcie** — stan EKF może być zapisywany w punkcie
   kontrolnym, więc po niekontrolowanej awarii orientacja jest przywracana,
   a nie wyznaczana od zera.
9. **Metrki jakości dla UI** — kowariancja i test spójności pozwalają pokazać
   operatorowi „jakość wyrównania” na żywo i ostrzegać, gdy rozbieżność rośnie.

---

## 4. Ryzyka i uwagi projektowe

- **Obserwowalność** — orientacja jest obserwowalna tylko, gdy pomiary obejmują
  różne obszary nieba; pomiary z jednego rejonu nie pozwolą na pełne 3-DOF
  wyrównanie (analogiczny problem jak w Wahba przy współliniowych wektorach,
  [`mount_controller.cpp:3698`](../../src/controllers/mount_controller.cpp:3698)).
- **Gauge kwaternionu** — konieczna renormalizacja i ostrożność przy przejściu
  przez kwaternion jednostkowy (już obsłużone w `predict`).
- **Koszt obliczeniowy** — operacje na macierzach (LDLT w Eigen) na iterację;
  przy docelowym cyklu ~20–50 Hz trzeba ograniczyć rozmiar stanu / rzadkość update'ów.
- **Zakres stosowalności** — największy sens dla CASUAL (swobodna orientacja);
  dla EQUATORIAL orientacja jest z definicji stała, a EKF użyteczny głównie do
  estymacji offsetu enkodera / parametrów TPoint.

---

## 5. Opcjonalność i koszt wdrożenia (tryb EKF vs obecny)

### 5.1 Założenie — dwa tryby pracy

System może działać w dwóch trybach, przełączanych **flagą konfiguracyjną**
(`orientation_ekf_enabled`, domyślnie **false**):

- **Tryb obecny (domyślny)** — orientacja wyznaczana jak dziś: kalibracja bootstrap
  (Wahba/SVD, [`mount_controller.cpp:3787`](../../src/controllers/mount_controller.cpp:3787)),
  `setMountOrientation()`, wczytywanie stanu/konfiguracji.
- **Tryb EKF** — ciągła estymacja orientacji przez `models::KalmanFilter`
  (predict/update w pętli śledzenia, zapis zwrotny kwaternionu do `mount_orientation_`).

Wybór trybu można zrealizować jako **flagę runtime** (config + gRPC + UI) lub
**flagę compile-time** (opcja CMake `ENABLE_ORIENTATION_EKF`).

### 5.2 Ocena kosztu

**Umiarkowanie kosztowna i niskiego ryzyka implementacja — rzędu 6–12 dni
roboczych** (≈1,5–3 tygodni) dla solidnej, przetestowanej wersji. Główna zaleta:
oba tryby to w dużej mierze **równoległe, niezależne ścieżki kodu**, więc EKF można
dodać jako cechę *add-on* za flagą, bez ruszania obecnej ścieżki (domyślny tryb =
zachowanie obecne, ryzyko regresji ~zero).

#### Dlaczego to nie jest drogie
- Filtr `models::KalmanFilter` **już istnieje i jest przetestowany**
  ([`kalman_filter.cpp`](../../src/models/kalman_filter.cpp:15),
  [`test_kalman_filter.cpp`](../../tests/test_kalman_filter.cpp)) — nie trzeba go pisać od zera.
- Obecna ścieżka (Wahba/SVD) pozostaje nietknięta przy wyłączonym EKF.
- Podział na tryby to tylko gałąź `if` na flagę — koszt pomijalny.

#### Rozbicie kosztów

| Zakres | Koszt (dni) | Uwagi |
|---|---|---|
| Flaga konfiguracyjna + plumbing (struktury, JSON, gRPC get/set, UI) | 0,5 | np. `orientation_ekf_enabled` |
| Członek EKF w `Impl` + cykl życia (init, re-init po Home/bootstrap/setOrientation, reset przy zmianie trybu) | 0,5–1 | `std::unique_ptr<models::KalmanFilter> orientation_ekf_` obok `position_kf_` |
| **Podpięcie pomiarów** (bootstrap/TPoint/pozycje z HAL → `update(...)`) | **1–2** | to najtrudniejsza część: wektory pomiaru, macierze H, R, indeksowanie stanu EKF |
| Zapis zwrotny kwaternionu → `mount_orientation_` + bramkowanie zbieżności (deadband) | 0,5–1 | ścieżka `setMountOrientation` |
| Zabezpieczenia (bramkowanie innowacji, fallback do Wahba/SVD, test spójności) | 0,5–1 | częściowo gotowe w EKF |
| Przełącznik runtime vs compile-time (CMake) | 0,5–1 | runtime wygodniejsze (+0,5–1 dnia), compile-time tańsze w utrzymaniu |
| Testy (jednostkowe przełącznika trybów + integracyjne) | 1–2 | |
| Walidacja/dostrojenie na symulacji i sprzęcie | 1–3 | dobór R dla gwiazd vs pozycji, test obserwowalności |
| **Suma** | **~6–12 dni** | |

### 5.3 Co podbija koszt (i jak tego uniknąć)
- **Najdroższy element to poprawne zbudowanie wektorów pomiaru i macierzy H/R**
  do istniejącego układu stanu EKF (kwaternion + TPoint + prędkości + środowisko)
  oraz zapewnienie obserwowalności. To nie kwestia „przełącznika”, lecz poprawnej
  estymacji — stąd 1–2 dni i dodatkowy czas na dostrojenie.
- **Przełącznik runtime** wymaga utrzymywania i re-walidacji obu ścieżek po każdej
  zmianie; jeśli priorytetem jest niski koszt, można zacząć od **flagi compile-time**
  (CMake), a runtime dodać później.
- Należy zdefiniować zachowanie stanu EKF przy przełączaniu trybów (najprościej:
  reset EKF przy włączeniu, start z bieżącego `mount_orientation_`).

### 5.4 Co jest wspólne i się nie zmienia
- `PositionKalmanFilter` (wygładzanie pozycji) działa w obu trybach — jest
  ortogonalny do orientacji.
- Zbiory pomiarów (bootstrap/TPoint) są te same; EKF tylko dodatkowo je konsumuje
  i utrzymuje kowariancję.

### 5.5 Rekomendacja
Zacznij od **flagi runtime `orientation_ekf_enabled` (domyślnie false)** —
zachowanie obecne jako domyślne, EKF jako opcja. Koszt ~6–12 dni, ryzyko niskie
(ścieżka add-on), a korzyści (ciągła samokorekta orientacji, mniej ręcznych
kalibracji, lepsza dokładność) uzyskuje się bez zmiany domyślnego trybu działania.

---

## 6. Zmiany w Web UI i perspektywa użytkownika końcowego

### 6.1 Zmiany w Web UI

#### Ustawienia (Settings) — przełącznik trybu EKF
W panelu ustawień istnieje już grupa „Kalman Filter”
([`settings.js`](../../web/public/js/components/settings.js:320)) z polami
`process_noise`, `measurement_noise`, `kalman_adaptive_q`, `kalman_adaptive_r`,
`kalman_innovation_threshold`. Do tej grupy dodałoby się nowe pole typu checkbox:

- `orientation_ekf_enabled` → **„Ciągłe dopasowanie orientacji (EKF)”**
  (domyślnie wyłączone).

Parametry Q/R są już wystawione w UI, więc nie trzeba nowych kontrolek; do tego
mechanizm i18n ([`settings.js`](../../web/public/js/components/settings.js:939))
otrzyma opisy nowego pola.

#### Kalibracja (Calibration) — przepływ bootstrap
Obecnie panel pokazuje status bootstrap: licznik pomiarów, błąd wyrównania
(`bootstrap-alignment-error`), `residual_rms`, gotowość do TPoint
([`calibration.js`](../../web/public/js/components/calibration.js:542)).
Przy włączonym EKF:

- przycisk „Run Bootstrap” nadal służy do wstępnego wyrównania (Wahba/SVD) —
  wynik **zasila EKF jako stan początkowy**,
- pojawi się wskaźnik **„Quality of orientation (live)”** — jak EKF na bieżąco
  poprawia orientację (błąd maleje w czasie, gdy napływają pomiary gwiazd),
- przycisk „Clear Bootstrap” będzie też resetował stan EKF.

#### Karta statusu montażu (Mount Status)
W karcie statusu ([`mountStatus.js`](../../web/public/js/components/mountStatus.js:506))
pojawi się nowy wiersz:

- **„Orientacja: EKF aktywny · jakość 98%”** (badge: zielony = zbieżna,
  żółty = słaba obserwowalność, czerwony = rozbieżność/fallback),
- przy wyłączonym EKF: **„Orientacja: kalibrowana (bootstrap)”** — tryb jak obecnie.

Dane pochodziłyby z nowego endpointu statusu (kowariancja / `getConsistencyTest()`
z EKF), mapowanego przez proxy i `api.js`.

#### Powiadomienia (toasty/banery)
- Po włączeniu EKF: toast „Włączono ciągłe dopasowanie orientacji. Wykonaj wstępną
  kalibrację bootstrap.”.
- Przy rozbieżności EKF / braku obserwowalności: „Orientacja EKF: zbyt mało pomiarów
  / brak pokrycia nieba — wrócono do trybu bootstrap.”.
- Po zbieżności do dobrej jakości: „Orientacja wyrównana (jakość ≥ X%).”.

### 6.2 Perspektywa użytkownika końcowego

**Scenariusz 1 — użytkownik nie zmienia nic (domyślnie).**
Nic się nie zmienia: UI wygląda i działa identycznie jak dziś. Flaga domyślnie
wyłączona → zero ryzyka regresji dla obecnych użytkowników.

**Scenariusz 2 — zaawansowany użytkownik włącza EKF (montaż CASUAL / swobodna
orientacja).**
1. W **Ustawieniach** zaznacza „Ciągłe dopasowanie orientacji (EKF)” i zapisuje.
2. Wykonuje **pierwszą kalibrację bootstrap** (jak dotychczas — 3+ gwiazdy);
   UI pokazuje zwykły wynik wyrównania.
3. Od tej pory podczas normalnego śledzenia karta **Mount Status** pokazuje
   „Orientacja: EKF aktywny · jakość …” — użytkownik **widzi na żywo, jak jakość
   rośnie** z każdą kolejną obserwacją.
4. Gdy zmieni ustawienie/położenie montażu, może **wyczyścić kalibrację** — EKF
   zostanie zresetowany, a UI o tym poinformuje.
5. Gdy EKF ma za mało danych (np. pomiary tylko z jednej strony nieba), UI
   **ostrzeże zamiast milczeć**, a w razie potrzeby wróci do trybu bootstrap
   (z komunikatem).

**Scenariusz 3 — operator obserwatorium.**
- Widzi w statusie **jakość orientacji na żywo** (zamiast „czarnej skrzynki”) —
  może wcześnie wychwycić dryf termiczny/flexurę.
- Nie musi **przerywać sesji** na ponowną kalibrację — EKF poprawia orientację
  w tle.
- Po awarii/restarcie (w połączeniu z mechanizmem checkpointów) UI pokazuje,
  że orientacja została **przywrócona ze stanu EKF**, a nie wyznaczona od zera.

**Co użytkownik NIE widzi (i nie musi rozumieć).**
Szczegóły macierzy H/R, kowariancji, kwaternionu — zostają po stronie backendu.
UI pokazuje wyłącznie **przełącznik + czytelny wskaźnik jakości + ostrzeżenia**,
spójne z resztą panelu.

### 6.3 Podsumowanie zmian w UI

1. Nowy checkbox **„Ciągłe dopasowanie orientacji (EKF)”** w Ustawieniach
   → grupa „Kalman Filter”.
2. Nowy wskaźnik **„Orientacja + jakość EKF”** w karcie Mount Status (badge
   z kolorem).
3. Rozszerzony panel Kalibracji: wskaźnik „live quality” i reset EKF przy
   czyszczeniu bootstrap.
4. Toasty/banery: włączenie, brak obserwowalności, rozbieżność/fallback,
   przywrócenie po restarcie.
5. (Opcjonalnie) linia w logach/podglądzie diagnostycznym z wartościami jakości EKF.

Realizacja to głównie: nowe pole w konfiguracji (mapowane przez `config.js`/proxy),
nowy endpoint statusu EKF w gRPC + proxy oraz rozszerzenie komponentów
[`settings.js`](../../web/public/js/components/settings.js),
[`mountStatus.js`](../../web/public/js/components/mountStatus.js) i
[`calibration.js`](../../web/public/js/components/calibration.js) — bez przebudowy
całego UI.

---

## 7. Podsumowanie

Podpięcie istniejącego EKF do kontrolera zamieniłoby jednorazową kalibrację
bootstrap (Wahba/SVD) w **ciągły, samo-poprawiający się estymator orientacji**
działający w tle normalnej pracy. Korzyści: lepsza dokładność
wskazywania/śledzenia, mniej ręcznych kalibracji, kompensacja dryfu i flexury,
spójna estymacja orientacji+TPoint oraz możliwość wznowienia orientacji po awarii
dzięki zapisywaniu stanu filtra. Główne wyzwania to zapewnienie obserwowalności
(pokrycie nieba pomiarami) oraz ochrona przed rozbieżnością (bramkowanie innowacji
+ fallback do Wahba/SVD).
