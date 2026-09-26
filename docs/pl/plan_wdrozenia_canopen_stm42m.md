# Plan uzupełnienia funkcjonalności CANopen (STM42M)

**Data:** 2026-09-22
**Punkt wyjścia:** audyt [`docs/pl/audyt_canopen_stm42m.md`](audyt_canopen_stm42m.md) — CANopen w kodzie nie istnieje, jedyna ścieżka CAN to MF7025V2.
**Źródło wymagań:** [`docs/stm42-canopen-protocol.pdf`](../stm42-canopen-protocol.pdf) (NiMotion STM42/STM42M).

---

## 1. Architektura docelowa (wzorowana na MF7025V2)

```
LAYER 4  src/api/service_impl.cpp          (bez zmian — korzysta z HALInterface)
LAYER 3  src/controllers/mount_controller.cpp / derotator_controller.cpp
LAYER 2  src/hal/canopen_hal/               ← NOWE: CanOpenHAL
            ├── CanOpenMotor      : MotorControl
            ├── CanOpenEncoder    : EncoderReader
            └── CanOpenSafetyMonitor : SafetyMonitor
LAYER 1  src/controllers/
            ├── icanopen_interface.h        ← NOWE: abstrakcja
            ├── canopen_interface.cpp/.h    ← NOWE: CiA 402 impl
            └── canopen_factory.cpp/.h      ← NOWE: fabryka
LAYER 0  lib/canopen_wrapper/              ← NOWE: C + SocketCAN
            ├── include/canopen/canopen.h
            └── src/canopen.cpp
```

Wzorzec implementacyjny: [`Mf7025v2Hal`](../../include/hal/mf7025v2_hal/mf7025v2_hal.h) + [`IMf7025v2Interface`](../../include/controllers/imf7025v2_interface.h).

---

## 2. Fazy wdrożenia

### Faza 0 — Warstwa C nad SocketCAN (`lib/canopen_wrapper`)

**Cel:** surowy dostęp do magistrali CAN z obsługą SDO/NMT/Heartbeat/EMCY.

| # | Zadanie | Szczegóły |
|---|---------|-----------|
| 0.1 | `canopen.h` | Typy: `canopen_ctx_t`, COB-ID (`0x580/0x600/0x700/0x080 + node`), stałe SDO (`0x2B/0x2F/0x23/0x40/0x4B/0x43/0x60/0x80`), kody abort (tabela 3-2 PDF) |
| 0.2 | `canopen.cpp` — socket | open/close, wątek reader (poll), wspólny `sock_mutex` |
| 0.3 | SDO expedited write | 1/2/4 bajty (`0x2F/0x2B/0x23`), retry (MAX_RETRIES=2, backoff 50 ms), parsowanie abort (`(data[0] & 0xE0) == 0x80`) |
| 0.4 | SDO expedited read | `0x40` → odpowiedź `0x4F/0x4B/0x43`, retry jak przy write |
| 0.5 | NMT | `0x000` + `01/02/80/81/82 <node>` (start/stop/pre-op/reset/reset-comm) |
| 0.6 | Heartbeat/Boot-up | odbiór `0x700+node`, callback `nmt_cb` (stany 0x00/0x04/0x05/0x7F) |
| 0.7 | EMCY | odbiór `0x080+node` (`(cob_id & ~0x7F) == 0x080`), callback `emcy_cb`, zapis `last_emergency[node]` |
| 0.8 | Testy | `tests/test_canopen_wrapper.cpp` — encode/decode ramek, abort kody, retry |

**Kryterium akceptacji:** testy jednostkowe SDO read/write przechodzą na `vcan0`.

---

### Faza 1 — Warstwa C++ interfejsu (`src/controllers/`)

**Cel:** abstrakcja `ICanOpenInterface` + implementacja CiA 402 dla STM42M.

