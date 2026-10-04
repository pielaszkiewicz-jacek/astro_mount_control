# Analiza: dlaczego typy montażu mają różne implementacje

> **Pytanie wyjściowe:** Dlaczego istnieje osobna implementacja dla montażu
> EQUATORIAL, ALT_AZ i CASUAL, skoro z matematycznego punktu widzenia różnią się
> tylko macierzą obrotu?

## 1. Odpowiedź w jednym zdaniu

Matematycznie teza jest **słuszna** — to jedna rodzina transformacji różniąca się
macierzą/kwaternionem. Różne implementacje w kodzie wynikają z **historii
rozwoju** oraz **pragmatyki bezpieczeństwa**, a nie z konieczności matematycznej.
Rzeczywiście różnicującą częścią jest nie sama macierz, lecz:

- **(a)** pochodna tej macierzy — prędkości śledzenia zależne od pozycji,
- **(b)** polityka osi — zawijanie, meridian flip, singularność zenitalna, park.

## 2. Formalizm matematyczny

Wszystkie trzy typy to ten sam 2‑DOF gimbal kartezjański. Różni je **jedna macierz
obrotu** `R`, wiążąca wektor w układzie nieba/ENU z wektorem w układzie osi montażu:

| Typ montażu | Macierz / transformacja `R` |
|-------------|------------------------------|
| **EQUATORIAL** | obrót do ramy HA/Dec (oś biegunowa na północ) |
| **ALT_AZ** | obrót o `(90° − φ)` wokół osi wschód‑zachód |
| **CASUAL** (w kodzie: „custom/arbitrary”) | dowolny kwaternion `Q` |

Kluczowa obserwacja: [`CASUAL` z `Q = R_altaz(φ)`](../../include/config/mount_config.h:82)
**jest** `ALT_AZ`, a z `Q = R_equatorial` **jest** `EQUATORIAL`. Formalnie to jeden
uogólniony przypadek, a pozostałe dwa są jego szczególnymi wartościami.

> **Uwaga terminologiczna:** w kodzie nie istnieje osobny typ `CUSTOM` — odpowiednikiem
> „custom/dowolnie zorientowanego” jest [`MountType::CASUAL`](../../include/config/mount_config.h:82).
> Enum zawiera: `EQUATORIAL`, `ALT_AZ`, `UNKNOWN`, `CASUAL`.

## 3. Gdzie w kodzie jest rozgałęzienie

Konwersja celu w `slewToEquatorial()` ma trzy osobne gałęzie:

| Typ | Lokalizacja | Sposób liczenia |
|-----|-------------|-----------------|
| EQUATORIAL | [`if (mount_type == EQUATORIAL)`](../../src/controllers/mount_controller.cpp:720) | `HA = LST − RA`, skalowanie przez `gear_ratio`, najkrótsza ścieżka HA + `resolveDecTarget()` |
| CASUAL | [`else if (mount_type == CASUAL)`](../../src/controllers/mount_controller.cpp:779) | `equatorialToMountOrientation()` przez kwaternion |
| ALT_AZ | [`else // ALT_AZ`](../../src/controllers/mount_controller.cpp:797) | `equatorialToHorizontal()` (SOFA), konwencja kanoniczna: oś1 = wysokość, oś2 = azymut |

Pozostałe rozjechane obszary:

| Obszar | Miejsce |
|--------|---------|
| Prędkości śledzenia | zunifikowane: [`computeMountFrameRates()`](../../src/controllers/mount_controller.cpp:6969) (ALT_AZ = CASUAL z `Q = I`) vs stały rate EQ ([komentarz](../../src/controllers/mount_controller.cpp:146)) |
| Korekcje pozycji | [`applyEquatorialCorrections()`](../../src/controllers/mount_controller.cpp:7331) vs round‑trip przez RA/Dec ([linia 2142](../../src/controllers/mount_controller.cpp:2142)) |
| Normalizacja osi | [linie 3447–3465](../../src/controllers/mount_controller.cpp:3447) |
| Rotacja pola | [`FieldRotationModel`](../../include/models/field_rotation_model.h:16) — 3 metody |
| Derotator | [switch po `MountKind`](../../src/controllers/derotator_controller.cpp:73) |
| Bootstrap / kalibracja | [uniwersalny Wahba/SVD](../../src/controllers/mount_controller.cpp:3747) |

