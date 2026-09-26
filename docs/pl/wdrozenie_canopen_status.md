# Status wdrożenia CANopen (STM42M)

**Data:** 2026-09-22
**Zakres:** Fazy 0–6 planu [`docs/pl/plan_wdrozenia_canopen_stm42m.md`](plan_wdrozenia_canopen_stm42m.md)

## Zrealizowane

| Faza | Pliki | Status |
|------|-------|--------|
| 0 — warstwa C SocketCAN | [`lib/canopen_wrapper/include/canopen/canopen.h`](../../lib/canopen_wrapper/include/canopen/canopen.h), [`lib/canopen_wrapper/src/canopen.cpp`](../../lib/canopen_wrapper/src/canopen.cpp) | ✅ |
| 1 — interfejs C++ CiA 402 | [`include/controllers/icanopen_interface.h`](../../include/controllers/icanopen_interface.h), [`include/controllers/canopen_interface.h`](../../include/controllers/canopen_interface.h), [`src/controllers/canopen_interface.cpp`](../../src/controllers/canopen_interface.cpp), [`include/controllers/canopen_factory.h`](../../include/controllers/canopen_factory.h), [`src/controllers/canopen_factory.cpp`](../../src/controllers/canopen_factory.cpp) | ✅ |
| 2 — HAL | [`include/hal/canopen_hal/canopen_hal.h`](../../include/hal/canopen_hal/canopen_hal.h), [`src/hal/canopen_hal/canopen_hal.cpp`](../../src/hal/canopen_hal/canopen_hal.cpp) | ✅ |
| 3 — HALFactory | [`src/hal/hal_factory.cpp`](../../src/hal/hal_factory.cpp) — `createCanOpenHAL()` + `getAvailableTypes()` | ✅ |
| 4 — konfiguracja | [`config/canopen_stm42m.json`](../../config/canopen_stm42m.json) | ✅ |
| 5 — testy | [`tests/test_canopen_wrapper.cpp`](../../tests/test_canopen_wrapper.cpp), [`tests/test_canopen_factory.cpp`](../../tests/test_canopen_factory.cpp), [`tests/test_canopen_hal.cpp`](../../tests/test_canopen_hal.cpp) | ✅ 13/13 |
| 6 — CMake/docs | [`CMakeLists.txt`](../../CMakeLists.txt), ten dokument | ✅ |

## Wyniki testów

```
test_canopen_wrapper : 6/6  PASSED
test_canopen_factory : 3/3  PASSED
test_canopen_hal     : 6/6  PASSED
```

## Zakres funkcjonalny

- SDO expedited read/write (1/2/4 B) + retry + kody abort
- NMT start/stop/pre-op/reset
- Heartbeat / boot-up (`0x700+ID`) i EMCY (`0x080+ID`) z callbackami,
  wpięte w pętlę monitorującą HAL przez `pollEvents()` + `setHeartbeatPeriod(1017h)`
- CiA 402 state machine (`6040h` 6→7→15, weryfikacja `6041h`)
- Profile Position Mode (`607Ah`, `6081h`, `6083h`, `6084h`)
- Profile Velocity Mode (`60FFh`)
- Homing Mode (`6098h`, `6099h`, `609Ah`, `MotorControl::home()`)
- PDO: TPDO1 (`6041h`+`6064h`), RPDO2 (`6040h`+`607Ah`), mapowanie dynamiczne + cache
- PID grupa `2008h` (`writePidLoopRam`: Kpc, SpdFdFwrGain, PosLoopGain)
- Odczyt pozycji `6064h` / `6063h`, prędkości `606Ch`
- Fault reset (`6040h` bit7)
- Node-ID / baud / EEPROM save (`200Ch`, `1010h`)
- HAL: `CanOpenMotor`, `CanOpenEncoder`, `CanOpenSafetyMonitor`, wątek monitorujący

## Uwagi

- [`docs/canopen_compliance_audit.md`](../canopen_compliance_audit.md) i [`docs/canopen_hal_verification_report.md`](../canopen_hal_verification_report.md) opisują stan sprzed wdrożenia — wymagają ponownego audytu po integracji.
- PID (`2008h`), PDO mapping i homing (`6098h`) zostały wdrożone w drugiej iteracji (patrz wyżej).
- Mapowanie PID opiera się na skalach z PDF (`Kpc` 0.01, `SpdFdFwrGain` 0.01, `PosLoopGain` 0.00001); przed użyciem w produkcji zweryfikować skale z firmwarem STM42M.
