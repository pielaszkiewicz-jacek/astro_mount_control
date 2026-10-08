# Analiza pętli śledzenia — poprawność obliczeń i stabilność

Dokument analizuje pętlę śledzenia w [`src/controllers/mount_controller.cpp`](../src/controllers/mount_controller.cpp)
oraz jej interakcję z kontrolerami osi CANopen (napędy STM42M/STMP42SXI w trybie CiA 402 *Profile Position*).
Kontekst: [`config/canopen_stm42m.json`](../config/canopen_stm42m.json) (`type: equatorial`,
`gear_ratio: 360`, `encoder_counts_per_degree: 364.08888889 = 131072/360`,
`split_tracking_loop: true`, `calc_update_hz: 50`, `io_update_hz: 200`,
`tracking_pos_update_hz: 10`, `tracking_pos_lead_seconds: 3.0`,
`tracking_pos_velocity_factor: 1.0`).

---

## 1. Architektura pętli

Pętla śledzenia składa się z trzech wątków (tryb *split loop*):

| Wątek | Częstotliwość | Rola |
|---|---|---|
| **calc** (`work_thread_`) | `calc_update_hz` = 50 Hz (20 ms) | całkowanie pozycji, filtr Kalmana, korekcje (nutacja/TPoint/refrakcja), limity miękkie, flip południkowy, **obliczanie nowych celów pozycyjnych** |
| **io reader** (`trackingIoReaderLoop`) | 200 Hz | zbieranie telemetrii (pozycje/prędkości z cache PDO, stan safety, czujniki) do `TrackingTelemetry` |
| **io writer** (`trackingIoWriterLoop`) | 200 Hz | emisja najnowszego `TrackingCommand` do napędów |

Tryb legacy (pojedyncza pętla, `tracking_update_ms` = 5 ms) robi to samo w jednym wątku.

Cadence aktualizacji pozycji: `POS_UPDATE_INTERVAL = calc_hz / pos_update_hz = 50/10 = 5`
iteracji = **100 ms**. W trybie pozycyjnym cel jest wysyłany raz na 100 ms; pomiędzy
aktualizacjami napęd realizuje swój wewnętrzny profil trapezu do ostatniego celu.

Dla EQUATORIAL są dwa tryby śledzenia:

- **pozycyjny** (domyślny, `equatorial_tracking_velocity_mode=false`) — co 100 ms wysyłany
  jest nowy absolutny cel pozycji (`setPosition` → 607Ah),
- **prędkościowy** (eksperymentalny) — wysyłane jest `setVelocity` (60FFh) z korekcją dryfu.

ALT_AZ / CASUAL używają wyłącznie trybu prędkościowego z prędkościami zależnymi od pozycji.

---

## 2. Układy odniesienia i jednostki (poprawność)

W całym kontrolerze obowiązują trzy spójne układy:

```
stopnie teleskopu  ──× gear_ratio(360)──▶  stopnie silnika („servo”)  ──× cpd(364.0889)──▶  licznik napędu (counts)
```

- `home_offset_axisN` mapuje **licznik napędu** (stopnie silnika) na pozycję
  „po Home": `reported = counter + home_offset`.
- Cel w ramie napędu: `target_motor = telescope_deg × gear − home_offset`.
- `raw_servo_axisN = counter + home_offset` — pozycja raportowana (getStatus).
- `axisN_position_` — wewnętrzne, scałkowane oszacowanie pozycji (ramka home-offset),
  w trakcie śledzenia własność wątku calc.

**Weryfikacja poprawności ram:** cel jest zawsze przeliczany do ramy licznika napędu
(odejmując `home_offset`), a `ref1/ref2` (kotwica fizyczna) to `getActualPosition()`
albo `raw_servo − home_offset` — czyli ten sam licznik. Jednostki są spójne wzdłuż
całej ścieżki `startTracking → pętla → refreshPositionsFromHAL → HAL → napęd`.

Miejsce konwersji do counts jest jedno — [`CanOpenHAL::CanOpenMotor::setPosition()`](../src/hal/canopen_hal/canopen_hal.cpp:57):

```cpp
const int32_t target = static_cast<int32_t>(position_deg * cpd);   // motor deg → counts
const uint32_t vel   = degPerSecToCounts(velocity_deg_s, cpd);     // motor deg/s → counts/s
```

---

## 3. Tryb pozycyjny — oś RA (krok po kroku)

Cel RA w każdej aktualizacji pozycyjnej (100 ms):

1. `ha_hours = LST − snap_target_ra + lead`, gdzie
   `lead = POS_LEAD_SECONDS × SIDEREAL_HOURS_PER_SEC` (3 s ≈ 0,000836 h ≈ 4,5° silnika).
2. Kotwiczenie do **fizycznej** pozycji napędu:
   `current_ha_hours = raw_servo_axis1 / (gear × 15)`,
   `ha_hours = current_ha_hours + najkrótsza_delta`.