## 4. Dlaczego tak się stało (przyczyny)

### 4.1 Ewolucja historyczna
EQUATORIAL i ALT_AZ powstały pierwsze z formuł bezpośrednich (HA/Dec oraz SOFA
`iauHd2ae`). CASUAL dodano później jako generalizację kwaternionową (raporty
„P5”, faza 3). Kod rósł przez doklejanie — stąd trzy kopie mapy współrzędnych.

### 4.2 Różni się nie tylko mapa, ale jej pochodna czasowa
- EQUATORIAL śledzi ze **stałą** prędkością (sidereal w RA, 0 w Dec).
- ALT_AZ/CASUAL wymagają prędkości **zależnych od pozycji**, bo `R(t)` jest zmienne
  w czasie — trzeba liczyć jakobian `∂(a1,a2)/∂(RA,Dec)·ω`, a nie mnożyć przez
  statyczną macierz.

### 4.3 Różnice fizyczne / politykowe, których macierz nie wyraża
- **Meridian flip** istnieje tylko dla EQUATORIAL
  ([odrzucanie dla pozostałych](../../src/controllers/mount_controller.cpp:6746),
  [`time_to_meridian_ = 24`](../../src/controllers/mount_controller.cpp:1663)).
- **Zawijanie osi** ma inną semantykę: HA w `[-180,180]` i Dec w `[-90,90]` dla EQ;
  azymut `[0,360)` dla ALT_AZ; dla CASUAL „azymutopodobna” jest oś2, dla ALT_AZ oś1.
- **Singularność zenitalna** Alt‑az (`clamp cos(alt)`), której równikowy nie ma.
- **Pier‑side / Dec multi‑turn** (`resolveDecTarget()`), domyślny park = NCP,
  offset home, TPOINT zadawany w HA/Dec (dla alt‑az trzeba round‑tripować).
- **Rotacja pola / derotator** — inny model dla każdego typu.

### 4.4 Pragmatyzm
Bezpośrednie formuły są łatwiejsze do weryfikacji i bezpieczniejsze — EQ nie
ryzykuje regresji przez ogólną macierz, a zachowania specyficzne dla montażu
(limit zenitalny, flip) łatwiej wyrazić lokalnie.

## 5. Dowód, że unifikacja jest możliwa — i już się zaczęła

Bootstrap kalibracji **został już zunifikowany**: zamiast wcześniejszego
rozgałęzienia (CASUAL → Wahba/SVD, EQ/ALT_AZ → mean‑offset) jest teraz jeden
uniwersalny Wahba/SVD dla wszystkich typów — patrz
[`// --- Universal Wahba/SVD for ALL mount types (plan §8.6) ---`](../../src/controllers/mount_controller.cpp:3747)
oraz [analiza_projektu.md](analiza_projektu.md:127).

To dowodzi, że rozjazd nie wynika z konieczności matematycznej, tylko z narosłej
separacji — i że kierunek unifikacji jest już obrany.

## 6. Rekomendowana unifikacja (strategia + deskryptor polityki)

Zamiast `if (mount_type == ...)` rozsianych po wielu miejscach — jedna kinematyka
oparta o rotację plus mały opis polityki:

```cpp
struct MountKinematics {
    // Jedna wspólna transformacja; szczególne przypadki to konkretne R:
    //   EQUATORIAL: R_eq,  ALT_AZ: R_altaz(φ),  CASUAL: dowolny Q
    std::pair<double,double> skyToMount(double ra, double dec, double jd) const;
    std::pair<double,double> mountToSky(double a1, double a2, double jd) const;
    // Jakobian ∂(a1,a2)/∂(RA,Dec) * ω — wspólny dla wszystkich:
    std::pair<double,double> trackingRates(TrackingMode m, double a1, double a2) const;
};

struct MountPolicy {
    bool supports_meridian_flip;   // EQ = true
    int  wrap_axis;                // 1 = HA/azymut, 2 = Dec/azymut
    double min_cos_face;           // 1.0 dla EQ, ~cos(89.5°) dla ALT_AZ
    std::pair<double,double> park_default;  // NCP dla EQ
};
```

