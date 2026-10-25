# Wyznaczanie prędkości śledzenia — opis i analiza unifikacji

> **Zakres:** jak kontroler wyznacza prędkości obu osi podczas śledzenia dla
> `EQUATORIAL`, `ALT_AZ` i `CASUAL`, oraz czy da się to zrobić jednym,
> jednolitym mechanizmem opartym o pozycję bieżącą, pozycję docelową i macierz
> transformacji.
>
> Powiązane dokumenty: [`analiza_typow_montazu.md`](analiza_typow_montazu.md),
> [`propozycja_podzialu_watku_sledzenia.md`](propozycja_podzialu_watku_sledzenia.md).

## 1. Konwencja osi (kanoniczna)

- **EQUATORIAL:** oś1 = HA (kąt godzinny), oś2 = Dec.
- **ALT_AZ:** oś1 = wysokość, oś2 = azymut.
- **CASUAL:** oś1 = wysokość‑podobna, oś2 = azymut‑podobna (rama montażu).

Wszystkie prędkości w kodzie są w **stopniach serwo/s** (telescope °/s × `gear_ratio`).

## 2. Wspólne ramy pętli śledzenia

- Prędkości trzymane w parze `axis1_rate_` / `axis2_rate_` (chronione `rate_mutex_`),
  kopiowane do `current_rate_1/2` na początku iteracji.
- Po wyznaczeniu prędkości są:
  - skalowane przez `rate_factor` (strefa hamowania miękkich limitów),
  - integrowane wewnętrznie (`axis_n += rate·dt`) dla symulacji/KF,
  - podawane do filtra Kalmana (`setRates`).
- Wysyłka do napędów:
  - ALT_AZ / CASUAL oraz EQUATORIAL‑velocity → `setVelocity` (przez
    `publishVelocityCommand` w wariancie split),
  - EQUATORIAL‑position → `setPosition` celów (napęd generuje ruch własnym
    generatorem trajektorii).

### 2.1 Selektor trybu śledzenia (`tracking_mode`)

Od zmian 2026-10-11 dostępny jest selektor `mount.tracking_mode`:

| Wartość | Zachowanie |
|---------|------------|
| `auto` (domyślnie) | EQUATORIAL → tryb pozycyjny; ALT_AZ / CASUAL → tryb prędkościowy |
| `position` | tryb pozycyjny (`setPosition`) dla wszystkich typów montażu |
| `velocity` | tryb prędkościowy (`setVelocity`) dla wszystkich typów montażu |

Tryb pozycyjny dla ALT_AZ / CASUAL:
- cel obu osi jest przesuwany co interwał aktualizacji o
  `snap_rate_i × (Δt + tracking_pos_lead_seconds)`,
- prędkość profilowa `pos_vel_axis_i` = `|snap_rate_i| × tracking_pos_velocity_margin`
  (przycięta do `max_tracking_rate`),
- oś azymutalna (axis2) jest przewijana do okna wieloobrotowego napędu
  (najkrótsza droga względem pozycji fizycznej).

Prędkości `snap_rate_1/2` dla ALT_AZ / CASUAL są pozycyjnie zależne i liczone
przez `MountKinematics::trackingRates()` (sekcja 3.3).

## 3. Prędkości per typ montażu

### 3.1 EQUATORIAL — prędkości stałe

Ustawiane raz w `startTracking()` (niezależne od pozycji):

| Tryb | oś1 (HA) | oś2 (Dec) |
|------|----------|-----------|
| SIDEREAL | `0.004178 · gear_ha` | `0` |
| SOLAR | `0.004167 · gear_ha` | `0` |
| LUNAR | `0.004079 · gear_ha` | `0` |
| CUSTOM | `customRA_arcsec_s / 3600 · gear_ha` | `customDec_arcsec_s / 3600 · gear_dec` |
| OFF | `0` | `0` |

Podtryby:

- **Position mode (domyślny, `equatorial_tracking_velocity_mode=false`)** — host
  co ~50 iteracji przelicza `HA = LST − RA (+lead)` i wysyła `setPosition`;
  faktyczny ruch generuje napęd. Prędkość hosta nie jest wysyłana co iterację.
