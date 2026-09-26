# Audyt implementacji kontrolera CANopen pod kątem STM42M

**Data:** 2026-09-22
**Zakres:** implementacja CANopen w kodzie projektu `astro_mount_control`
**Źródło wymagań:** [`docs/stm42-canopen-protocol.pdf`](../stm42-canopen-protocol.pdf) (NiMotion STM42)

---

## 1. Wnioski

| Pytanie | Odpowiedź |
|---------|-----------|
| Czy implementacja kontrolera CANopen jest kompletna? | **NIE** |
| Czy uwzględnia możliwości STM42M? | **NIE** — w kodzie nie ma żadnej implementacji CANopen/CiA 402 |
| Jaki jest rzeczywisty stan? | CANopen **nie istnieje w kodzie**; jedyna działająca ścieżka CAN to MF7025V2 (LingKong) |

`HALFactory::create(HALType::CANOPEN)` rzuca wyjątek **„CANopen HAL implementation not yet available"** — [`src/hal/hal_factory.cpp:22`](../../src/hal/hal_factory.cpp:22). Typ `CANOPEN` nie jest nawet raportowany przez [`getAvailableTypes()`](../../src/hal/hal_factory.cpp:48).

---

## 2. Stan faktyczny plików

### ❌ Nie istnieją (opisywane w dokumentacji, ale nieobecne w drzewie)

| Opisywany plik | Rola | Status |
|----------------|------|--------|
| `include/controllers/icanopen_interface.h` | Abstrakcyjny interfejs CANopen | **BRAK** |
| `include/controllers/canopen_interface.h` | Konkretny interfejs CiA 402 | **BRAK** |
| `include/controllers/canopen_factory.h` | Fabryka CANopen | **BRAK** |
| `src/controllers/canopen_interface.cpp` | Implementacja CiA 402 | **BRAK** |
| `src/controllers/canopen_factory.cpp` | Fabryka CANopen | **BRAK** |
| `lib/canopen_wrapper/` | Wrapper C nad SocketCAN | **BRAK** (katalog `lib/` nie istnieje) |
| `src/hal/canopen_hal/` | HAL CANopen (CanOpenMotor/Encoder/SafetyMonitor) | **BRAK** |
| `src/api/canopen_server.cpp` | Serwer gRPC CANopen | **BRAK** |
| `proto/canopen_service.proto` | Kontrakt gRPC CANopen | **BRAK** (są tylko nieaktualne artefakty `build/gen_py/canopen_service_pb2.py`) |

### ✅ Istnieją (pozostałości/konfiguracja bez implementacji)

| Plik | Zawartość |
|------|-----------|
| [`src/hal/hal_factory.cpp`](../../src/hal/hal_factory.cpp:17) | `case CANOPEN: throw "not yet available"` |
| [`include/hal/hal_config.h`](../../include/hal/hal_config.h:14) | enum `HALType::CANOPEN`, `can_node_id` |
| [`proto/mount_controller.proto`](../../proto/mount_controller.proto:1129) | `CanOpenConfig` (library, interface, bitrate, node_id, use_sync) |
| [`config/canopen.json`](../../config/canopen.json:1) | Konfiguracja CANopen (nieużywana — HAL rzuca wyjątek) |
| [`include/config/mount_config.h`](../../include/config/mount_config.h:19) | `position_counts_per_degree` domyślnie `4000/360` (zgodne z STM42) |
| `build/external/canopennode/` | Biblioteka CANopenNode pobierana przy budowie |
| `scripts/canopen_*.sh` | **Jedyne działające** narzędzia STM42M (CLI z tej sesji) |

> Dokumenty [`canopen_compliance_audit.md`](../canopen_compliance_audit.md) i
> [`canopen_hal_verification_report.md`](../canopen_hal_verification_report.md)
> opisują kod, który **nie istnieje** — to dokumentacja aspirowana/nieaktualna.

---

## 3. Możliwości STM42M vs stan implementacji

