# Analiza refaktoryzacji kontrolera montażu
## Obniżenie złożoności i podniesienie stabilności

**Data:** 2026-09-28
**Główny plik:** [`src/controllers/mount_controller.cpp`](src/controllers/mount_controller.cpp) (8581 linii)

Dokument identyfikuje miejsca, w których refaktoryzacja daje największy zwrot
w postaci **niższej złożoności** i **wyższej stabilności**. Analiza opiera się
na pomiarach (liczby linii, liczba zdublowanych wzorców, liczba pól klasy) oraz
na błędach naprawionych w tym pliku w przeszłości (sekcje 6/6a w
[`analiza_trackingu_kontroler_indi.md`](docs/pl/analiza_trackingu_kontroler_indi.md)).

## Status wdrożenia (po refaktorze)

Wykonane fazy (każda zweryfikowana kompilacją `astro_mount_core`):

- **P1 — helpery jednostek:** `haGear()`/`decGear()`; usunięte 23 powtórzenia
  fallbacku `gear_ratio > 0.0 ? … : 360.0`.
- **P6 — pola zamiast `static`:** 12 lokalnych `static` (liczniki logów,
  debounce gamepada) przeniesionych do pól `Impl`; dodatkowo usunięty efekt
  „pierwsze naciśnięcie było połykane" w debounce przycisków gamepada.
- **P2 — `computeSoftLimits()`:** czysta, testowalna funkcja
  `computeSoftLimits()` + struktura `SoftLimitEvaluation`; `evaluateSoftLimits()`
  stało się cienką warstwą (konwersja servo→teleskop + zapis pól).
- **P4 — podział pętli śledzenia:** wyodrębnione `computeAltAzRates()`,
  `computeCasualRates()` (obliczanie prędkości), `applyEquatorialCorrections()`
  (nutacja/TPoint/refrakcja) oraz `trackingModeFactor()` (współczynnik trybu).
  Pętla śledzenia skurczyła się o ~430 linii.
- **P5 — `runSlewMonitor()`:** wspólny monitor slew (watchdog, `targetReached`,
  weryfikacja, retry, timeout, finalizacja) wyodrębniony z `slewToEquatorial`
  i `slewToHorizontal` — usunięte ~240 linii duplikacji.
- **P7 — `computeFlipTargets()`:** wspólne wyliczanie celów flipa meridianowego
  (multi-turn-safe) używane przez flip automatyczny i ręczny
  (`executeMeridianFlip`); przy okazji naprawiony duplikat błędu #9 w ścieżce
  ręcznej.

Plik zmniejszył się z 8581 do 8309 linii pomimo dodania 8 nazwanych funkcji —
efekt konsolidacji duplikatów. Pełne rozbicie `Impl` na osobne pliki/komponenty
to nadal przyszła praca (higiena strukturalna), ale największe duplikacje i
ukryte stany zostały wyeliminowane.

---

## 1. Metryki złożoności

| Metryka | Wartość | Ryzyko |
|---|---|---|
| [`mount_controller.cpp`](src/controllers/mount_controller.cpp) | 8581 linii | bardzo wysokie |
| Klasa `MountController::Impl` (całość w `.cpp`) | ~8150 linii | god class |
| Pętla śledzenia (lambda w `startTracking`) | ~1400 linii | jedna funkcja robi wszystko |
| Pola prywatne `Impl` | 100+ | wysoka złożoność stanu |
| Wzorzec `gear_ratio > 0.0 ? … : 360.0` | 23 wystąpienia | duplikacja konwersji jednostek |
| Walidacja soft limitów (cel przed ruchem) | 3 kopie | ryzyko rozjazdu |
| Pętle slew (equatorial / horizontal) | 2 kopie | duplikacja ~400 linii |
| Poprawki astronomiczne (equatorial vs alt-az/casual) | 2 kopie | duplikacja ~150 linii |
| Obliczanie prędkości śledzenia (ALT_AZ vs CASUAL) | 2 kopie | duplikacja ~100 linii |
| `static` zmienne lokalne wewnątrz metod | 12 wystąpień | ukryty stan, problem z wielowątkowością |

---

## 2. Główne ogniska złożoności

### 2.1 „God class" — `MountController::Impl`

Publiczny interfejs [`MountController`](include/controllers/mount_controller.h)
jest już pimpl-em (delegujące metody zaczynają się od linii 8166), ale cała
logika siedzi w jednej klasie `Impl` liczącej ~8150 linii. Klasa jednocześnie
odpowiada za:

- maszynę stanów (`SLEWING` / `TRACKING` / `MERIDIAN_FLIP` / …),
- pętlę śledzenia (integracja pozycji, poprawki, motor I/O),
- obrót przez meridian,
- soft limity i strefę hamowania,
- konwersje współrzędnych i jednostek,
- kalibrację TPoint/bootstrap,
- gamepad, LX200, ephemeris, guider, HAL status, stan/konfigurację.

**Skutek dla stabilności:** każda zmiana dotyka jednego pliku z setkami pól;
trudno utrzymać spójność między ścieżkami (np. poprawka multi-turn flipa musiała
uwzględniać konwencję `home_offset` i `raw_servo` jednocześnie — patrz błąd #9).

### 2.2 Monolityczna pętla śledzenia

Lambda uruchamiana w `work_thread_` (od ~linii 1906) zawiera sekwencyjnie:

1. odczyt bezpieczeństwa HAL,
2. odczyt sensorów środowiskowych,
3. soft limity (`evaluateSoftLimits`),
4. integrację pozycji + Kalman,
5. normalizację ALT_AZ/CASUAL,
6. poprawki astronomiczne (nutacja/TPoint/refrakcja) — dla 3 typów montaży,
7. obliczanie prędkości (ALT_AZ / CASUAL),
8. detekcję i wykonanie meridian flip,
9. snapshoty + I/O silników (position mode / velocity mode / drift correction).

To jest ~1400 linii w jednej funkcji lambda z kilkoma poziomami zagnieżdżenia.
**Skutek:** błędy takie jak „uncontrolled rotation" (przypadek #9) żyją w jednej
zagnieżdżonej gałęzi i są trudne do wyizolowania testem.

### 2.3 Zduplikowane bloki

Zmierzone duplikacje (konkretne numery linii po ostatnich zmianach):

1. **Konwersja servo↔teleskop** — 23× wzorzec
   `config_.mount_config.<axis>_params.gear_ratio > 0.0 ? gear_ratio : 360.0`
   (np. [`mount_controller.cpp:728`](src/controllers/mount_controller.cpp:728),
   [`mount_controller.cpp:1690`](src/controllers/mount_controller.cpp:1690),
   [`mount_controller.cpp:2221`](src/controllers/mount_controller.cpp:2221)).
   Ten sam fallback powtórzony 23 razy to 23 miejsca, w których można pomylić
   `ha_gear` z `dec_gear` (historycznie zdarzył się błąd `axis2 / ha_gear`).

2. **Walidacja soft limitów przed ruchem** — 3 kopie:
   - pętla slew equatorial (≈[`mount_controller.cpp:727`](src/controllers/mount_controller.cpp:727)),
   - pętla slew horizontal (≈[`mount_controller.cpp:1197`](src/controllers/mount_controller.cpp:1197)),
   - `startTracking` (≈[`mount_controller.cpp:1688`](src/controllers/mount_controller.cpp:1688)).

3. **Pętle slew** — `slewToEquatorial` i `slewToHorizontal` powielają
   watchdog, weryfikację `targetReached`, retry, timeout i finalizację stanu.

4. **Poprawki astronomiczne** — ścieżka EQUATORIAL (nutacja/TPoint/refrakcja)
   i ścieżka ALT_AZ/CASUAL powtarzają tę samą sekwencję w innej ramce.

5. **Obliczanie prędkości** — blok ALT_AZ i blok CASUAL powtarzają `omega`,
   `mode_factor`, guardy `cos(lat)`/`cos(alt)`, konwersję rad→deg→servo.

### 2.4 `static` zmienne lokalne

12 wystąpień (m.in. [`mount_controller.cpp:3052`](src/controllers/mount_controller.cpp:3052),
[`mount_controller.cpp:3184`](src/controllers/mount_controller.cpp:3184),
[`mount_controller.cpp:3278`](src/controllers/mount_controller.cpp:3278),
[`mount_controller.cpp:4126`](src/controllers/mount_controller.cpp:4126)).
Służą jako liczniki logowania i debounce przycisków gamepada. Problem: stan
żyje globalnie (dzielony między instancjami), nie resetuje się między sesjami
i jest niewidoczny w klasie — utrudnia testowanie i diagnozę.

### 2.5 Magiczne liczby i konwencje jednostek

Wielokrotnie powtarzane: `15.0` (godziny→stopnie), `360.0` (obrót), `0.004178`
(stopa gwiazdowa), `1e-9` (tolerancje), `89.5°` (guard zenitu), `0.1°` (guard
bieguna). Bez typów jednostek (`ServoDegrees`, `TelescopeDegrees`, `Hours`)
kompilator nie wyłapie pomyłek, które historycznie powodowały błędy A1/A3/C2
(z wcześniejszej analizy śledzenia).

---

## 3. Propozycje refaktoryzacji (priorytetowo)

### P1 — Typy jednostek i helpery konwersji (niskie ryzyko, duży zysk)

Wprowadź lekkie typy/helpery:

```cpp
struct ServoDegrees  { double value; };
struct TelescopeDegrees { double value; };
struct Hours { double value; };

double haGear()   const { return gear(ha_axis_params.gear_ratio); }
double decGear()  const { return gear(dec_axis_params.gear_ratio); }
TelescopeDegrees servoToTel(double servo, double gear);
ServoDegrees     telToServo(double tel, double gear, double home_offset);
```

Usuwa 23 powtórzenia fallbacku i całą klasę błędów „servo zamiast teleskopu".
To jest **zerozmianowy** refaktor (czysta mechanika), łatwy do przetestowania.

### P2 — `SoftLimitEvaluator` (wyodrębnij `evaluateSoftLimits`)

Funkcja `evaluateSoftLimits` (≈[`mount_controller.cpp:7257`](src/controllers/mount_controller.cpp:7257))
miesza: konwersję jednostek, fold HA/Dec, liczenie dystansów, strefę
warning/decel, budowanie komunikatu i obliczanie współczynnika skali.
Wyodrębnij:

```cpp
struct SoftLimitResult {
    double distance_axis1, distance_axis2;
    bool warning, deceleration;
    double rate_factor;
    std::string message;
};
class SoftLimitEvaluator {
    SoftLimitResult evaluate(ServoDegrees a1, ServoDegrees a2) const;
};
```

Do tego jedna wspólna metoda `validateTargetLimits()` zastąpi 3 kopie walidacji
przed ruchem (sekcja 2.3 p.2).

**Efekt:** jedna implementacja limitów zamiast trzech; łatwe unit testy granic
(histereza folda, zona decel).

### P3 — `MeridianFlipController` (wyodrębnij logikę flipa)

Logika flipa (detekcja, kontrola wykonalności, cele, wykonanie, zakończenie)
jest wtopiona w pętlę śledzenia (≈[`mount_controller.cpp:2837`](src/controllers/mount_controller.cpp:2837)–3060).
Wyodrębnij do osobnej klasy z jawnymi wejściami/wyjściami:

```cpp
class MeridianFlipController {
    FlipDecision evaluate(double ha, ServoDegrees dec, ...);
    FlipTargets computeTargets(ServoDegrees a1, ServoDegrees a2, ...);
    bool completeIfReached(...);
};
```

To miejsce wygenerowało błędy #9 i #10 — wyodrębnienie pozwoli objąć je
testami jednostkowymi z pozycjami multi-turn (np. `axis2=5311889°`).

### P4 — `TrackingEngine` (podziel pętlę śledzenia)

Podziel lambdę ~1400 linii na nazwane kroki (komentarze „I/O Block 1/2/3" już
wyznaczają granice):

- `trackingIterationOnce(dt)` — orkiestrator,
- `applyAstronomicalCorrections(...)` — jedna funkcja dla wszystkich typów
  montaży (parametryzowana ramką),
- `computeRates(...)` — wspólna dla ALT_AZ/CASUAL z parametrem konwersji,
- `sendMotorTargets(...)` — position/velocity mode + drift correction.

Dodatkowo zunifikuj poprawki astronomiczne EQUATORIAL i ALT_AZ/CASUAL: obie
wykonują „przelicz na równikowe → nutacja → TPoint → refrakcja → przelicz z
powrotem", różnią się tylko transformacją ramki. Można to zapisać jako jeden
algorytm z funktorem konwersji.

### P5 — Wspólny `SlewExecutor` (zunifikuj pętle slew)

`slewToEquatorial` i `slewToHorizontal` dzielą ~80% logiki (join wątku,
`refreshPositionsFromHAL`, watchdog, `targetReached` + retry, timeout, finalny
stan). Wyodrębnij szablon metody:

```cpp
bool executeSlew(ServoDegrees target1, ServoDegrees target2,
                 const SlewProfile& profile);
```

Konkretne metody zostawiają tylko przeliczenie współrzędnych na cele servo.

### P6 — Zamień `static` lokalne na pola stanu

Przenieś 12 `static` liczników/debounce'ów do pól `Impl` (np.
`flip_log_counter_`, `pos_mode_logged_`, `refresh_log_counter_`,
`button_debounce_*`). Daje to:
- reset stanu między sesjami,
- bezpieczeństwo przy wielu instancjach,
- możliwość testowania.