3. `new_axis1_target = ha_hours × 15 × gear − home_offset_axis1`.
4. Re-wrap o **całkowite obroty** do okna licznika napędu (`ref1`).
5. `pos_vel = sidereal × pos_vel_factor`; jeśli `|target − ref1| > close1` (0,4° teleskopu)
   → `pos_vel = max_slew_rate` (8000°/s silnika).
6. `publishPositionCommand(...)`.

**Poprawność matematyczna:** gdy napęd jest w HA celu,
`new_axis1_target − ref1 = lead` (cel jest dokładnie `lead` przed napędem).
Napęd porusza się z `pos_vel = sidereal`, cel „ucieka" z tą samą prędkością sideralną,
więc odstęp pozostaje stały — napęd **nie dogania celu** i płynie jednostajnie, bez
przestojów. To jest zamierzone, stabilne zachowanie (feed-forward + lead).

**Warunki stabilności (wrażliwości):**

- `tracking_pos_velocity_factor > 1` → napęd szybszy niż sideralny → dogania cel →
  zatrzymanie → piłokształt. Przy `1.0` — stabilnie.
- Rzeczywista prędkość napędu większa niż zadany profil (błąd pętli prędkości napędu)
  → ten sam piłokształt.
- Dodany wcześniej człon proporcjonalny (`pos_vel = sidereal + Kp×(target−actual−lead)`
  z przycinaniem do 0) **powodował dokładnie ten piłokształt** — przy dojechaniu
  napędu do celu prędkość była przycinana do zera. Został cofnięty do feed-forward.

---

## 4. Tryb pozycyjny — oś Dec

- `new_axis2_target = axis2_target_` (stały cel wyliczony raz w `startTracking`
  z `resolveDecTarget`, więc zawsze po właściwej stronie pirsowania).
- Re-wrap o całkowite obroty Dec (`360° × dec_gear`) do okna fizycznego napędu.
- `pos_vel` jest **wspólny** dla obu osi.

Dec jest nieruchoma w śledzeniu sideralnym, więc stały cel jest poprawny. Podczas
startowego slewa Dec dostaje `max_slew_rate` (mały ruch, bez znaczenia), w śledzeniu
ustalonym dostaje `sidereal` (cel osiągnięty → brak ruchu).

**Ryzyka:**