| # | Zadanie | Szczegóły |
|---|---------|-----------|
| 1.1 | `icanopen_interface.h` | Metody analogiczne do `IMf7025v2Interface`: `open/close`, `enableDrive/disableDrive`, `setPositionTarget`, `setVelocityTarget`, `stopAxis`, `emergencyStop`, `getDriveStatus`, `getPositionData`, `getEncoderData`, `clearErrors`, `sendSDO/readSDO`, `sendNMT`, `configureDrive`, `getNodeId/setNodeId`, `setBaudRate`, `saveParameters` |
| 1.2 | `canopen_interface.h/.cpp` | CiA 402 state machine: `6040h = 6→7→F` z weryfikacją `6041h`; PP (zapis `607Ah/6081h/6083h/6084h`, wyzwolenie `0x4F→0x5F` rel. / `0x0F→0x1F` abs.); PV (`60FFh`); HM (`6098h/6099h/609Ah`, home offset `607Ch`) |
| 1.3 | Tryb/konfiguracja | `2002h:01h=0` (CiA402), `6060h` (tryb), `608Fh`/`6091h` (przeliczanie), `607Eh` (polaryzacja) |
| 1.4 | Odczyt stanu | `6041h` (status word + bity: enabled=0x27/0x37, target reached=0x400, fault=0x08), `6064h` (pozycja), `606Ch` (prędkość), `1001h`/`1003h` (błędy) |
| 1.5 | Node-ID/EEPROM | `200Ch:02h` (Node-ID), `200Ch:03h` (baud), `1010h:01h` = `0x65766173` (save), `1011h:01h` (restore) |
| 1.6 | `canopen_factory.h/.cpp` | `create("mock"/"canopensocket"/"canfestival")`; mock do testów |
| 1.7 | `accel_mode` | `"rate"` → `6083h` = przyspieszenie [u/s²]; `"time"` → przeliczenie czasu rampy na przyspieszenie |

**Kryterium akceptacji:** mock + `vcan0` obsługują enable/disable, PP obrót, PV, odczyt pozycji i fault reset.

---

### Faza 2 — HAL (`src/hal/canopen_hal/`)

**Cel:** implementacja interfejsów HAL na bazie warstwy 1.

| # | Zadanie | Mapowanie na STM42M |
|---|---------|---------------------|
| 2.1 | `CanOpenMotor : MotorControl` | `enable()` → NMT start + `6040h 6→7→F`; `setPosition()` → PP `607Ah`; `setVelocity()` → PV `60FFh`; `stop()` → `6040h` halt/quick stop; `emergencyStop()` → `6040h=0x02`; `getActualPosition()` → `6064h`; `targetReached()` → `6041h` bit10; `clearErrors()` → `6040h` bit7 |
| 2.2 | `CanOpenEncoder : EncoderReader` | `read()` → `6064h` (user units) / `6063h` (encoder units); `calibrate()` → `607Ch` home offset + `2017h:02h` bit1 (set origin) |
| 2.3 | `CanOpenSafetyMonitor : SafetyMonitor` | heartbeat `0x700+node` (NMT), EMCY `0x080+node`, `1001h` error register; limit checks wg `SafetyConfig` |
| 2.4 | `CanOpenHAL : HALInterface` | `initialize()` tworzy interfejs + komponenty; `createMotorControl/createEncoderReader/createSafetyMonitor`; `start/stop`; status/diagnostyka |
| 2.5 | PID (opcjonalne) | STM42M ma grupę `2008h` (PosLoopGain `2008:01`, Kpc `2008:02`, SpdFilterCoeff `2008:0B`) → `writePidLoopRam()` może mapować na `2008h` |
| 2.6 | Pozycja absolutna/rewind | `position_rewind_enabled` → zerowanie licznika przez `2017h:02h` (set origin) po przekroczeniu progu |

**Kryterium akceptacji:** `CanOpenHAL` przechodzi `test_canopen_hal` (44 przypadki analogiczne do MF7025V2).

---

### Faza 3 — Integracja z `HALFactory`

| # | Zadanie |
|---|---------|
| 3.1 | Zaimplementować `HALFactory::createCanOpenHAL()` zamiast `throw` ([`hal_factory.cpp:22`](../../src/hal/hal_factory.cpp:22)) |
| 3.2 | Dodać `HALType::CANOPEN` do `getAvailableTypes()` (Linux/SocketCAN) |
| 3.3 | Konwersja `hal::CanOpenConfig` → `ICanOpenInterface::Config` w fabryce |

**Kryterium akceptacji:** `HALFactory::create(HALType::CANOPEN)` zwraca działający HAL; `isTypeAvailable(CANOPEN)==true` na Linuksie.

---

### Faza 4 — Konfiguracja STM42M

| # | Zadanie |
|---|---------|
| 4.1 | Nowy plik `config/canopen_stm42m.json` (lub poprawka `config/canopen.json`): `can_node_id=1/2`, `encoder_resolution=4000`, `counts_per_degree=11.111...` |
| 4.2 | `servo_init` rozszerzyć o: NMT start, `2002h:01h=0`, `6060h=1` (PP), przed `6040h 6→7→F` |
| 4.3 | Domyślne parametry ruchu: `6081h`, `6083h`, `6084h` zgodne z montażem |

