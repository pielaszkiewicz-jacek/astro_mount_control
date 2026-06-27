# Raport weryfikacji układu współrzędnych

Data analizy: 2026-06-27

---

## 1. Maksymalny zakres współrzędnych montażu

### Pytanie: Czy kąt może być większy niż 360 stopni?

**Tak.** Kąty osi silników (serwo) mogą być dowolnie duże — przekraczać 360°, a nawet osiągać wartości rzędu tysięcy stopni. Pozycje teleskopu są normalizowane do zakresu `[0°, 360°)`.

#### Szczegóły dla osi silników (`axis1_position_`, `axis2_position_`):

- [mount_controller.cpp:1706-1716](src/controllers/mount_controller.cpp:1706) — jawny komentarz: *"Do NOT normalize axis1_position to [-180, 180] here. CANopen drives use absolute positioning — if the servo is at position 5400° (after 1 hour of tracking at 1.5°/s servo rate) … Double-precision float handles ~10 hours of tracking at 1.5°/s servo rate (54000°)"*.
- Dla montaży **EQUATORIAL** pozycje osi NIE są normalizowane — rosną bez ograniczeń podczas śledzenia.
- Dla montaży **ALT_AZ** i **CASUAL** pozycje osi są przycinane w pętli śledzenia: [mount_controller.cpp:1764-1787](src/controllers/mount_controller.cpp:1764) — wysokość do `[-5°, 90°]`, azymut do `[0°, 360°)`.

#### Szczegóły dla osi teleskopu (`telescope_axis1_position`, `telescope_axis2_position`):

Wyliczane w [`getStatus()`](src/controllers/mount_controller.cpp:2996) poprzez podzielenie pozycji serwa przez gear_ratio, a następnie znormalizowane:
- **EQUATORIAL**: HA → `[0°, 360°)`, Dec → `[-90°, 90°]` → `[0°, 360°)` ([linie 3021-3030](src/controllers/mount_controller.cpp:3021))
- **ALT_AZ**: Wysokość → `[0°, 90°]`, Azymut → `[0°, 360°)` ([linie 3031-3036](src/controllers/mount_controller.cpp:3031))
- **CASUAL**: Wysokość → `[0°, 90°]`, Azymut → `[0°, 360°)` ([linie 3037-3043](src/controllers/mount_controller.cpp:3037))

#### Błąd w komentarzu:

Komentarz przy deklaracji `axis1_position_` ([linia 5984](src/controllers/mount_controller.cpp:5984)) mówi: *"Normalized servo position [0°, 360°)"*. Jest to **błędny/nieaktualny** — pozycje serw NIE są normalizowane (co potwierdza kod w liniach 1706-1716).

---

## 2. Normalizacja kątów — poprawność

### Oś silnika

| Typ montażu | Normalizacja axis1 | Normalizacja axis2 |
|---|---|---|
| EQUATORIAL | ❌ brak (rośnie bez ograniczeń) | ❌ brak (rośnie bez ograniczeń) |
| ALT_AZ | ✅ clamp [-5°, 90°] (serwo°) | ✅ fmod [0°, 360°) (serwo°) |
| CASUAL | ✅ clamp [-5°, 90°] (serwo°) | ✅ fmod [0°, 360°) (serwo°) |

### Oś teleskopu (w getStatus)

| Typ montażu | telescope_axis1 | telescope_axis2 |
|---|---|---|
| EQUATORIAL | HA fmod [0°, 360°) | Dec flip do [-90°,90°] → fmod [0°,360°) |
| ALT_AZ | clamp [0°, 90°] | fmod [0°, 360°) |
| CASUAL | clamp [0°, 90°] | fmod [0°, 360°) |

**Werdykt:** Normalizacja osi teleskopu jest poprawna. Normalizacja osi silnika dla EQUATORIAL jest celowo pominięta (absolutne pozycjonowanie CANopen).

---

## 3. Limity — osie silnika vs osie montażu

### Pytanie: Czy limity odnoszą się do współrzędnych osi (silnika), czy do wynikowych współrzędnych montażu?