- **Velocity mode (`=true`)** — `cmd_rate = snapshot stałej prędkości + drift_trim`,
  gdzie `drift_trim = clamp(0.5·(expected − actual), ±1 °/s)` liczony raz na
  sekundę; publikacja przez `publishVelocityCommand`.

Prędkości nie zależą od bieżącej pozycji — śledzenie sideralne to stała
prędkość w HA i zero w Dec.

> **Założenie:** ta stała prędkość jest poprawna tylko dla **idealnie
> skalibrowanego i wyrównanego** montażu równikowego. W realnym sprzęcie nie
> zachodzi — patrz §4.4.

### 3.2 ALT_AZ — prędkości zależne od pozycji (`Q = I`)

Liczone co iterację w `computeMountFrameRates()` z kwaternionem tożsamościowym:

1. `alt = axis1_position/gear_ha`, `az = axis2_position/gear_dec` (== prawdziwe alt/az).
2. `ω = 7.292115e-5 · trackingModeFactor(mode)`
   (SIDEREAL 1.0, SOLAR 0.9972, LUNAR 0.9760, CUSTOM 1.0, OFF 0).
3. Prędkości w prawdziwym układzie horyzontalnym:
   - `alt_rate = ω · cos(φ) · cos(az)`
   - `az_rate  = −ω · cos(φ) · sin(alt) / cos(alt)` (`cos(alt)` przycięte do `cos(89.5°)`).
4. Budowany wektor prędkości ENU, rzutowany na osie (dla `Q=I`):
   - `axis1_rate = alt_rate · gear_ha`
   - `axis2_rate = az_rate · gear_dec`

### 3.3 CASUAL — te same wzory, obrócone o `Q`

Ta sama `computeMountFrameRates()`, ale:

1. `mount_alt/mount_az → (alt, az)` przez `mountOrientationToHorizontal(..., Q)`.
2. Kroki astronomiczne jak w ALT_AZ (te same wzory w prawdziwym układzie horyzontalnym).
3. Wektor prędkości obracany przez `Q` do ramy montażu i rzutowany na jego osie.
4. Skalowanie przez `gear_ratio`.

CASUAL = ALT_AZ złożony z rotacją `Q` (ALT_AZ to `Q = I`).

### 3.4 Post‑przetwarzanie (ALT_AZ i CASUAL)

- **Wygładzanie:** `smoothed = last + 0.3·(rate − last)`.
- **Próg zmiany:** wysyłka tylko gdy `|Δ| > 1e-6 °/s`.
- **Wysyłka:** `publishVelocityCommand` → wątek io reemituje ostatnią prędkość
  co tick (RPDO2, `io_update_hz`).
- **Guider:** korekty ST4 dodawane jako offsety pozycji, nie prędkości.

## 4. Pytanie projektowe: czy prędkość nie powinna być liczona jednolicie?

**Teza:** dla każdego typu prędkość powinna być wyznaczana tak samo — na
podstawie **pozycji bieżącej i docelowej**, gdzie pozycja docelowa jest
wyznaczana macierzą transformacji (nieba → osie montażu).

### 4.1 To jest matematycznie poprawne i wykonalne

Niech `f(RA, Dec, t)` = transformacja niebo → osie montażu (dla montażu
losowo zorientowanego to `f = R_mount⁻¹ · v_sky`, dla EQ/ALT_AZ — szczególne
przypadki tej samej rodziny). Wtedy wymagana prędkość osi to pochodna po czasie:

```
rate_axes(t) = d/dt [ f(RA(t), Dec(t), t) ]
             = J(current_position) · ω_celestial        (postać analityczna, jakobian)
             ≈ ( f(t+dt) − f(t) ) / dt                  (postać różnicowa)
```

- **EQUATORIAL** → `f = (LST(t) − RA, Dec)`, więc `dHA/dt = const`, `dDec/dt = 0`
  → jakobian **stały**. „Stała prędkość EQ” to po prostu szczególny przypadek
  stałego jakobianu, a nie odrębne zjawisko.