- Uszkodzony `home_offset_axis2` → cel daleko od napędu → walidacja w `startTracking`
  odrzuca („Dec-axis position desync — re-home required") lub re-wrap trzyma go
  w oknie ±180° teleskopu.
- Guard desyncu (`dec_desync_count_ > 30`) zatrzymuje śledzenie, zamiast pozwolić
  osi Dec kręcić się w nieskończoność.

---

## 5. Odczyt pozycji i „rozdwojenie" pozycji

Dwie zmienne pozycji pełnią różne role:

| Zmienna | Źródło | Użycie |
|---|---|---|
| `raw_servo_axisN` | fizyczny napęd (`getActualPosition` + `home_offset`), aktualizowana przez `refreshPositionsFromHAL` (pętla główna) | kotwica celu RA/Dec, raport do getStatus |
| `axisN_position_` | całkowanie `rate×dt` + KF + korekcje (wątek calc) | limity miękkie, korekcje, HA do flipa |

Podział jest celowy: cel jest kotwiczony do pozycji fizycznej (bez lead-a), a
oszacowanie wewnętrzne nie jest nadpisywane w trakcie śledzenia (unika to oscylacji
„sawtooth ~20°" opisanej w kodzie). Rozbieżność obu pozycji jest ograniczona
wielkością korekcji (nutacja/TPoint/refrakcja — sekundy łuku).

**Opóźnienie odczytu:** `getActualPosition()` zwraca wartość z cache TPDO1 (6064h),
odświeżaną co ~10 ms przez wątek monitora HAL — opóźnienie ~10–20 ms, przy
1,5°/s silnika to ~0,03° — pomijalne.

---

## 6. Filtr Kalmana

`predict(dt)` z wstrzykniętymi prędkościami, potem `update(axisN_position_)`, gdzie
pomiar jest pozycją **po** zaawansowaniu kinematycznym. Innowacja ≈ 0, więc KF jest
praktycznie przepustką („near-passthrough smoother"). Nie podwaja prędkości, nie
stabilizuje niczego istotnego, ale też nie szkodzi. Kandydat do usunięcia lub
przestrojenia (uwaga w kodzie wskazuje, że docelowo to KF powinien być autorytatywnym
integratorem na surowym odczycie enkodera).

---

## 7. Interakcja z kontrolerami osi (CANopen CiA 402)

Ścieżka pozycji: `setPosition(motor_deg, vel, accel)` → `setPositionTargetPdo`:

1. Jeśli PDO fast-path aktywny: przełącz tryb na Profile Position,
2. jeśli profil vel/accel się zmienił — zapis SDO (6081h/6083h/6084h),
3. wyślij dwie ramki RPDO2 (6040h + 607Ah): pierwsza `Enable Operation`,
   druga `New Setpoint` (narastające zbocze bitu 4) — *fire-and-forget*, bez ACK.
4. Jeśli mapowanie PDO nie zostało zaakceptowane (`pdo_configured_ == false`) —
   fallback na ścieżkę SDO.

**Konsekwencje dla stabilności:**

- RPDO2 to zapis „rzuć i zapomnij": kontroler **nie potwierdza**, że napęd przyjął
  nowy cel. Stąd w kodzie kluczowe jest, by mapowanie 0x1601 faktycznie wskazywało
  607Ah (poprawka w `configurePdoVelocityMapping`) — inaczej bajty celu lądują w
  obiekcie prędkości i napęd jedzie ze stałą prędkością.
- `int32_t target` przepełnia się dopiero po ~2,1 mld counts (≈136 nocy śledzenia
  sideralnego); **rewind** (poniżej) trzyma licznik daleko od granicy.
- Napęd realizuje profil trapezu i **zatrzymuje się na celu**. To źródło wymogu
  lead-a: bez lead-a napęd dojeżdża do celu wcześniej niż następna aktualizacja
  i występuje piłokształt.

---

## 8. Position rewind (przewijanie liczników)

`maybeRewindDriveCounters()` (wywoływane z `refreshPositionsFromHAL`) zeruje licznik,
gdy `|counter_motor_deg| × cpd ≥ 80% × 1 000 000 counts`, tylko gdy montaż jest IDLE.

**Przeliczenie progu:** `800 000 counts ÷ 364,0889 counts/° = 2197° silnika = 6,1°
teleskopu`. Oznacza to, że próg jest **bardzo niski** — rewind odpala się już po
~6° skumulowanego ruchu osi (np. ~24 min śledzenia sideralnego osi RA, albo po
jednym większym slewie). Po zerowaniu licznik zaczyna od ~0, więc rewind powtarza
się co ~6° ruchu w każdej sesji.

**Poprawność:** zerowanie + przesunięcie `home_offset += pos_drive`,
`axis_target −= pos_drive` jest spójne ramowo (raportowana pozycja i cele pozostają
w miejscu), pod warunkiem że:

- zero faktycznie zadziałało (weryfikacja `|after| < 360°`),
- pomiędzy odczytem licznika a przesunięciem ramki nie wystartował slew/track
  (wyścig — zamknięty przez wykonanie całej sekwencji pod wyłącznym `state_mutex_`).

**Rekomendacja:** próg 80% z 1M counts jest najpewniej skalibrowany względem
`counts/stopień teleskopu`, a nie `counts/stopień silnika` (364,0889). Podniesienie
progu (np. `1 000 000 counts` przy liczniku 32-bit mieszczącym 2,1 mld) zmniejszy
częstotliwość przewijań i ryzyko, nie zbliżając się do przepełnienia.

---

## 9. Limity miękkie i korekcje astronomiczne

- `evaluateSoftLimits()` liczy strefy hamowania z `axisN_position_` (oszacowanie
  wewnętrzne) i skaluje `rate_factor` — w trybie pozycyjnym działa na prędkość
  profilu, nie na sam cel.
- `applyEquatorialCorrections()` (nutacja/TPoint/refrakcja) modyfikuje **tylko**
  `axisN_position_` jako delty (E1 fix). **W trybie pozycyjnym te korekcje nie
  trafiają do komendy celu** (`new_axis2_target = axis2_target_`), więc są one
  de facto kosmetyczne (poprawiają raportowaną pozycję i limity), a nie faktyczne
  wskazanie na niebie. To znane ograniczenie — dla subsekundowej precyzji cel Dec
  powinien być korygowany wprost.

---

## 10. Podsumowanie ryzyk i rekomendacje

| # | Obszar | Ocena | Uwaga / rekomendacja |
|---|---|---|---|
| 1 | Ramy i jednostki | **poprawne** | spójne telescope→motor→counts, `home_offset` poprawnie odejmowany od celów |
| 2 | Cel RA + lead | **poprawne** | feed-forward stabilny przy `pos_vel_factor ≤ 1` |
| 3 | Prędkość pozycyjna | **poprawiona** | cofnięto człon P (piłokształt); ewentualnie dodać P z deadbandem, bez przycinania do 0 |
| 4 | Oś Dec | **poprawna** | stały cel + re-wrap + guard desyncu |
| 5 | KF | **neutralny** | przepustka; kandydat do usunięcia/przestrojenia |
| 6 | Rewind | **poprawny, ale zbyt częsty** | próg 800k counts = 6,1° teleskopu; podnieść próg |
| 7 | Korekcje w trybie pozycyjnym | **ograniczenie** | TPoint/refrakcja nie trafiają do komendy celu Dec |
| 8 | PDO fire-and-forget | **ryzyko ukryte** | brak potwierdzenia przyjęcia celu; pilnować poprawnego mapowania 0x1601 |

Kluczowy wniosek: po cofnięciu pętli zamkniętej prędkości pętla pozycyjna jest
obliczeniowo poprawna i stabilna w warunkach nominalnych. Pozostałe ryzyka to
(1) zbyt agresywny próg rewindu, (2) brak aplikowania korekcji TPoint/refrakcji do
celu w trybie pozycyjnym oraz (3) czysto kosmetyczny filtr Kalmana.
