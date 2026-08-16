# Analiza stabilności, stabilności numerycznej i poprawności numerycznej

**Data analizy:** 2026-08-11
**Zakres:** cały projekt `astro_mount_control` — moduły liczące (astronomia, modele, sterowniki, kalibracje, interpolacje).
**Cel:** identyfikacja zagrożeń dla stabilności systemu, niestabilności numerycznych oraz błędów poprawności (jednostki, wzory, osobliwości).

---

## 0. Status napraw (2026-08-11)

Wszystkie błędy poprawności i zagrożenia stabilności opisane w niniejszym raporcie zostały **naprawione** (lub, tam gdzie zmiana przepływu sterowania byłaby ryzykowna, udokumentowane). Weryfikacja: pełny build (`make -j`) bez błędów oraz wszystkie testy jednostkowe przeszły:

- `test_astronomical_calculations` — 22/22 ✅
- `test_tpoint_model` — 15/15 ✅
- `test_kalman_filter` — 22/22 ✅
- `test_mount_controller` — 128/128 ✅
- `test_ephemeris_tracker` — 68/68 ✅
- `test_subarcsecond_accuracy` — 6/6 ✅ („Sub-arcsecond accuracy achieved”)

| # | Poprawka | Status | Lokalizacja |
|---|----------|--------|-------------|
| B1 | LST: `lon*D2R/15.0` → `lon*D2R` (2 miejsca) | ✅ naprawione | [astronomical_calculations.cpp:222](../src/core/astronomical_calculations.cpp:222), [:269](../src/core/astronomical_calculations.cpp:269) |
| B2 | GMST: usunięto błędne dodawanie ΔAT do UT1 | ✅ naprawione | [astronomical_calculations.cpp:577](../src/core/astronomical_calculations.cpp:577) |
| B3 | TPoint Newton: usunięto błędne podziały kroku (15·3600, 3600) | ✅ naprawione | [tpoint_model.cpp:288](../src/models/tpoint_model.cpp:288) |
| B4 | Refrakcja: jedna korekcja temperatury, usunięto 1.33322 | ✅ naprawione | [astronomical_calculations.cpp:180](../src/core/astronomical_calculations.cpp:180) |
| B5 | FocusCurve: dane odśrodkowane + rozwiązywanie QR zamiast Cramera | ✅ naprawione | [focus_curve.cpp:93](../src/models/focus_curve.cpp:93) |
| B6 | ProperMotion: clamp `cos(δ)` przy biegunach | ✅ naprawione | [astronomical_calculations.cpp:745](../src/core/astronomical_calculations.cpp:745) |
| B7 | FieldRotation: usunięto `tan(δ)`, strażnik mianownika, strażnik NaN | ✅ naprawione | [field_rotation_model.cpp:6](../src/models/field_rotation_model.cpp:6) |
| B8 | ExposurePlanner: poprawna formuła kwadratowa (SNR z tłem i szumem odczytu) | ✅ naprawione | [exposure_planner.cpp:52](../src/models/exposure_planner.cpp:52) |
| R1 | KF: jawna kontrola PD macierzy S + regularyzacja | ✅ naprawione | [kalman_filter.cpp:169](../src/models/kalman_filter.cpp:169) |
| R2 | KF: metryka stabilności = promień spektralny F (zamiast F+Fᵀ) | ✅ naprawione | [kalman_filter.cpp:691](../src/models/kalman_filter.cpp:691) |
| R3 | KF: adaptacyjne Q — skalowanie tylko diagonali + clamp poza-diagonali | ✅ naprawione | [kalman_filter.cpp:260](../src/models/kalman_filter.cpp:260) |
| R7 | evaluateSoftLimits: `std::fmod` zamiast pętli `while` | ✅ naprawione | [mount_controller.cpp:6674](../src/controllers/mount_controller.cpp:6674) |
| Degeneracja TPoint | Ostrzeżenie przy jednoczesnym WORM_ERROR i POLAR_AZ | ✅ dodane | [tpoint_model.cpp:45](../src/models/tpoint_model.cpp:45) |
| R6 | KF w pętli trackingu — przepływ **celowo niezmieniony**, dodano dokumentację projektu | 📝 udokumentowane | [mount_controller.cpp:1809](../src/controllers/mount_controller.cpp:1809) |