**Kryterium akceptacji:** start aplikacji z `config/canopen_stm42m.json` załącza obie osie bez wyjątku.

---

### Faza 5 — Testy

| Test | Zakres |
|------|--------|
| `test_canopen_wrapper` | SDO encode/decode, abort, retry, NMT/EMCY/heartbeat |
| `test_canopen_factory` | tworzenie mock/canopensocket |
| `test_canopen_hal` | enable, PP move, PV, readback `6064h`, fault reset |
| `test_hal_integration` | MountController + mock CANopen (slew/park/stop) |
| `test_subarcsecond_accuracy` | rozszerzenie o rzeczywiste skale `4000/360` |

---

### Faza 6 — Dokumentacja, CMake, sprzątanie

| # | Zadanie |
|---|---------|
| 6.1 | CMake: dodać `lib/canopen_wrapper` i `src/hal/canopen_hal`, flaga `ENABLE_CANOPEN` |
| 6.2 | Oznaczyć [`canopen_compliance_audit.md`](../canopen_compliance_audit.md) i [`canopen_hal_verification_report.md`](../canopen_hal_verification_report.md) jako nieaktualne/do ponownego audytu po wdrożeniu |
| 6.3 | Zaktualizować [`hal_layer.md`](hal_layer.md) i [`architecture.md`](architecture.md) |

---

## 3. Mapowanie obiektów STM42M → warstwy

| Obiekt | Znaczenie | Warstwa |
|--------|-----------|---------|
| `200Ch:02h` | Node-ID (1–127) | 0/1 (`setNodeId`) |
| `200Ch:03h` | Baud-rate | 0/1 |
| `2002h:01h` | Tryb CiA402/Nimotion | 1 (init) |
| `6060h` / `6061h` | Tryb operacji / display | 1 |
| `6040h` / `6041h` | Control/Status word | 1/2 |
| `607Ah` | Target position | 1/2 |
| `6081h/6083h/6084h` | Profile velocity/accel/decel | 1/2 |
| `60FFh` | Target velocity (PV/CSV) | 1/2 |
| `6098h/6099h/609Ah/607Ch` | Homing | 1 |
| `6062h/6063h/6064h` | Position demand/actual | 1/2 (encoder) |
| `606Ch` | Velocity actual | 1/2 |
| `608Fh/6091h/607Eh` | Enkoder/gear/polaryzacja | 1 |
| `1010h/1011h` | Save/Restore | 1 |
| `1001h/1003h` | Error register/field | 1/2 (safety) |
| `1017h` | Heartbeat period | 1 (init) |
| `2008h` | Pętle PID (PosLoopGain itd.) | 2 (opcjonalnie) |
| `2017h:02h` | Virtual DI (set origin) | 2 (rewind/calibracja) |

## 4. Format ramek SDO (przypomnienie implementacyjne)

```
write 1B : 2F <idx_lo> <idx_hi> <sub> <v0> 00 00 00   → potw. 60
write 2B : 2B <idx_lo> <idx_hi> <sub> <v0> <v1> 00 00 → potw. 60
write 4B : 23 <idx_lo> <idx_hi> <sub> <v0..v3> 00 00  → potw. 60
read     : 40 <idx_lo> <idx_hi> <sub> 00 00 00 00     → odp. 4F/4B/43
abort    : 80 <idx_lo> <idx_hi> <sub> <code4B LE> 00 00
COB-ID   : 0x600+node (req), 0x580+node (resp)
NMT      : 000#01/02/80/81/82 <node>
```

## 5. Szacunkowy nakład

| Faza | Nakład | Priorytet |
|------|--------|-----------|
| 0 (C wrapper) | 2–3 dni | Krytyczny |
| 1 (C++ CiA 402) | 3–4 dni | Krytyczny |
| 2 (HAL) | 2–3 dni | Krytyczny |
| 3 (HALFactory) | 0.5 dnia | Wysoki |
| 4 (config) | 0.5 dnia | Wysoki |
| 5 (testy) | 2 dni | Wysoki |
| 6 (docs/CMake) | 1 dzień | Średni |
| **Razem** | **11–14 dni** | |

## 6. Rekomendowana kolejność realizacji

1. **Faza 0** — bez warstwy C nic nie zadziała; testy na `vcan0`.
2. **Faza 1** — minimum: state machine + PP + odczyt `6064h` (pokrywa CLI, które już działa).
3. **Faza 2 + 3** — wpięcie do HALFactory (odblokowuje `config/canopen*.json`).
4. **Faza 4** — poprawny config STM42M.
5. **Faza 5/6** — testy i dokumentacja.