| # | Możliwość STM42M (obiekt / usługa) | W kontrolerze | W CLI (tej sesji) |
|---|-------------------------------------|:---:|:---:|
| 1 | Node-ID — odczyt/zapis (`200Ch:02h`) | ❌ | ✅ `canopen_set/get_node_id.sh` |
| 2 | Baud-rate CAN (`200Ch:03h`) | ❌ | ❌ |
| 3 | NMT: start/stop/reset/reset-comm (`0x000`) | ❌ | ⚠️ tylko start (`rotate`) |
| 4 | Heartbeat (`1017h`, `0x700+ID`) | ❌ (tylko w JSON) | ❌ |
| 5 | SDO odczyt/zapis (expedited) | ❌ | ✅ |
| 6 | PDO RPDO1–4 / TPDO1–4 + mapowanie (`1400h`, `1600h`, `1800h`, `1A00h`) | ❌ | ❌ |
| 7 | EMCY (`0x080+ID`, `1001h`, `1003h`) | ❌ | ❌ |
| 8 | CiA 402 state machine (`6040h`/`6041h`) | ❌ | ✅ (PP) |
| 9 | Profile Position Mode (`607Ah`, `6081h`, `6083h`, `6084h`) | ❌ | ✅ |
| 10 | Velocity Mode (`6042h`, `6048h`, `6049h`) | ❌ | ❌ |
| 11 | Profile Velocity Mode (`60FFh`) | ❌ | ❌ |
| 12 | Homing Mode (`6098h`, `6099h`, `609Ah`, `607Ch`) | ❌ | ❌ |
| 13 | Interpolated Position (`60C0h`, `60C1h`, `60C2h`) | ❌ | ❌ |
| 14 | CSP (`607Ah` + PDO) / CSV (`60FFh` + PDO) | ❌ | ❌ |
| 15 | Nimotion Position/Velocity Mode (`2002h:01h`) | ❌ | ❌ |
| 16 | Konwersja jednostek (`608Fh`, `6091h`, `607Eh`) | ❌ | ⚠️ pośrednio `--units-per-turn` |
| 17 | Zapis/przywrócenie parametrów (`1010h`/`1011h`) | ❌ | ✅ (save w `set_node_id`) |
| 18 | Kasowanie błędu (`6040h` bit7, `1003h`) | ❌ | ❌ |
| 19 | Pozycja aktualna/żądana (`6062h`, `6063h`, `6064h`) | ❌ | ✅ (odczyt `6064h`) |
| 20 | Following error (`6065h`, `6066h`, `60F4h`) | ❌ | ❌ |
| 21 | Position window/time (`6067h`, `6068h`) | ❌ | ❌ |
| 22 | Prędkość aktualna (`606Ch`) | ❌ | ❌ |
| 23 | Parametry profilu (`607Fh`, `6080h`, `6082h`, `6085h`, `6086h`, `60C5h`, `60C6h`) | ❌ | ❌ |
| 24 | Wejścia/wyjścia cyfrowe (`2003h`, `2004h`, `2017h`, `2021h`, `200Bh`) | ❌ | ❌ |

**Podsumowanie:** 0/24 możliwości w kontrolerze; 5–6/24 w CLI.

---

## 4. Rozbieżności konfiguracji `config/canopen.json` względem STM42M

| Pole | Wartość w JSON | Wymagana dla STM42M |
|------|----------------|---------------------|
| `can_node_id` | **5** i **6** | fabrycznie **1** (obecnie urządzenia mają ID 1 i 2) |
| `encoder_resolution` | **16384** | **4000** (closed-loop, `608Fh:01h`) |
| `encoder_counts_per_degree` | **10000** | **~11.11** (4000/360) |
| `servo_init` | tylko `6040h = 6→7→15` | brak NMT start, brak `2002h:01h=0`, `6060h=1`, homingu |
| `default_mode` | `"POSITION"` | wymaga `6060h=1` (PP) |

Konfiguracja jest przygotowana pod serwonapędy z enkoderem 16384 counts/obrót, a nie pod STM42M.

---

## 5. Rekomendacje

1. **Nie używać** `config/canopen.json` dla STM42M w obecnym stanie — HAL CANopen nie istnieje.
2. Do STM42M używać CLI: [`canopen_set_node_id.sh`](../../scripts/canopen_set_node_id.sh), [`canopen_get_node_id.sh`](../../scripts/canopen_get_node_id.sh), [`canopen_rotate.sh`](../../scripts/canopen_rotate.sh).
3. Jeśli STM42M ma być wspierany natywnie, wymagane jest wdrożenie od zera:
   - warstwa C SocketCAN (SDO expedited + retry),
   - `ICanOpenInterface` + `canopen_interface.cpp` (CiA 402: PP/PV/HM),
   - `src/hal/canopen_hal/` (MotorControl, EncoderReader przez `6064h`/`606Ch`),
   - `HALFactory::createCanOpenHAL()` zamiast `throw`.
4. Zaktualizować `config/canopen.json` pod STM42M: `can_node_id=1/2`, rozdzielczość 4000, sekwencja init z NMT start + `2002h:01h` + `6060h`.
5. Oznaczyć dokumenty `canopen_compliance_audit.md` i `canopen_hal_verification_report.md` jako nieaktualne (opisują nieistniejący kod).