**Uwaga dot. R6:** zmiana przepływu (podawanie do KF surowego pomiaru sprzed przesunięcia kinematycznego) wymagałaby uczynienia KF autorytatywnym integratora z przestrojonymi szumami Q/R — bez tego silny pomiar anulowałby przesunięcie `rate·dt` i zatrzymał montaż. Decyzja inżynierska: pozostawiono poprawny (bez podwójnego liczenia prędkości) przepływ i udokumentowano uzasadnienie. Właściwym przyszłym ulepszeniem jest odczyt surowego enkodera w każdej iteracji + integracja przez KF z odpowiednim tuningiem.

---

## 1. Streszczenie (kluczowe ustalenia)

System jest generalnie dobrze zabezpieczony przed propagacją NaN/Inf — w pętli trackingu [`mount_controller.cpp`](../src/controllers/mount_controller.cpp) oraz w obliczeniach astronomicznych występują liczne strażniki `std::isfinite()`, clampingi osobliwości (bieguny, zenit, horyzont) oraz stabilne dekompozycje (Joseph form w KF, LDLT zamiast odwracania macierzy, ColPivHouseholderQR w TPoint, barycentryczna interpolacja Lagrange'a w efemerydach). To jest mocna strona projektu.

Jednak analiza ujawnia **kilka realnych błędów poprawności numerycznej** (głównie błędy jednostek i wzorów), które w scenariuszach rzeczywistych dają systematyczne błędy wskazywania/trackingu rzędu minut kątowych:

| # | Priorytet | Moduł | Opis | Lokalizacja |
|---|-----------|-------|------|-------------|
| B1 | 🔴 Wysoki | `astronomical_calculations` | Błędne dodanie długości geograficznej do LST (`lon*D2R/15` zamiast `lon*D2R`) — błąd ~0.93h/14° przy λ=15° | [astronomical_calculations.cpp:222](../src/core/astronomical_calculations.cpp:222), [astronomical_calculations.cpp:269](../src/core/astronomical_calculations.cpp:269) |
| B2 | 🔴 Wysoki | `astronomical_calculations` | `calculateGMST` dodaje ΔAT=37 s jako przybliżenie UT1 (UT1−UTC < 0.9 s) — błąd ~36 s ≈ 9.3′ w GMST/LST | [astronomical_calculations.cpp:577](../src/core/astronomical_calculations.cpp:577) |
| B3 | 🔴 Wysoki | `tpoint_model` | Newton w `predictMountPosition` — krok dzielony przez 15·3600/3600 mimo że delta jest już w h/° → iteracja praktycznie zamrożona | [tpoint_model.cpp:288](../src/models/tpoint_model.cpp:288) |
| B4 | 🟠 Średni | `astronomical_calculations` | `applyAtmosphericRefraction` — podwójna korekcja temperaturowa (t_k²) i spurious 1.33322 (mmHg) → refrakcja niedoszacowana ~25% | [astronomical_calculations.cpp:180](../src/core/astronomical_calculations.cpp:180) |
| B5 | 🟠 Średni | `focus_curve` | Cramer dla b/c w dopasowaniu paraboli — błędne formuły (c = a), złe parametry wierzchołka | [focus_curve.cpp:131](../src/models/focus_curve.cpp:131) |
| B6 | 🟠 Średni | `astronomical_calculations` | `applyProperMotion` — brak zabezpieczenia 1/cos(dec) przy biegunach | [astronomical_calculations.cpp:745](../src/core/astronomical_calculations.cpp:745) |
| B7 | 🟡 Niski | `field_rotation_model` | brak osłon osobliwości tan(dec) i mianownika 1−sin²lat·sin²dec | [field_rotation_model.cpp:11](../src/models/field_rotation_model.cpp:11) |
| B8 | 🟡 Niski | `exposure_planner` | formuła czasu ekspozycji wymiarowo błędna | [exposure_planner.cpp:71](../src/models/exposure_planner.cpp:71) |

Osobno zidentyfikowano zagrożenia stabilności i luki funkcjonalne (rozdział 6) oraz listę praktyk wzorcowych (rozdział 7).

---

## 2. Obliczenia astronomiczne — [`astronomical_calculations.cpp`](../src/core/astronomical_calculations.cpp)

### 2.1. 🔴 B1 — Błąd jednostek długości geograficznej w konwersji równikowych ↔ horyzontalnych

[`equatorialToHorizontal()`](../src/core/astronomical_calculations.cpp:206) i [`horizontalToEquatorial()`](../src/core/astronomical_calculations.cpp:243):

```cpp
double gast = iauGst94(jd, 0.0);          // GAST w radianach (SOFA)
double lst = gast + lon * D2R / 15.0;     // BŁĄD: powinno być lon * D2R
```

`iauGst94()` zwraca **radiany**. LST w radianach = GAST + λ (radiany). Dodanie `lon*D2R/15` (czyli λ/15) zamiast `lon*D2R` (czyli λ) daje błąd:

- λ = 15°E → poprawny LST przesunięty o 1 h, kod daje 1/15 tej poprawki → błąd ~0.933 h = **~14° w HA**.
- Błąd przenosi się na azymut/altitude w [`equatorialToMountOrientation()`](../src/core/astronomical_calculations.cpp:526) i [`mountOrientationToEquatorial()`](../src/core/astronomical_calculations.cpp:514), a przez nie na slewy ALT_AZ/CASUAL ([`slewToEquatorial()`](../src/controllers/mount_controller.cpp:608)) oraz na korekty astronomiczne w pętli trackingu.

> **Wniosek:** dla obserwatoriów o λ ≠ 0 wskazywanie ALT_AZ/CASUAL jest systematycznie przesunięte o ~0.93 h·(λ/15°). Bug prawdopodobnie maskowany w testach, gdzie λ = 0.

**Proponowana poprawka:**
```cpp
double lst = gast + lon * D2R;   // radiany
```

### 2.2. 🔴 B2 — `calculateGMST` — błędne przybliżenie UT1

[`calculateGMST()`](../src/core/astronomical_calculations.cpp:577):

```cpp
double delta_at = 37.0;              // ΔAT = TAI − UTC
double jd_ut1 = jd + delta_at / 86400.0;   // "Approximate UT1 ≈ TAI"
return iauGst94(jd_ut1, 0.0) * R2D / 15.0;
```

Zależności czasowe: **UT1 − UTC = DUT1 ∈ (−0.9 s, +0.9 s)**; **TAI − UTC = ΔAT = 37 s**. Dodanie ΔAT=37 s zamiast DUT1 wprowadza błąd ~36 s w skali czasowej UT1. Przy prędkości obrotu Ziemi ~15.041″/s daje to błąd **~556″ ≈ 9.3′** w GMST, a więc i w LST.

`calculateLST()` jest używane w [`slewToEquatorial()`](../src/controllers/mount_controller.cpp:582), [`startTracking()`](../src/controllers/mount_controller.cpp:1369) oraz w [`calculateParallacticAngle()`](../src/core/astronomical_calculations.cpp:626). Efekt to **stałe przesunięcie celu RA/HA o ~0.15°** (nie dryf — mount i tak jedzie z prędkością gwiazdową).

> Komentarz w kodzie jest wewnętrznie sprzeczny: „UT1 differs from UTC by ΔAT” — to nieprawda (UT1 różni się od UTC o DUT1 < 0.9 s, to TAI różni się od UTC o ΔAT).

**Proponowana poprawka:** przyjąć `jd_ut1 = jd` (DUT1 < 1 s jest pomijalne dla celu ~1″) lub użyć `iauUtcut1`/IERS Bulletyn A.

### 2.3. 🟠 B4 — `applyAtmosphericRefraction` — podwójna korekcja temperatury i współczynnik mmHg

[`applyAtmosphericRefraction()`](../src/core/astronomical_calculations.cpp:165):

```cpp
double p_corr = pressure_mbar * 283.15 / (t_k * 1.33322);
...
r *= p_corr / 1010.0 * 283.15 / t_k;
```

Podstawiając `p_corr`:
```
r_final = r * (P·283.15²) / (1010 · 1.33322 · t_k²)
```

Poprawna korekcja Saemundssoy: `r · (P/1010) · (283.15/t_k)`. W kodzie:
- temperatura występuje **w kwadracie** (t_k²) — podwójne uwzględnienie;
- dodatkowy czynnik `1.33322` (1013.25/760, przelicznik mmHg) jest bez znaczenia dla ciśnienia w hPa.

W warunkach standardowych (T=10 °C, P=1010) refrakcja jest **niedoszacowana o ~25%** (czynnik 1/1.33322). Przy innych temperaturach błąd rośnie dodatkowo przez t_k².

### 2.4. 🟠 B6 — `applyProperMotion` — osobliwość 1/cos(dec)

[`applyProperMotion()`](../src/core/astronomical_calculations.cpp:726):

```cpp
double ra1 = ra_rad + pm_ra_rad * dt_years / cos(dec_rad);
```

Brak zabezpieczenia przed `cos(dec_rad) → 0` przy |δ| → 90°. Przy obiektach biegunowych dzielnik dąży do 0 → ekstremalne wzmocnienie lub NaN. W pozostałych częściach kodu osobliwości tego typu są clampowane (np. [`calculateParallacticAngle()`](../src/core/astronomical_calculations.cpp:637), [`applyGuiderCorrection()`](../src/controllers/mount_controller.cpp:4453)) — tutaj zabezpieczenia brakuje.

### 2.5. Uwagi pozytywne

- Precesja/nutacja/aberracja przez **IAU 2006** (`iauPmat06`, `iauNut06a`, `iauObl06`) zamiast starszych modeli — komentarze w kodzie poprawnie dokumentują poprawę.
- Podział dat juliańskich na dwie części (`DJ00` i przesunięcie) w [`applyPrecession()`](../src/core/astronomical_calculations.cpp:84) — dobra praktyka precyzji.
- Klampowanie refrakcji przy zenicie ([astronomical_calculations.cpp:191](../src/core/astronomical_calculations.cpp:191)) z poprawnym uzasadnieniem fizycznym.
- Strażniki NaN w [`calculateFieldRotation()`](../src/core/astronomical_calculations.cpp:344) i [`calculateParallacticAngle()`](../src/core/astronomical_calculations.cpp:613).

---

## 3. Filtr Kalmana — [`kalman_filter.cpp`](../src/models/kalman_filter.cpp)

### 3.1. Ocena ogólna — dobra praktyka

- **Joseph form** aktualizacji kowariancji ([kalman_filter.cpp:199](../src/models/kalman_filter.cpp:199)) — zapewnia symetrię i dodatnią półokreśloność P nawet przy niedokładnym wzmocnieniu.
- Rozwiązywanie wzmocnienia przez **LDLT** zamiast jawnej odwrotności ([kalman_filter.cpp:171](../src/models/kalman_filter.cpp:171)).
- **Renormalizacja kwaternionu** po predykcji ([kalman_filter.cpp:130](../src/models/kalman_filter.cpp:130)) — kluczowe dla długich sesji.
- Symetryzacja P po każdym kroku.
- Log-wyznacznik przez LLT w [`computeLogLikelihood()`](../src/models/kalman_filter.cpp:359) — stabilnie.

### 3.2. Zagrożenia / uwagi

- **R1 (stabilność):** `LDLT` nie sygnalizuje niezawodnie osobliwości — `info()` może zwrócić `Success` dla macierzy półokreślonej z zerowym pivotem, a `solve()` daje wtedy Inf/NaN. Fallback do `.inverse()` wyzwala się tylko przy `info()!=Success`. W praktyce S = HPHᵀ+R jest dodatnio określona (R>0), więc ryzyko jest niskie, ale brak jawnego sprawdzenia PD/pivotów. ([kalman_filter.cpp:171-187](../src/models/kalman_filter.cpp:171))
- **R2 (poprawność metryki):** `filter_stability` w [`getAdvancedMetrics()`](../src/models/kalman_filter.cpp:691) liczy wartości własne F+Fᵀ. Dla F=I (typowy przypadek) F+Fᵀ=2I → wartość własna 2>0 → metryka **zawsze zwraca 0** („niestabilny”). Metryka jest bezwartościowa i myląca.
- **R3 (adaptacyjne Q):** `Q_ *= scale_factor` skaluje całą macierz (w tym poza-diagonalne), a klamp dotyczy tylko diagonali [1e-6, 100] — elementy poza-diagonalne mogą rosnąć bez ograniczeń. ([kalman_filter.cpp:265](../src/models/kalman_filter.cpp:265))
- **R4 (spójność):** renormalizacja kwaternionu nie jest odzwierciedlona w propagacji kowariancji (P nie jest renormalizowane) — drobna niespójność, w praktyce pomijalna.
- **R5:** [`computeSigmaPoints()`](../src/models/kalman_filter.cpp:421) używa LDLT i klampuje ujemne wartości własne do 0 — poprawne zabezpieczenie dryfu numerycznego.

---

## 4. Sterownik montażu — [`mount_controller.cpp`](../src/controllers/mount_controller.cpp)

### 4.1. Pozytywne zabezpieczenia (bardzo dobre)

- Liczne strażniki `std::isfinite()` po każdym kroku aktualizacji pozycji, KF, nutacji, korekt astronomicznych — przejście do stanu ERROR zamiast propagacji NaN ([mount_controller.cpp:1800](../src/controllers/mount_controller.cpp:1800), [mount_controller.cpp:1828](../src/controllers/mount_controller.cpp:1828)).
- Klamp osobliwości: cos(alt) przy zenicie ([mount_controller.cpp:2318](../src/controllers/mount_controller.cpp:2318)), cos(lat) przy biegunie, cos(dec) w guiderze ([mount_controller.cpp:4453](../src/controllers/mount_controller.cpp:4453)) z poprawnym fizycznie uzasadnieniem (max ~11.5× amplifikacji).
- Watchdog pętli trackingu (dt > 5 s) i timeout slewu ([mount_controller.cpp:1738](../src/controllers/mount_controller.cpp:1738)).
- Guider delta jako **offset pozycji** (nie modulacja prędkości) — niezależny od dt, zużywany jednorazowo — eliminuje akumulację ([mount_controller.cpp:1758](../src/controllers/mount_controller.cpp:1758)).
- Nutacja jako **delta** (różnica względem poprzedniej iteracji), nie wartość absolutna — brak kumulacji ([mount_controller.cpp:1969](../src/controllers/mount_controller.cpp:1969)).
- `PositionKalmanFilter` z Joseph form ([mount_controller.cpp:130](../src/controllers/mount_controller.cpp:130)).

### 4.2. Uwagi projektowe

- **R6:** w pętli trackingu pozycja jest najpierw przesuwana o `current_rate*dt` ([mount_controller.cpp:1773](../src/controllers/mount_controller.cpp:1773)), a **następnie** ten sam (już przesunięty) stan podawany jest jako pomiar do `position_kf_->update(...)` ([mount_controller.cpp:1823](../src/controllers/mount_controller.cpp:1823)). KF wewnętrznie także wykonuje predykcję `pos += rate*dt`. Efekt: KF nie filtruje surowego pomiaru enkodera, tylko „wygładza” pozycję już skorygowaną kinematycznie (potencjalne podwójne liczenie prędkości, maskowanie realnego szumu enkodera). To bardziej usterka architektury pętli niż błąd numeryczny, ale warto rozważyć podawanie surowego odczytu enkodera.
- **R7:** normalizacja HA w [`evaluateSoftLimits()`](../src/controllers/mount_controller.cpp:6674) pętlą `while` — przy pozycjach serwa rzędu 10⁶ stopni (dni trackingu) to ~2800 iteracji na wywołanie (w każdej iteracji pętli). Poprawnie ograniczone, ale wolniejsze niż `std::fmod`.
- **R8:** `PositionKalmanFilter::update()` używa `S.inverse()` ([mount_controller.cpp:125](../src/controllers/mount_controller.cpp:125)) — dla macierzy 2×2 akceptowalne, ale bez jawnej kontroli osobliwości (init() zabezpiecza R≥1e-12, więc ryzyko niskie).
- **R9:** pole rotation dla ALT_AZ/CASUAL ([mount_controller.cpp:4366](../src/controllers/mount_controller.cpp:4366)) — `-ω·cos(lat)/sin(alt)` clampowane do ±20°/s; przy alt<1° wartość jest niefizyczna, ale zabezpieczona. Dobrze, że nie ma dzielenia przez 0.

---

## 5. Modele: TPoint, PEC, field rotation, efemerydy, focus, ekspozycje

### 5.1. 🟠/🔴 B3 — TPoint `predictMountPosition` — błąd jednostek w kroku Newtona

[`predictMountPosition()`](../src/models/tpoint_model.cpp:236):

```cpp
// delta = -J⁻¹·f, gdzie J = ∂f/∂(ha, dec)
// J(0,0) = ∂f_ra/∂ha  [arcsec / godzina]
// J(0,1) = ∂f_ra/∂dec [arcsec / stopień]
Vector2d delta = -J_inv * f;              // delta(0) w HOURS, delta(1) w DEGREES
...
ha_guess  += damping * delta(0) / (15.0 * 3600.0);  // BŁĄD: dzieli godziny przez 54000
dec_guess += damping * delta(1) / 3600.0;            // BŁĄD: dzieli stopnie przez 3600
```

Jacobi jest liczone metodą różnic centralnych z `eps = 1e-6` dodawanym do `ha_guess` (jednostka: **godziny**), więc `J(0,0)` ma jednostki arcsec/godzina, a `delta(0)` — godziny. Podzielenie przez 15·3600 = 54000 redukuje krok ~54 000× — iteracja praktycznie stoi. Analogicznie dla dec (÷3600). W efekcie, mimo 15 iteracji, `predictMountPosition` zwraca wynik bliski punktu startowego `(ha=0, dec)`, a kryterium zbieżności `|ra_error|<0.01″` niemal nigdy nie zostaje osiągnięte — **poprawka TPOINT jest w praktyce pomijana** w [`slewToEquatorial()`](../src/controllers/mount_controller.cpp:601) i [`startTracking()`](../src/controllers/mount_controller.cpp:1387).

**Proponowana poprawka:**
```cpp
ha_guess  += damping * delta(0);   // delta już w godzinach
dec_guess += damping * delta(1);   // delta już w stopniach
```
(lub liczyć J z eps wyrażonym w arcsec i zostawić podział — ale wtedy spójnie.)

### 5.2. 🟠 B5 — FocusCurve `fitParabolic` — błędne formuły Cramera

[`fitParabolic()`](../src/models/focus_curve.cpp:93) rozwiązuje układ równań normalnych [3×3] metodą Cramera. Formuły dla `b` ([focus_curve.cpp:135](../src/models/focus_curve.cpp:135)) i `c` ([focus_curve.cpp:139](../src/models/focus_curve.cpp:139)) **nie odpowiadają regule Cramera** — w szczególności wzór na `c` jest **identyczny ze wzorem na `a`**, co jest matematycznie niemożliwe. W efekcie parametry wierzchołka `vcurve_b = -b/(2a)` i `vcurve_c = c - b²/(4a)` są błędne, a wyznaczona pozycja ostrości (`focus_position`) i `focus_hfd` mogą być znacząco przekłamane.

Dodatkowo układ równań normalnych (Σx⁴, Σx³, …) dla pozycji focusera rzędu tysięcy jest **źle uwarunkowany** (κ ~ x²). Zalecane: fit na danych **odśrodkowanych** (`x' = x − x̄`) z użyciem QR/Cholesky’ego zamiast Cramera.

### 5.3. 🟡 B7 — FieldRotationModel — osobliwości

[`calculateAltAz()`](../src/models/field_rotation_model.cpp:6):
- `tan(dec_rad)` — osobliwość przy δ = ±90°, brak clampa;
- mianownik `1 − sin²(lat)·sin²(dec)` — dąży do 0 przy lat=dec=±90° → dzielenie przez 0.

Ten model jest używany przez [`DerotatorController::getFieldRotation()`](../src/controllers/derotator_controller.cpp:18) (które zwraca `{}` — niezaimplementowane) oraz przez `calculateFieldRotation()` w astro_calc. Warto dodać te same zabezpieczenia co w [`calculateParallacticAngle()`](../src/core/astronomical_calculations.cpp:637).

### 5.4. 🟡 B8 — ExposurePlanner `calculateExposureTime` — błąd wymiarowy

[`calculateExposureTime()`](../src/models/exposure_planner.cpp:52):

```cpp
double exposure = (target_snr * target_snr * read_noise_e * read_noise_e) /
                  (star_photon_rate * star_photon_rate);
```

Z definicji SNR = N_s/√(N_s + N_sky + N_r²). Dla źródła, gdzie dominuje szum odczytu: SNR ≈ N_s/N_r = (rate·t)/read → **t = SNR·read/rate**, a nie `SNR²·read²/rate²`. Formuła jest wymiarowo błędna (daje czas w (e⁻)²/(e⁻/s)² = s²). Dla docelowego SNR=100 i read=10 e⁻, rate=1000 e⁻/s daje 1 s (poprawna wartość) vs kod: 10 s. Warto zweryfikować i poprawić wzór.

### 5.5. PEC — [`pec_model.cpp`](../src/models/pec_model.cpp)

[`performFFT()`](../src/models/pec_model.cpp:72) sumuje `sin/cos` po **całym oknie 3 cykli** z okresem bazowym `T = worm_cycle_seconds` (1 cykl). Sumowanie `∑ₙ sin(2πk·tₙ/T)` po 3 okresach daje spektralny wyciek — harmoniczne niezgodne z liczbą całkowitą cykli w oknie (k∤3) mają zaniżone amplitudy (istotne dla k=1,2,4,5…). Prawidłowy DFT powinien użyć okresu okna (3T) lub wykryć fazę względem 1 cyklu. Wada jakościowa (dokładność PEC), nie niestabilność.

### 5.6. Efemerydy — [`ephemeris_tracker.cpp`](../src/models/ephemeris_tracker.cpp)

Bardzo dobre praktyki numeryczne:
- **barycentryczna interpolacja Lagrange'a** z prekomputowanymi wagami (O(n), odporna na kasowanie) — [ephemeris_tracker.cpp:300](../src/models/ephemeris_tracker.cpp:300);
- klamp czasu do zakresu efemeryd (brak katastrofalnej ekstrapolacji) — [ephemeris_tracker.cpp:139](../src/models/ephemeris_tracker.cpp:139);
- ochrona mianowników i spadek do interpolacji liniowej przy degeneracji — [ephemeris_tracker.cpp:379](../src/models/ephemeris_tracker.cpp:379);
- ekstrapolacja kwadratowa z przyspieszeniem i clampami prędkości — [ephemeris_tracker.cpp:230](../src/models/ephemeris_tracker.cpp:230).

Drobne uwagi:
- czasy jako `double` sekund od epoki Unix (~1.7e9) mają rozdzielczość ~2.4e-7 s (ULP) — wystarczająca dla efemeryd;
- wagi barycentryczne dla dużej liczby węzłów i szerokiego zakresu czasu mogą underflowować do 0 (strażnik `1e-300` obsługuje to, spadek do interpolacji liniowej).

---

## 6. Zagrożenia stabilności systemu (poza numeryką stricte)

1. **Stuby i niezaimplementowane ścieżki** — nie są błędami numerycznymi, ale oznaczają brak poprawności funkcjonalnej:
   - [`st4_calibration.cpp`](../src/controllers/st4_calibration.cpp) — tylko komentarze (brak pomiaru arcsec/ms);
   - [`St4Guider::calibrate()`](../src/controllers/st4_guider.cpp:27) — bez rzeczywistej kalibracji kierunków;
   - [`derotator_tmc5160.cpp`](../src/hal/derotator_hal/derotator_tmc5160.cpp) — pusty;
   - [`FieldRotationModel::calculateCasual()`](../src/models/field_rotation_model.cpp:23) — **ignoruje kwaternion** i zwraca wyniki jak dla Alt-Az (oznaczone „Simplified model”);
   - [`DerotatorController::getFieldRotation()`](../src/controllers/derotator_controller.cpp:18) — zwraca `{}`.
2. **Konwencje kwaternionów** — dwa różne układy elementów: `[qx,qy,qz,qw]` (astro_calc, [`rotateVectorByQuaternion`](../src/core/astronomical_calculations.cpp:448)) oraz `[qw,qx,qy,qz]` w [`KalmanFilter::updateStateTransitionMatrix`](../src/models/kalman_filter.cpp:829). Brak jednolitej dokumentacji konwencji zwiększa ryzyko błędu przy integracji EKF z resztą systemu.
3. **Degeneracja kolumn w TPoint** — gdy włączone są jednocześnie `WORM_ERROR` (baza `sin(1·ha)`) i `POLAR_AZ` (`sin(ha)`), kolumny są **identyczne** → macierz rang-deficient, niestabilne/nieidentyfikowalne parametry mimo QR. ([tpoint_model.cpp:691](../src/models/tpoint_model.cpp:691), [tpoint_model.cpp:656](../src/models/tpoint_model.cpp:656)) — warto wykrywać i wyłączać kolizyjne kolumny (podobnie jak już zrobiono dla POLAR_ALT/COLLIMATION).
4. **`getMetrics()` w EphemerisTrackerManager** — uśrednianie `total_error_sum / total_tracks_` po trackerach o różnym czasie trwania; metryki zbiorcze mogą być mylące ([ephemeris_tracker.cpp:1707](../src/models/ephemeris_tracker.cpp:1707)).

---

## 7. Podsumowanie — co robić

### Poprawki wysokiego priorytetu (błędy poprawności)
1. **[B1]** `equatorialToHorizontal`/`horizontalToEquatorial`: `lon * D2R / 15.0` → `lon * D2R`.
2. **[B2]** `calculateGMST`: nie dodawać ΔAT do UT1 (użyć `jd` lub poprawnie DUT1).
3. **[B3]** `tpoint_model::predictMountPosition`: usunąć błędne podziały kroku Newtona (lub ujednolicić jednostki Jacobianu).

### Poprawki średniego priorytetu
4. **[B4]** `applyAtmosphericRefraction`: jedna korekcja temperatury, usunąć `1.33322`.
5. **[B5]** `focus_curve::fitParabolic`: poprawny Cramer (lub lepiej QR na danych odśrodkowanych).
6. **[B6]** `applyProperMotion`: clamp `cos(dec)` przy biegunach.
7. **[R1/R5]** KF: jawna kontrola dodatniej określoności S (pivots LDLT); naprawić/ukryć metrykę `filter_stability`; ograniczyć wzrost poza-diagonalnych Q.

### Poprawki niskiego priorytetu / porządkowe
8. **[B7]** `field_rotation_model`: dodać osłony osobliwości.
9. **[B8]** `exposure_planner::calculateExposureTime`: poprawić wymiarowo wzór.
10. **[R6]** podawać do `PositionKalmanFilter` surowy odczyt enkodera zamiast pozycji już zaktualizowanej kinematycznie.
11. **[R7]** `evaluateSoftLimits`: `std::fmod` zamiast pętli `while` dla dużych pozycji.
12. Zaimplementować stuby (ST4, derotator, `calculateCasual`) lub oznaczyć jako niezaimplementowane na poziomie API.
13. Ujednolicić konwencję układu elementów kwaternionu (`[w,x,y,z]` vs `[x,y,z,w]`).

---

## 8. Metodologia i zakres

Analizę wykonano na podstawie lektury plików:
- [`src/core/astronomical_calculations.cpp`](../src/core/astronomical_calculations.cpp)
- [`src/models/kalman_filter.cpp`](../src/models/kalman_filter.cpp)
- [`src/models/tpoint_model.cpp`](../src/models/tpoint_model.cpp)
- [`src/models/pec_model.cpp`](../src/models/pec_model.cpp)
- [`src/models/field_rotation_model.cpp`](../src/models/field_rotation_model.cpp)
- [`src/models/ephemeris_tracker.cpp`](../src/models/ephemeris_tracker.cpp)
- [`src/models/exposure_planner.cpp`](../src/models/exposure_planner.cpp)
- [`src/models/focus_curve.cpp`](../src/models/focus_curve.cpp)
- [`src/controllers/mount_controller.cpp`](../src/controllers/mount_controller.cpp) (kluczowe sekcje: slewy, tracking, ALT_AZ/CASUAL, guider, soft limits, meridian flip)
- [`src/controllers/derotator_controller.cpp`](../src/controllers/derotator_controller.cpp)
- [`src/controllers/st4_guider.cpp`](../src/controllers/st4_guider.cpp), [`src/controllers/st4_calibration.cpp`](../src/controllers/st4_calibration.cpp)
- [`src/hal/simulated_hal/simulated_hal.cpp`](../src/hal/simulated_hal/simulated_hal.cpp), [`src/hal/derotator_hal/derotator_tmc5160.cpp`](../src/hal/derotator_hal/derotator_tmc5160.cpp)

**Zakres:** stabilność (pętle sterowania, akumulacja, dryf, propagacja NaN), stabilność numeryczna (uwarunkowanie macierzy, dekompozycje, osobliwości, kasowanie cyfrowe) oraz poprawność numeryczna (jednostki, wzory, konwencje, zakresy).

**Poza zakresem:** bezpieczeństwo wątków poza wątkami stricte obliczeniowymi (częściowo omówione przy pętli trackingu), poprawność logiczna logiki stanów montażu (FLIP/SLEW/TRACK), poprawność danych katalogowych (web/data/catalogs).