### P7 — Podział `Impl` na komponenty (docelowo)

Po wykonaniu P1–P6 naturalny podział na:

- `TrackingEngine` (pętla, poprawki, prędkości),
- `MeridianFlipController`,
- `SoftLimitEvaluator`,
- `SlewExecutor`,
- `CoordinateTools` (jednostki, fold, LST),
- `CalibrationService` (TPoint/bootstrap) — obecnie w
  [`src/models/tpoint_model.cpp`](src/models/tpoint_model.cpp:1263) i w `Impl`.

`Impl` stanie się cienkim koordynatorem (stan + delegacja), a każdy komponent
będzie testowalny w izolacji.

---

## 4. Mapowanie refaktoryzacji na stabilność

| Refaktor | Który historyczny błąd by utrudnił/wyeliminował |
|---|---|
| P1 typy jednostek | A1/A3/C2 (servo w funkcjach trygonometrycznych), pomyłki `ha_gear`/`dec_gear` |
| P2 SoftLimitEvaluator | „Hard limit exceeded" / fałszywe decel przy pozycjach multi-turn |
| P3 MeridianFlipController | #9 (multi-turn cele flipa), #10 (flip blisko bieguna) |
| P4 TrackingEngine | E1 (kumulacja poprawek), niekontrolowany obrót |
| P5 SlewExecutor | #7 (cel HA o pełny obrót), timeouty slew |
| P6 pola zamiast static | ukryty stan, diagnoza sesji |