**Limity odnoszą się do wynikowych współrzędnych montażu (teleskopu).** To przypuszczenie użytkownika jest **BŁĘDNE**.

Funkcja [`evaluateSoftLimits()`](src/controllers/mount_controller.cpp:5866) przyjmuje pozycje osi w stopniach serwa, ale przed porównaniem z limitami dzieli je przez gear_ratio:

```cpp
// Linie 5897-5900
const double ha_gear = config_.ha_axis_params.gear_ratio > 0.0 ? ... : 360.0;
const double dec_gear = config_.dec_axis_params.gear_ratio > 0.0 ? ... : 360.0;
double telescope_axis1 = axis1_pos / ha_gear;   // konwersja: serwo° → teleskop°
double telescope_axis2 = axis2_pos / dec_gear;  // konwersja: serwo° → teleskop°
```

Dopiero `telescope_axis1` i `telescope_axis2` (w stopniach teleskopu) są porównywane z wartościami `config_.soft_limit_axis1_min` itd.

Dodatkowo, limity dla osi azymutalnej (axis2 w ALT_AZ/CASUAL) są pomijane przy sprawdzaniu naruszenia twardego limitu ([linie 1793-1797](src/controllers/mount_controller.cpp:1793)), ponieważ azymut wrapuje i nie ma fizycznego ogranicznika.

**Werdykt: Limity działają poprawnie — odnoszą się do współrzędnych teleskopu, nie serwomotorów.**

---

## 4. Operacja HOME

### Pytanie: Czy zasadna byłaby operacja HOME ustawiająca punkt referencyjny?

**Tak, w pełni zasadna.** Obecny stan:

- Jest przycisk HOME na gamepadzie ([linia 5499](src/controllers/mount_controller.cpp:5499)), ale wykonuje on **slew do pozycji parkowania** zamiast ustawiać punkt referencyjny ([linie 6211-6226](src/controllers/mount_controller.cpp:6211)).
- Nie ma dedykowanej funkcji `home()` wystawionej przez gRPC API.
- Istnieje tylko `homeDerotator()` ([linia 5088](src/controllers/mount_controller.cpp:5088)) — wyłącznie dla derotatora.

**Problem:** Po uruchomieniu, enkodery CANopen zgłaszają przypadkowe pozycje bezwzględne (zależne od stanu po poprzednim wyłączeniu). Bez punktu referencyjnego te pozycje są przekładane bezpośrednio na współrzędne teleskopu, powodując że montaż szybko osiąga limity kątów.

**Potrzebne:** Funkcja `home()` która:
1. Ustawia bieżącą pozycję osi jako znaną pozycję referencyjną (np. po ręcznym ustawieniu montażu na Polaris).
2. Alternatywnie: automatycznie znajduje pozycję referencyjną za pomocą czujników krańcowych (home switch) i ustawia znaną pozycję osi.

---

## 5. Błędna prezentacja współrzędnych w zakładce Status

### Problem potwierdzony — KRYTYCZNY BŁĄD

W zakładce Status pokazywane są **te same wartości** dla "osi silnika" i "osi teleskopu".

#### Ścieżka danych:

**1. Backend C++** — [`service_impl.cpp:119-125`](src/api/service_impl.cpp:119):

```cpp
auto* pos = response->mutable_current_position();
pos->set_axis1(status.telescope_axis1_position);  // ← Pozycja TELESKOPU
pos->set_axis2(status.telescope_axis2_position);  // ← Pozycja TELESKOPU
response->set_telescope_axis1(status.telescope_axis1_position);  // ← TO SAMO
response->set_telescope_axis2(status.telescope_axis2_position);  // ← TO SAMO
```

Obie pary pól (`current_position.axis1/2` i `telescope_axis1/2`) są wypełniane tą samą wartością: `status.telescope_axis1_position` / `status.telescope_axis2_position`. **Pozycja serwa (`status.axis1_position`) NIGDY nie jest eksportowana.**

**2. Proxy** — [`server.js:2272-2278`](web/proxy/server.js:2272):