- **ALT_AZ / CASUAL** → `f` nieliniowe, jakobian zależy od pozycji
  (`cos(alt)`, `cos(az)`), stąd prędkości zależne od pozycji.

Zatem jedno ujęcie `trackingRates(current, target, dt)` (albo jawnie jakobian
`J(current)·ω`) pokrywa **wszystkie** typy — obecny kod już to robi dla
ALT_AZ/CASUAL (`computeMountFrameRates`), a EQ pozostaje osobną, zamkniętą formułą.

### 4.2 Warunki, które trzeba uwzględnić w jednolitej implementacji

1. **Zawijanie osi okresowych (najkrótsza ścieżka).** Pozycja docelowa może być
   wybrana jako ±k·360° od bieżącej (żeby napęd nie odkręcał pełnych obrotów).
   Prędkość musi wynikać z **delty po najkrótszej ścieżce** (`Δ mod 360`), a nie
   z surowej różnicy `target − current` — inaczej zawinięcie daje skokową,
   fałszywą prędkość. Dotyczy to osi azymutopodobnej (ALT_AZ oś2, CASUAL oś2)
   oraz HA w EQ.
2. **Korekcje astronomiczne.** Refrakcja, nutacja i TPOINT zmieniają pozycję
   *aparentną* celu (a więc i efektywne prędkości). W podejściu „cel z macierzy”
   trzeba albo włączyć te korekcje do `f`, albo świadomie zaakceptować mały
   błąd prędkości (feed‑forward), korygowany później sprzężeniem zwrotnym.
3. **Singularność zenitalna (ALT_AZ).** `az_rate ∝ 1/cos(alt)` — wymaga
   przycięcia `cos(alt)` (już jest: `cos(89.5°)`).
4. **Tryb sterowania ≠ typ montażu.** „Prędkość” ma sens tylko w trybie
   prędkościowym. W trybie pozycyjnym (domyślny dla EQ) host wysyła cele, a ruch
   generuje napęd — nie ma tam prędkości hosta do unifikacji. Ewentualna
   unifikacja powinna dotyczyć wspólnego **feed‑forward rate** używanego w trybie
   prędkościowym (ALT_AZ/CASUAL, EQ‑velocity) oraz wewnętrznej integracji/KF.
5. **Analitycznie vs numerycznie.** Różnica skończona `(f(t+dt) − f(t))/dt` jest
   prosta i jednolita, ale podatna na szum (dt, kwantyzacja, fp). Postać
   jakobianowa (`J·ω`) jest stabilniejsza i tańsza; sugerowana jako docelowa.
6. **Dryf.** Czysty feed‑forward (jakobian) nie koryguje dryfu napędu — dlatego
   EQ‑velocity ma dodany `drift_trim`; dla pełnej unifikacji warto mieć wspólną
   warstwę korekcji dryfu dla wszystkich typów w trybie prędkościowym.

### 4.3 Wniosek / rekomendacja

Tak — prędkość **może i powinna** być wyznaczana jednym mechanizmem:
`rate = J(current_position) · ω_celestial` (albo równoważnie różnicowo z
transformacji `f`). Wszystkie trzy typy stają się wtedy jednym przypadkiem
różniącym się jedynie macierzą `R`/kwaternionem `Q` i polityką zawijania osi —
co jest dokładnie tezą [`analiza_typow_montazu.md`](analiza_typow_montazu.md).

Stan obecny: **częściowa unifikacja** — ALT_AZ i CASUAL dzielą
`computeMountFrameRates()`; EQUATORIAL nadal używa osobnej, zamkniętej formuły
stałej prędkości. Docelowo: jedna funkcja `MountKinematics::trackingRates()`
z EQ/ALT_AZ jako szczególnymi wartościami `Q` (stały vs zmienny jakobian), plus
wspólna warstwa: zawijanie najkrótszą ścieżką, korekcje aparentne i dryf.

### 4.4 Uwaga o EQUATORIAL: stała prędkość = założenie idealnej kalibracji

Stała prędkość (sidereal w HA, `0` w Dec) jest **analitycznie poprawna tylko dla
idealnie skalibrowanego i wyrównanego montażu równikowego**. To założenie w
rzeczywistości nie zachodzi — pojawiają się między innymi:

- **błąd wyrównania na biegunie (polar alignment error)** — oś obrotu RA nie
  pokrywa się z biegunem, więc Dec dryfuje, a dryf zależy od HA i Dec (człon
  `cos/tan` znika tylko przy idealnym wyrównaniu);
- **refrakcja atmosferyczna** — zależna od wysokości nad horyzontem modyfikuje
  aparentną prędkość celu, szczególnie nisko nad horyzontem (model Saemundsson);
- **ugięcie (flexure) tubusu i osi, błąd ortogonalności (cone error,
  non‑perpendicularity), błąd indeksu, nie‑prostopadłość osi** — model
  geometryczny TPOINT;
- **błąd okresowy (periodic error) oraz błąd skali prędkości napędu** — PE i
  niedokładność prędkości sideralnej ślimaka;
- **refrakcja różnicowa i zmiany atmosferyczne** w trakcie sesji.

Wniosek: w rzeczywistości także dla EQUATORIAL wymagana prędkość **zależy od
pozycji i warunków**, więc „stała prędkość” jest jedynie członem wiodącym:

```
rate_Eq(current, t) = ω_sidereal + korekty(refrakcja, nutacja, TPOINT, PE, wyrównanie)
```

Dlatego EQ również pasuje do jednolitego ujęcia `rate = J(current)·ω + korekty`.
Różnica względem ALT_AZ/CASUAL jest tylko taka, że w EQ jakobian jest (w
przybliżeniu) stały, a w rzeczywistości trzeba go korygować o błędy kalibracji —
nie odwrotnie (nie zakładać stałości).

Praktyczna konsekwencja: dla EQUATORIAL potrzebne jest sprzężenie zwrotne
(korekcja dryfu — już obecna w trybie velocity — oraz autoguiding), a docelowo
feed‑forward z jakobianem uwzględniającym refrakcję/TPOINT. W przeciwnym razie
stała prędkość skutkuje systematycznym dryfem i rotacją pola obrazu.

> **Implikacja dla unifikacji:** nie ma przesłanek, by EQ traktować odrębnie
> tylko dlatego, że „ma stałą prędkość”. Stałość jest przybliżeniem wynikającym
> z idealnej kalibracji, więc wspólny mechanizm `trackingRates(current, target)`
> jest właściwy dla wszystkich trzech typów; dla EQ redukuje się on do stałej
> w warunkach idealnych i automatycznie zawiera korekty w warunkach realnych.

---

## 5. Plan unifikacji wyznaczania prędkości

### 5.1 Docelowa abstrakcja

Jedna funkcja liczona na podstawie **pozycji bieżącej** i **celu** (przez
macierz transformacji), wspólna dla wszystkich typów:

```cpp
struct TrackingRates { double axis1{0.0}; double axis2{0.0}; };  // servo deg/s

struct MountKinematics {
    // Pozycja bieżąca (telescope deg) + cel (RA/Dec) + czas → prędkości obu osi.
    TrackingRates trackingRates(double axis1_tel, double axis2_tel,
                                double target_ra_h, double target_dec_deg,
                                double jd, config::TrackingMode mode) const;
};
```

Wewnątrz:
1. **Cel aparentny** — RA/Dec + opcjonalnie nutacja / TPOINT / refrakcja.
2. **Transformacja** `axes = f(RA, Dec, jd)` — macierz/kwaternion `Q` zależny od
   typu (EQ/ALT_AZ/CASUAL to jeden wzór, różne `Q`).
3. **Pochodna** — `rate = J(current) · ω_celestial · trackingModeFactor(mode)`
   (jakobian) lub równoważnie różnicowo.
4. **Polityka osi** — zawijanie najkrótszą ścieżką (`Δ mod 360`), przycięcie
   osobliwości zenitalnej, ograniczenia.
5. **Feed‑forward + sprzężenie zwrotne** — wspólna warstwa korekcji dryfu/guidingu.

### 5.2 Mapowanie typów na jedno ujęcie