Kluczowa zasada: **testowalność = stabilność**. Obecnie logika pętli śledzenia
i flipa jest nieosiągalna dla unit testów bez pełnego HAL-a. Wyodrębnienie
P2–P4 umożliwia testy regresyjne z danymi liczbowymi z logów (np. przypadek
`axis2=5311889°` z raportu błędu).

---

## 5. Plan wdrożenia (niskie ryzyko, inkrementalnie)

Każda faza kończy się kompilacją (`cmake --build build --target astro_mount_core`)
i ewentualnym uruchomieniem testów. Kolejność od najbezpieczniejszej:

1. **Faza 0 — P1**: helpery jednostek + zamiana 23 fallbacków. Czysta mechanika,
   zero zmian zachowania. Natychmiastowa redukcja szumu.
2. **Faza 1 — P2**: `SoftLimitEvaluator` + unifikacja walidacji celów (3 kopie → 1).
3. **Faza 2 — P6**: przeniesienie `static` lokalnych do pól.
4. **Faza 3 — P3**: `MeridianFlipController` + testy regresyjne multi-turn.
5. **Faza 4 — P4**: podział pętli śledzenia na kroki, unifikacja poprawek i
   prędkości.
6. **Faza 5 — P5**: wspólny `SlewExecutor`.
7. **Faza 6 — P7**: docelowy podział `Impl` na komponenty.

---

## 6. Ryzyka i mitygacja

| Ryzyko | Mitygacja |
|---|---|
| Refaktor zmienia zachowanie numeryczne | Faza 0 jest czysto mechaniczna; testy round-trip (`test_mount_coordinates`) + testy porównawcze logów |
| Wyodrębnienie pętli rozbija dostęp do 100 pól | Komponenty dostają jawne wejścia/wyjścia (struktury), a nie wskaźnik na `Impl` |
| Regresja flipa/soft limitów | Testy jednostkowe P2/P3 z danymi z realnych logów (multi-turn) |
| Duży plik trudny do review | Każda faza to osobny, mały commit |

Rekomendacja: zacząć od **Fazy 0 (P1)** — najniższe ryzyko, natychmiastowa
redukcja 23 powtórzeń i przygotowanie gruntu pod resztę.
