# Raport — Faza 0: Diagnostyka i testy

**Data:** 2026-08-14
**Cel:** wykonanie Fazy 0 z [`kompleksowa_analiza_projektu_2026-08-14.md`](kompleksowa_analiza_projektu_2026-08-14.md):
1. Uruchomienie pełnego builda i wszystkich testów (w tym [`test_field_rotation_st4.cpp`](../../tests/test_field_rotation_st4.cpp)).
2. Test integracyjny proxy (Node.js supertest) potwierdzający obecne odpowiedzi 502/symulowane dla tras rozszerzonych (regresja przed zmianą).

---

## 1. Build i testy C++

### Wykonano

- Rekonfiguracja CMake w `build/` — cel `test_field_rotation_st4` **nie był obecny** w dotychczasowym buildzie (target dodany do [`CMakeLists.txt`](../../CMakeLists.txt:700), ale build nie był rekonfigurowany).
- Pełny build: `make -j$(nproc)` — wszystkie cele zbudowane poprawnie.
- Testy: `ctest --output-on-failure`.

### Wynik

```
100% tests passed, 0 tests failed out of 16
```

| Test | Wynik |
|------|:-----:|
| AstronomicalCalculationsTest | ✅ |
| TPOINTModelTest | ✅ |
| KalmanFilterTest | ✅ |
| GrpcIntegrationTest | ✅ |
| SubArcsecondAccuracyTest | ✅ |
| ConfigurationTest | ✅ |
| MountControllerTest | ✅ |
| EphemerisTrackerTest | ✅ |
| HALIntegrationTest | ✅ |
| GamepadHALTest | ✅ |
| WatchdogTest | ✅ |
| LoggerTest | ✅ |
| ConfigMonitorTest | ✅ |
| SerialHALTest | ✅ |
| EthernetHALTest | ✅ |
| **FieldRotationSt4Test** | ✅ (12/12) |

### Poprawki w `test_field_rotation_st4.cpp`

Test był **nowy i nigdy wcześniej nie kompilowany/nie uruchamiany** (raport 2026-08-14 zalecał jego uruchomienie). Wykryto i naprawiono dwie usterki:

1. **Błąd kompilacji (typ HAL):** plik używał `hal::St4Control` / `hal::St4Direction` bez wprowadzenia `astro_mount::hal` do zakresu — `MockSt4` nie dziedziczył po właściwym `St4Control`, a `hal::St4Direction` nie był rozwiązywalny. Naprawa: alias przestrzeni nazw `namespace hal = astro_mount::hal;` (sam `using namespace` nie wystarczy — nie udostępnia nazwy przestrzeni do notacji kwalifikowanej `hal::`).

2. **Błędna przesłanka fizyczna testu `NonIdentityQuaternionChangesAngle`:** test zakładał, że *yaw montażu wokół osi zenitu* zmienia kąt rotacji pola. W [`calculateCasual()`](../../src/models/field_rotation_model.cpp:79) człon roll montażu to kąt między osią1 montażu a zenitem rzutowanymi na płaszczyznę obrazu. **Yaw wokół zenitu nie zmienia tego kąta** — oś wysokościowa pozostaje pionowa (jest osią obrotu), więc roll = 0. To zachowanie jest fizycznie poprawne (obrót kamery wokół pionu nie obraca obrazu na matrycy). Poprawiono test tak, aby używał obrotu montażu **wokół własnej osi celowania** o 45° — taki roll wprost obraca obraz i musi zmienić kąt o ≈45°, pozostawiając prędkość paralaktyczną bez zmian.

> Implementacja `calculateCasual` jest spójna z definicją modelu — **to test miał błędną przesłankę**, nie kod modelu.

---

## 2. Test integracyjny proxy (regresja przed zmianą)

### Wykonano

1. **Refaktor [`web/proxy/server.js`](../../web/proxy/server.js) umożliwiający testowanie** (zmiana niebehavioralna):
   - Start (tworzenie klientów gRPC + `app.listen`) objęty guardem `require.main === module` — przy imporcie do testu Express `app` jest eksportowany bez wiązania portu.
   - `module.exports = app` na końcu pliku.
   - Morgan (logowanie żądań) wyciszany przy `NODE_ENV=test`.
   - Zweryfikowano, że start produkcyjny (`node server.js`) działa bez zmian (banner, bind na 8080).

2. **Dodano `supertest`** jako devDependency w [`web/proxy/package.json`](../../web/proxy/package.json) oraz skrypt `"test": "NODE_ENV=test node --test test/"`.

3. **Nowy test** [`web/proxy/test/proxy.integration.test.js`](../../web/proxy/test/proxy.integration.test.js) (node:test + supertest, 30 asercji).

### Wynik

```
# tests 30
# pass 30
# fail 0
```

| Grupa | Zachowanie potwierdzone (baseline) | Liczba |
|-------|------------------------------------|:------:|
| POST tras rozszerzonych (camera/focuser/guider/pulley/power/sequencer/pec) | **HTTP 502** z payloadem `error` | 18 |
| GET tras rozszerzonych (camera/focuser/guider/pulley/weather/power/sequencer/pec) | **HTTP 200 z twardo zakodowanymi danymi symulowanymi** (np. `Simulated Camera`, `position: 50000`, `temperature_c: 15.5`) | 8 |
| Kontrola: trasa montażu `/api/status` | **HTTP 503** „Mount controller unreachable” (jawne zgłoszenie błędu — odwrotność cichego symulowania) | 1 |

Test potwierdza ustalenie **P1** z raportu: trasy serwisów rozszerzonych wywołują RPC nieistniejące na kliencie `MountControllerService` → POST dają 502, a GET **cicho zwracają dane symulowane** (użytkownik UI nie widzi błędu). Po naprawie w Fazie 1 test **należy zaktualizować** (oczekiwanie realnych danych lub jawnego `503` zamiast danych symulowanych).

### Jak uruchomić

```bash
# Build + testy C++
cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc) && ctest --output-on-failure

# Testy proxy
cd web/proxy && npm install && npm test
```

---

## 3. Wnioski dla Fazy 1

- Ustalenie **P1** potwierdzone testem: 18 endpointów POST → 502, 8 endpointów GET → dane symulowane.
- Warstwa integracji proxy jest jedyną blokującą wiarygodność UI — Faza 1 może startować z zielonym baseline (16/16 C++ + 30/30 proxy).
- Warto w Fazie 1 wykorzystać refaktor `server.js` (eksport `app`) do dalszych testów integracyjnych.
