# Analiza poprawności i stabilności drivera CANopen (STM42M)

**Data:** 2026-09-22
**Zakres:** [`lib/canopen_wrapper/`](../../lib/canopen_wrapper/), [`src/controllers/canopen_*`](../../src/controllers/), [`src/hal/canopen_hal/`](../../src/hal/canopen_hal/), [`include/hal/canopen_hal/`](../../include/hal/canopen_hal/)

---

## 1. Podsumowanie

Driver jest **poprawny protokołowo** (SDO expedited, NMT, CiA 402 state machine, PP/PV/HM, PDO) i **stabilny w typowym scenariuszu** (wątek monitorujący HAL zatrzymywany przed zamknięciem interfejsu). Analiza wykryła **3 problemy wysokiego ryzyka**, które zostały naprawione, oraz **kilka ryzyk średnich/niskich** do adresowania w kolejnych iteracjach.

---

## 2. Znalezione i naprawione problemy (HIGH)

### H1 — Wyścig w `canopen_shutdown()` (use-after-free)
**Plik:** [`lib/canopen_wrapper/src/canopen.cpp`](../../lib/canopen_wrapper/src/canopen.cpp:206)

Zamykanie socketu i `delete` mutexu bez synchronizacji mogło doprowadzić do dereferencji zwolnionego mutexu przez wątek czekający w SDO.

**Naprawa:** zamknięcie fd pod blokadą mutexu; mutex celowo nie jest zwalniany (jedna alokacja na cykl życia kontekstu — pomijalne).

### H2 — Utrata pozycji przy częściowym błędzie odczytu SDO
**Pliki:** [`canopen_interface.cpp`](../../src/controllers/canopen_interface.cpp:222), [`canopen_hal.cpp`](../../src/hal/canopen_hal/canopen_hal.cpp:203)

`getDriveStatus()` zwracał `true` z `actual_position = 0`, gdy odczyt `6064h` się nie powiódł, a `updateStatus()` nadpisywał cache pozycji zerem → skok pozycji o pełny obrót w logice sterowania.

**Naprawa:** dodano flagę `position_valid` w [`CanOpenStatus`](../../include/controllers/icanopen_interface.h:12); `updateStatus()` aktualizuje pozycję tylko, gdy flaga jest ustawiona.

### H3 — Brak kodu abort w logu (diagnostyka)
**Plik:** [`canopen.cpp`](../../lib/canopen_wrapper/src/canopen.cpp:115)

Kod abortu z ramki `0x80` nie był przekazywany do `log_abort()` (zawsze logowano `0x00000000`).

**Naprawa:** wartość z bajtów 4–7 jest wyodrębniana przed sprawdzeniem abortu.

---

## 3. Ryzyka średnie (do zaadresowania)

| # | Ryzyko | Miejsce | Uwagi |
|---|--------|---------|-------|
| M1 | Blokowanie monitora na martwym węźle | [`canopen_hal.cpp`](../../src/hal/canopen_hal/canopen_hal.cpp:515) | Przy `sdo_timeout_ms=1000` i 3 retry, 2 martwe osie blokują pętlę do ~18 s. Zalecane: `sdo_timeout_ms≈200` i/lub osobny wątek na oś. |
| M2 | Tryb PV wymaga stanu nieaktywnego | [`canopen_interface.cpp`](../../src/controllers/canopen_interface.cpp:208) | `setVelocityTarget()` pisze `6060h=3`; przy załączonym napędzie SDO abort → funkcja zwraca `false` (poprawnie), ale przełączanie trybu w locie nie działa. |
| M3 | `pdo_cache_` bez synchronizacji | [`canopen_interface.h`](../../include/controllers/canopen_interface.h:89) | Obecnie zapis i odczyt odbywają się w tym samym wątku monitorującym — bezpieczne de facto, ale kruche przy przyszłym dostępie z gRPC. Zalecany `std::mutex`. |
| M4 | Retry SDO write na timeout | [`canopen.cpp`](../../lib/canopen_wrapper/src/canopen.cpp:266) | Ponowny zapis jest idempotentny dla wartości, ale należy pamiętać, że nie jest to bezpieczne dla przyszłych transferów segmentowanych (brak bitu toggle). |
| M5 | Konwersja prędkości <1 count/s | [`canopen_hal.cpp`](../../src/hal/canopen_hal/canopen_hal.cpp:24) | Prędkości śledzenia (≈0.05 count/s) kwantyzują się do 1 — ograniczenie rozdzielczości STM42 (4000 counts/obrót). |

---

## 4. Ryzyka niskie

- `enableDrive()` ignoruje wynik zapisu `2002h:01h`/`6060h` (przy niepowodzeniu napęd może zostać w złym trybie).
- `dispatch_frame()` traktuje cały zakres `0x180–0x57F` jako PDO (dla mastera nieszkodliwe).
- Wątek callbacków (`pdo_cb`/`nmt_cb`/`emcy_cb`) jest wywoływany pod mutexem SDO — bezpieczne dziś (callback tylko zapisuje cache), ale wymaga dyscypliny przy rozbudowie.
- `CanOpenEncoder::read()` odczytuje `calibration_offset_` bez blokady (double, potencjalne rozdarcie na 32-bit).

---

## 5. Wnioski

- **Poprawność:** sekwencje SDO, NMT i CiA 402 są zgodne z [`docs/stm42-canopen-protocol.pdf`](../stm42-canopen-protocol.pdf); weryfikacja na rzeczywistych napędach (ping ID 1/2, obrót +45°, +90°) potwierdziła działanie.
- **Stabilność:** po naprawach H1–H3 nie ma znanych ścieżek use-after-free ani nadpisywania pozycji zerem; wątki HAL są zatrzymywane przed zamknięciem interfejsu.
- **Testy:** 15/15 (`wrapper` 6, `factory` 3, `hal` 6) przechodzi po zmianach.
- **Priorytet dalszych prac:** M1 (skrócenie timeoutu SDO lub monitor per oś) i M3 (mutex na `pdo_cache_`).