| Typ | transformacja `f` | jakobian | uwaga |
|-----|-------------------|----------|-------|
| EQUATORIAL | `(LST − RA, Dec)` | ~stały `[[1,0],[0,1]]` | + korekty realne (§4.4) |
| ALT_AZ | `equatorialToHorizontal` | zmienny (`cos alt`, `cos az`) | `Q = I` |
| CASUAL | `equatorialToMountOrientation` | zmienny | dowolny `Q` |

EQ to przypadek **stałego jakobianu**; ALT_AZ to CASUAL z `Q = I`. Jedna
implementacja pokrywa wszystkie trzy.

### 5.3 Warianty implementacji pochodnej

- **A — analityczny jakobian (preferowany):** stabilny, tani, deterministyczny.
- **B — różnica skończona** `(f(t+dt) − f(t))/dt`: prostsza, podatna na szum
  (dt, kwantyzacja, fp) i wymaga ostrożnego zawijania; przydatna jako **oracle**
  w testach walidujących wariant A.

### 5.4 Fazy wdrożenia (przyrostowe, bez regresji)

1. **Faza 0 — baseline/regresja:** zapisać obecne prędkości (EQ, ALT_AZ, CASUAL)
   jako referencję w testach porównawczych.
2. **Faza 1 — EQ w nowym API:** wprowadzić `MountKinematics::trackingRates()`
   i wyrazić EQ (stała prędkość) jako przypadek stałego jakobianu. Testy EQ
   muszą być bit‑w‑bit zgodne (w tolerancji) z baseline — zero zmiany zachowania.
3. **Faza 2 — ALT_AZ/CASUAL do tego samego API:** przenieść obecną logikę
   [`computeMountFrameRates()`](../../src/controllers/mount_controller.cpp:6961)
   do wspólnej funkcji (bez zmiany numeryki); usunąć duplikację. Testy: ALT_AZ
   = analitycznie (tolerancja `1e-12`), CASUAL z `Q = I` = ALT_AZ.
4. **Faza 3 — korekty aparentne:** opcjonalnie włączyć refrakcję/nutację/TPOINT
   do wspólnej ścieżki feed‑forward (bramkowane flagami konfiguracji).
5. **Faza 4 — wspólny dryf/guiding:** ujednolicić korekcję dryfu (dziś tylko
   EQ‑velocity) jako wspólną warstwę sprzężenia zwrotnego dla trybu prędkościowego.
6. **Faza 5 — sprzątanie i dokumentacja:** usuwać stare ścieżki, aktualizować
   [`analiza_typow_montazu.md`](analiza_typow_montazu.md) i niniejszy dokument.

### 5.5 Ryzyka i przeciwdziałanie

| Ryzyko | Przeciwdziałanie |
|--------|------------------|
| Zawinięcie osi okresowej daje fałszywy skok prędkości | liczyć deltę po najkrótszej ścieżce (`Δ mod 360`), nie `target − current` |
| Szum różnicy skończonej (wariant B) | użyć jakobianu analitycznego (A) produkcyjnie |
| Regresja zachowania EQ | testy porównawcze 1:1 z baseline (Faza 0/1) |
| Tryb pozycyjny EQ nie wysyła prędkości hosta | unifikacja dotyczy feed‑forward/wektora prędkości (integracja, KF, tryb prędkościowy); position mode zostaje, ale może korzystać z tego samego `f` |
| Korekty aparentne zmieniają prędkość | włączać świadomie flagami; w razie wątpliwości feed‑forward + sprzężenie zwrotne |

### 5.6 Kryteria akceptacji

- **EQ** (idealne warunki): `axis1 = ω_sidereal·gear_ha`, `axis2 = 0` — zgodne z
  obecnym (tolerancja).
- **ALT_AZ:** zgodność z obecną `computeMountFrameRates()` (tolerancja `1e-12`).
- **CASUAL `Q = I`:** identyczne jak ALT_AZ.
- **Zawijanie:** brak fałszywych skoków prędkości dla celu przesuniętego o ±360°.
- **Testy:** pełny `ctest` zielony + nowe testy porównawcze feed‑forward.