`EQUATORIAL` i `ALT_AZ` stają się wtedy **szczególnymi przypadkami CASUAL** z
ustalonym `Q` (odpowiednio `Q_eq` i `R_altaz(φ)`), a prędkości liczone są raz —
przez jakobian wspólnej transformacji (dokładnie to robią dziś `computeAltAzRates`
i `computeCasualRates`, tylko osobno). Zachowania bezpieczeństwa przenoszą się do
`MountPolicy`, więc **nie ginie żadna logika specyficzna dla montażu** — znika
jedynie duplikacja mapy współrzędnych.

### 6.1 Stan wdrożenia (unifikacja obliczeń)

Zrealizowano (dokładnie równoważne numerycznie, potwierdzone pełnym zestawem
testów 23/23):

- **Wspólne prędkości śledzenia** — `computeMountFrameRates()` zastępuje
  `computeAltAzRates()` + `computeCasualRates()`; ALT_AZ to przypadek CASUAL z
  `Q = I` (rotacja ENU→montaż jest tożsamością), więc jedna funkcja obsługuje oba
  typy ([`mount_controller.cpp:6969`](../../src/controllers/mount_controller.cpp:6969)).
- **Forward-mapping w korekcjach nie-równikowych** — konwersja bieżącej pozycji
  montażu do RA/Dec w bloku nutacja/TPoint/refrakcja używa jednego wywołania
  `mountOrientationToEquatorial()` z efektywnym `Q` (tożsamość dla ALT_AZ)
  ([`mount_controller.cpp:2199`](../../src/controllers/mount_controller.cpp:2199)).
- **Ujednolicona konwencja osi ALT_AZ (i CASUAL)** — przyjęto kanonicznie
  `axis1 = wysokość, axis2 = azymut`. Usunięto rozjazd: `slewToEquatorial()`,
  `slewToHorizontal()` (gałęzie ALT_AZ i CASUAL), `startTracking()`,
  `getStatus()` i `getRotationMatrix()` używają tej samej konwencji, a warunki
  limitów miękkich sprawdzają oś1 (wysokość) i pomijają oś2 (azymut jest
  okresowy). Zaktualizowano testy jednostkowe.
- **Jednolita polityka limitów miękkich** — `SoftLimitPolicy` +
  `softLimitPolicyFor()` to jedno źródło prawdy dla wszystkich sprawdzeń:
  `slewToEquatorial()`, `slewToHorizontal()`, `startTracking()` oraz pętla
  śledzenia (`computeSoftLimits()`/`evaluateSoftLimits()`). Oś1 jest zawsze
  limitowana, a okresowa oś2 (azymut) dla ALT_AZ/CASUAL nie jest — naprawia to
  także błąd, w którym pętla śledzenia redukowała `rate_factor` na podstawie
  odległości azymutu od limitów osi2.

Pozostaje do unifikacji (świadomie odłożone):

- **`MountPolicy`** (zawijanie osi, fold Dec, wsparcie flipa, domyślny park) —
  wart wyodrębnienia jako deskryptor; sprawdzenia limitów są już zunifikowane,
  ale polityka flipa/parku `resolveDecTarget()`/`computeFlipTargets()` pozostaje
  rozproszona.
- **Backward-mapping `equatorial → mount` w korekcjach** — ścieżka CASUAL wymusza
  refrakcję wewnątrz `equatorialToMountOrientation()`, a ALT_AZ steruje nią flagą
  `enable_refraction_correction`; unifikacja wymaga jawnego parametru refrakcji.

## 7. Wniosek

Różne implementacje wynikają z **historii kodu i pragmatyzmu bezpieczeństwa**, a
nie z matematyki. Teza z pytania jest słuszna: to jedna rodzina transformacji
różniąca się macierzą/kwaternionem. Rzeczywiście różniącą się częścią jest nie
sama macierz, lecz:

1. pochodna tej macierzy (prędkości zależne od pozycji), oraz
2. polityka osi (zawijanie, flip, singularność zenitalna, park).

Kod sam już zaczął iść w stronę unifikacji (uniwersalny Wahba/SVD, komentarze
„supports ALL mount types”), więc naturalny kolejny krok to jedna
`MountKinematics` (z EQ/ALT_AZ jako stałymi `Q`) plus `MountPolicy` zastępujące
rozproszone gałęzie.