```js
position: {
  axis1: state.current_position?.axis1 ?? 0,  // = telescope_axis1_position
  axis2: state.current_position?.axis2 ?? 0,  // = telescope_axis2_position
},
telescope: {
  axis1: state.telescope_axis1 ?? 0,          // = TO SAMO telescope_axis1_position
  axis2: state.telescope_axis2 ?? 0,          // = TO SAMO telescope_axis2_position
},
```

**3. Frontend** — [`mountStatus.js:416-431`](web/public/js/components/mountStatus.js:416):

Obie sekcje ("Position" i "Telescope") wyświetlają te same wartości.

#### Poprawka:

W `service_impl.cpp` należy:
```cpp
// Pozycje serwa (stopnie wału silnika):
pos->set_axis1(status.axis1_position);   // ← zmienić z telescope_axis1_position
pos->set_axis2(status.axis2_position);   // ← zmienić z telescope_axis2_position
// Pozycje teleskopu (po przełożeniu):
response->set_telescope_axis1(status.telescope_axis1_position);  // ← OK
response->set_telescope_axis2(status.telescope_axis2_position);  // ← OK
```

---

## 6. Niezgodność współrzędnych między zakładką Tests a Status

### Problem częściowo potwierdzony

Przyczyną rozbieżności jest **normalizacja**:

- **Zakładka Tests** ([debugTest.js:654-656](web/public/js/components/debugTest.js:654)): Po transformacji współrzędnych obiektu (RA/Dec → teleskop), `axis1Deg` jest **surową wartością astronomiczną** (np. HA może być ujemne: `-50°`).
- **Zakładka Status** ([mount_controller.cpp:3021-3030](src/controllers/mount_controller.cpp:3021)): `getStatus()` normalizuje pozycje teleskopu do `[0°, 360°)`, więc `-50°` → `310°`.

Gdy Tests odczytuje pozycję po slewie przez `state.position.axis1` (która pochodzi z `getStatus()`), otrzymuje wartość znormalizowaną (np. `310°`), podczas gdy transformacja pokazała `-50°`. Użytkownik widzi "wartość bezwzględną" (znormalizowaną), a nie oryginalny kąt astronomiczny.

**Dodatkowy problem:** Funkcja [`notifyStatusChanged()`](src/controllers/mount_controller.cpp:5703) (używana przez callbacki statusu) **NIE wypełnia** `telescope_axis1_position` i `telescope_axis2_position` — pozostawia je z wartością domyślną `0.0`. Odbiorcy callbacka (np. sterownik INDI) widzieliby zerowe pozycje teleskopu.

---

## Podsumowanie

| # | Podejrzenie użytkownika | Werdykt |
|---|---|---|
| 1 | Maksymalny zakres > 360° | **Potwierdzone** — osie serwa mogą być dowolnie duże; osie teleskopu normalizowane do [0°,360°) |
| 2 | Normalizacja kątów niepoprawna | **Częściowo** — normalizacja osi teleskopu poprawna; komentarz o normalizacji osi serwa błędny |
| 3 | Limity odnoszą się do osi silnika, nie montażu | **Fałsz** — `evaluateSoftLimits()` poprawnie dzieli przez gear_ratio przed porównaniem |
| 4 | Potrzebna operacja HOME | **W pełni zasadne** — obecny przycisk HOME tylko slewuje do pozycji parkowania |
| 5 | Status pokazuje te same pozycje dla osi i teleskopu | **Potwierdzone (krytyczny bug)** — `service_impl.cpp` eksportuje `telescope_axis1_position` do obu pól |
| 6 | Tests vs Status pokazują różne wartości | **Potwierdzone** — przyczyną normalizacja do [0°,360°) w Status vs surowe kąty w Tests |

### Dodatkowy znaleziony bug:
- [`notifyStatusChanged()`](src/controllers/mount_controller.cpp:5703) nie wypełnia pól `telescope_axis1_position` / `telescope_axis2_position` — zawsze wysyła `0.0` do subskrybentów callbacka.
