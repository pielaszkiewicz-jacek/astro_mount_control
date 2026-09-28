# Inicjalizacja kontrolera CANopen STM42M (NiMotion STMP42SXI, CiA 402)

Kontroler obsługiwany jest przez trzy warstwy:

1. Niskopoziomowy wrapper CANopen (CiA 301) — [`lib/canopen_wrapper/src/canopen.cpp`](../../lib/canopen_wrapper/src/canopen.cpp:154): surowe ramki SocketCAN, SDO expedited, NMT, heartbeat/EMCY/PDO.
2. Interfejs konkretny CiA 402 — [`src/controllers/canopen_interface.cpp`](../../src/controllers/canopen_interface.cpp:82): realizacja maszyny stanów CiA 402 i mapowania PDO.
3. HAL — [`src/hal/canopen_hal/canopen_hal.cpp`](../../src/hal/canopen_hal/canopen_hal.cpp:376): spina konfigurację JSON z interfejsem, tworzy obiekty `CanOpenMotor`/`CanOpenEncoder` i uruchamia wątek monitorujący.

Konfiguracja: [`config/canopen_stm42m.json`](../../config/canopen_stm42m.json:193) — interfejs `can0`, 1 Mbit/s, Node-ID: HA (oś 0) = 1, Dec (oś 1) = 2, enkoder 17-bit 131072 imp/obr.

## 1. Przebieg inicjalizacji

### 1.1 Utworzenie interfejsu (fabryka)

[`CanOpenFactory::create()`](../../src/controllers/canopen_factory.cpp:7) dla `library == "canopensocket"` zwraca `CanOpenInterface`. Ten interfejs jest przekazywany do `CanOpenHAL`.

### 1.2 `CanOpenHAL::initialize()`

[`CanOpenHAL::initialize()`](../../src/hal/canopen_hal/canopen_hal.cpp:376):

1. Kopiuje `HALConfig` (parsowany w [`include/hal/hal_config.h`](../../include/hal/hal_config.h:263) z sekcji `hal.canopen`).
2. Buduje `ICanOpenInterface::Config`: `interface_name=can0`, `bitrate=1000000`, `node_id=1`, `sdo_timeout_ms=500`, `pdo_update_rate=100`, `accel_mode=rate`, `pdo_config_enabled=true`, parametry przewijania pozycji oraz ustawienia NMT (heartbeat 100 ms itd.).
3. Buduje mapowanie `axis_id → node_id`: `[1, 2]` z sekcji `hal.axes[].can_node_id`.
4. Wywołuje `can_iface_->initialize()` → [`CanOpenInterface::initialize()`](../../src/controllers/canopen_interface.cpp:82) → `canopen_init()`.

### 1.3 Otwarcie gniazda SocketCAN

[`canopen_init()`](../../lib/canopen_wrapper/src/canopen.cpp:154):

- `socket(PF_CAN, SOCK_RAW, CAN_RAW)`,
- `ioctl(SIOCGIFINDEX)` aby znaleźć indeks `can0`,
- `bind()` do `sockaddr_can`,
- ustawienie `SO_RCVTIMEO`/`SO_SNDTIMEO` na `sdo_timeout_ms * 1000` µs (czyli 500 ms).

### 1.4 Konfiguracja per-oś w pętli

Dla każdej osi (0=HA, 1=Dec), [`canopen_hal.cpp:403`](../../src/hal/canopen_hal/canopen_hal.cpp:403):

1. Tworzy `CanOpenMotor` i `CanOpenEncoder`, zapisuje `motor_config`/`encoder_config`.
2. **Heartbeat** (gdy `enable_nmt=true`): [`setHeartbeatPeriod()`](../../src/controllers/canopen_interface.cpp:352) → zapis SDO `1017h:00h = 100` ms (producer heartbeat time).
3. **Mapowanie PDO** (gdy `pdo_config_enabled=true`): [`configurePdo()`](../../src/controllers/canopen_interface.cpp:408):
   - **TPDO1** (sterownik → master): komunikacja `1800h`, mapowanie `1A00h` z wpisami `6041h:00h` (status word, 16 bit) + `6064h:00h` (pozycja aktualna, 32 bit), COB-ID `0x180+node`, transmisja async (255), event timer 10 ms.
   - **RPDO2** (master → sterownik): komunikacja `1401h`, mapowanie `1601h` z wpisami `6040h:00h` (controlword, 16 bit) + `607Ah:00h` (pozycja docelowa, 32 bit), COB-ID `0x300+node`.

### 1.5 Załączenie napędów

Po utworzeniu komponentów HAL, `MountController` wywołuje `hal_axis_motor_->enable()` ([`mount_controller.cpp:388`](../../src/controllers/mount_controller.cpp:388)), co schodzi do [`CanOpenMotor::enable()`](../../src/hal/canopen_hal/canopen_hal.cpp:38) → [`CanOpenInterface::enableDrive()`](../../src/controllers/canopen_interface.cpp:178). To jest właściwa, programowa inicjalizacja CiA 402 dla każdej osi:

| Krok | Komenda | Obiekt | Wartość | Komentarz |
|---|---|---|---|---|
| 1 | NMT Start | COB-ID `0x000` | cmd `0x01`, node | STMP42SXI wykonuje ruch tylko w stanie Operational |
| 2 | SDO write (2B) | `6040h:00h` | `0x0000` | Disable voltage — wymagane przed zmianą trybu |
| 3 | SDO write (2B) | `2002h:01h` | `0` | tryb CiA 402 (nie NiMotion open-loop) |
| 4 | SDO write (1B) | `6060h:00h` | `1` | Profile Position (PP) |
| 5 | SDO write (2B) | `6040h:00h` | `6` | Shutdown |
| 6 | SDO write (2B) | `6040h:00h` | `7` | Switch On |
| 7 | SDO write (2B) | `6040h:00h` | `15` | Enable Operation |
| 8 | SDO read | `6041h:00h` | — | weryfikacja stanu: `(status & 0x6F) == 0x27` (Operation enabled) |

Między krokami program odczekuje 50–100 ms.

### 1.6 Uruchomienie monitoringu

[`CanOpenHAL::start()`](../../src/hal/canopen_hal/canopen_hal.cpp:470) uruchamia wątek [`monitorLoop()`](../../src/hal/canopen_hal/canopen_hal.cpp:519), który co `pdo_update_rate` (100 ms) odbiera heartbeat/EMCY/PDO i odczytuje status obu osi.

## 2. Ważna uwaga: sekcja `servo_init` w JSON

Sekcja [`servo_init`](../../config/canopen_stm42m.json:165) w pliku konfiguracyjnym jest **deklaratywna** — opisuje te same kroki (NMT start, `2002h:01h=0`, `608Fh:01h=131072`, `608Fh:02h=1`, `6060h=1`, `6040h 6→7→15`). Jednak **środowisko wykonawcze C++ jej nie czyta**: pole istnieje tylko w protobufie ([`proto/mount_controller.proto:695`](../../proto/mount_controller.proto:695)) i jest edytowalne w panelu WWW. Faktyczna inicjalizacja po każdym starcie jest wykonywana programowo przez [`enableDrive()`](../../src/controllers/canopen_interface.cpp:178) i [`CanOpenHAL::initialize()`](../../src/hal/canopen_hal/canopen_hal.cpp:376).

Różnica: JSON zapisuje `608Fh:01h=131072` i `608Fh:02h=1`, natomiast kod runtime tego nie robi (608Fh jest w STMP42SXI tylko do odczytu — patrz komentarze w [`canopen_encoder_resolution.sh`](../../scripts/canopen_encoder_resolution.sh:122)).

## 3. Lista komend CANopen

### 3.1 NMT (COB-ID `0x000`)

Definicje: [`canopen.h:53`](../../lib/canopen_wrapper/include/canopen/canopen.h:53).

| Komenda | Hex | Znaczenie |
|---|---|---|
| Start Remote Node | `0x01` | przejście do Operational |
| Stop Remote Node | `0x02` | przejście do Stopped |
| Enter Pre-Operational | `0x80` | Pre-Operational |
| Reset Node | `0x81` | restart węzła |
| Reset Communication | `0x82` | restart komunikacji |

### 3.2 SDO — command specifier (pierwszy bajt ramki)

Definicje: [`canopen.h:42`](../../lib/canopen_wrapper/include/canopen/canopen.h:42).

| Komenda | Hex | Znaczenie |
|---|---|---|
| Write 1 bajt | `0x2F` | download expedited 8-bit |
| Write 2 bajty | `0x2B` | download expedited 16-bit |
| Write 4 bajty | `0x23` | download expedited 32-bit |
| Read request | `0x40` | upload expedited |
| Read resp 1B / 2B / 4B | `0x4F`/`0x4B`/`0x43` | odpowiedź odczytu |
| Write response | `0x60` | potwierdzenie zapisu |
| Abort | `0x80` | błąd SDO |

COB-ID: żądanie `0x600 + node`, odpowiedź `0x580 + node`. Retry: 2 próby, backoff 50 ms ([`canopen.cpp:36`](../../lib/canopen_wrapper/src/canopen.cpp:36)).

### 3.3 Obiekty zapisywane/odczytywane podczas inicjalizacji (runtime)

| Obiekt | Typ | Wartość | Rola |
|---|---|---|---|
| `1017h:00h` | uint16 | 100 | producer heartbeat time |
| `6040h:00h` | uint16 | 0/6/7/15 | controlword (maszyna stanów CiA 402) |
| `2002h:01h` | uint16 | 0 | wybór trybu CiA 402 (CtrlModeSelec) |
| `6060h:00h` | uint8 | 1 | Modes of Operation = Profile Position |
| `6041h:00h` | uint16 (RO) | — | statusword (weryfikacja) |
| `1800h:01/02/05` | — | `0x180+node`, 255, 10 | parametry komunikacji TPDO1 |
| `1A00h:00/01/02` | — | 2, `60410010h`, `60640020h` | mapowanie TPDO1 |
| `1401h:01/02` | — | `0x300+node`, 255 | parametry komunikacji RPDO2 |
| `1601h:00/01/02` | — | 2, `60400010h`, `607A0020h` | mapowanie RPDO2 |

### 3.4 Komendy jednorazowej konfiguracji (skrypt setup)

[`canopen_setup_stmp42sxi.sh`](../../scripts/canopen_setup_stmp42sxi.sh:130) — dla każdego węzła, przed pierwszym użyciem CiA 402:

| Krok | Obiekt | Wartość |
|---|---|---|
| wyłącz napęd | `6040h:00h` | 0 |
| tryb CiA 402 | `2002h:01h` | 0 |
| pętla zamknięta | `2002h:02h` | 1 |
| przełożenie 1:1 (silnik) | `6091h:01h` | 1 |
| przełożenie 1:1 (wał) | `6091h:02h` | 1 |
| rozdzielczość enkodera | `608Fh:01h` / `608Fh:02h` | 131072 / 1 |
| tryb PP | `6060h:00h` | 1 |
| zapis EEPROM | `1010h:01h` | `0x65766173` ("save") |
| restart węzła | NMT `0x81` | — |

Ustawienie Node-ID realizuje [`canopen_set_node_id.sh`](../../scripts/canopen_set_node_id.sh:146): zapis `200Ch:02h` (adres osi, uint16) → `1010h:01h = save` → NMT reset `0x81`.

### 3.5 Komendy operacyjne (po inicjalizacji, tryb PP)

| Obiekt | Rola |
|---|---|
| `607Ah:00h` | pozycja docelowa (target position, int32) |
| `6081h:00h` | prędkość profilu (profile velocity) |
| `6083h:00h` | przyspieszenie (profile acceleration) |
| `6084h:00h` | hamowanie (profile deceleration) |
| `6040h:00h` | `0x0F` → `0x1F` (new setpoint, bit4) — start ruchu absolutnego |
| `60FFh:00h` | prędkość docelowa (tryb Profile Velocity) |
| `6098h–609Ah` | parametry homingu |

## 4. Podsumowanie przepływu

1. **Fabryka** tworzy `CanOpenInterface` dla `canopensocket`.
2. **HAL** otwiera gniazdo `can0` (1 Mbit/s, timeout 500 ms), buduje mapowanie osi→Node-ID, ustawia heartbeat i mapowanie PDO.
3. **MountController** załącza każdą oś: NMT Start → disable → `2002h:01h=0` (CiA 402) → `6060h=1` (PP) → `6040h` = 6→7→15 → weryfikacja `6041h`.
4. **Wątek monitorujący** odbiera heartbeat/PDO i cyklicznie czyta status osi.

Kontroler to NiMotion **STM42M** (moduł STMP42SXI, BLDC/PMSM z enkoderem 17-bitowym 131072 imp/obr). W konfiguracji i skryptach obie nazwy są używane zamiennie dla tego samego napędu.

## 5. Ustawianie parametrów rozdzielczości

Rozdzielczość pozycji jest zdefiniowana na **dwóch poziomach**: po stronie napędu (obiekty CANopen `608Fh` i `6091h`) oraz po stronie oprogramowania (współczynniki `counts_per_degree` w konfiguracji). Oba poziomy muszą być ze sobą spójne, aby stopnie → zliczenia i zliczenia → stopnie były przeliczane poprawnie.

### 5.1 Parametry po stronie napędu (CANopen)

**`608Fh` — Position encoder resolution** (rozdzielczość enkodera pozycji):

| Podobiekt | Nazwa | Typ | Znaczenie |
|---|---|---|---|
| `608Fh:01h` | Encoder increments | uint32 | liczba inkrementów enkodera na obrót silnika |
| `608Fh:02h` | Motor revolutions | uint32 | liczba obrotów silnika |

Rozdzielczość pozycji = `608Fh:01h / 608Fh:02h` [counts / obrót silnika]. Dla enkodera 17-bitowego: `131072 / 1 = 131072` imp/obr.

**`6091h` — Gear ratio** (przełożenie elektroniczne):

| Podobiekt | Nazwa | Typ | Znaczenie |
|---|---|---|---|
| `6091h:01h` | Motor revolutions | uint32 | obroty silnika |
| `6091h:02h` | Shaft revolutions | uint32 | obroty wału napędzanego |

Przełożenie = `6091h:01h / 6091h:02h`. W tym montażu ustawione na **1:1** (brak przekładni), więc pozycja w `607Ah` interpretowana jest bezpośrednio w inkrementach enkodera.

**Zależności** (z komentarza [`canopen_gear_ratio.sh`](../../scripts/canopen_gear_ratio.sh:11)):

- pozycja (enkoder) = pozycja (komenda) × przełożenie,
- prędkość silnika (obr/min) = prędkość wału × przełożenie × 60 / rozdzielczość enkodera.

**Efektywna rozdzielczość na obrót wału**:

`counts/obrót wału = (608Fh:01h / 608Fh:02h) × (6091h:01h / 6091h:02h) = (131072 / 1) × (1 / 1) = 131072`

### 5.2 Jak są zapisywane (SDO)

Wszystkie podobiekty są uint32, więc zapis odbywa się przez **SDO download expedited 4 bajty (command specifier `0x23`)**:

- żądanie: COB-ID `0x600 + node`, dane `23 8F 60 <sub> <b0> <b1> <b2> <b3>` (dla `608Fh`) lub `23 91 60 <sub> ...` (dla `6091h`),
- potwierdzenie: COB-ID `0x580 + node`, dane `60 ...`,
- odczyt: `40 8F 60 <sub> 00 00 00 00` → odpowiedź `43 8F 60 <sub> <b0..b3>` (little-endian).

### 5.3 Skrypty ustawiające rozdzielczość

1. **Jednorazowy setup** [`canopen_setup_stmp42sxi.sh`](../../scripts/canopen_setup_stmp42sxi.sh:162):

   | Obiekt | Wartość | Znaczenie |
   |---|---|---|
   | `6091h:01h` | 1 | obroty silnika |
   | `6091h:02h` | 1 | obroty wału |
   | `608Fh:01h` | 131072 | inkrementy enkodera (17-bit) |
   | `608Fh:02h` | 1 | obroty silnika |

   Po zapisach następuje zapis EEPROM (`1010h:01h = 0x65766173`) i NMT reset `0x81`.

2. **Dedykowany zapis `608Fh`** [`canopen_encoder_resolution.sh`](../../scripts/canopen_encoder_resolution.sh:42):
   - `--increments <n>` → zapis `608Fh:01h`,
   - `--revolutions <n>` → zapis `608Fh:02h`,
   - `--save` → zapis do EEPROM.
   - Po każdym zapisie skrypt robi **odczyt zwrotny i weryfikację** ([`write_608f()`](../../scripts/canopen_encoder_resolution.sh:122)).

3. **Dedykowany zapis `6091h`** [`canopen_gear_ratio.sh`](../../scripts/canopen_gear_ratio.sh:30):
   - `--motor-revs <n>` → zapis `6091h:01h`,
   - `--shaft-revs <n>` → zapis `6091h:02h`,
   - `--save` → zapis do EEPROM.
   - Analogicznie z odczytem zwrotnym ([`write_6091()`](../../scripts/canopen_gear_ratio.sh:138)).

4. **Tylko odczyt**: [`canopen_read_encoder_resolution.sh`](../../scripts/canopen_read_encoder_resolution.sh:84) i tryby odczytu obu powyższych skryptów raportują `608Fh`/`6091h` oraz wyliczają `counts/stopień = rozdzielczość / 360`.

### 5.4 Parametry po stronie oprogramowania (konfiguracja JSON)

W [`config/canopen_stm42m.json`](../../config/canopen_stm42m.json:69) rozdzielczość jest powtórzona w kilku miejscach:

| Miejsce | Pole | Wartość | Rola |
|---|---|---|---|
| `mount` | `encoder_resolution` | 131072 | deklaratywna rozdzielczość enkodera |
| `axis_physical_parameters.*` | `position_counts_per_degree` | 364.08888889 | 131072/360 — dla warstwy montażu |
| `axis_physical_parameters.*` | `encoder_resolution` | 131072.0 | rozdzielczość na obrót |
| `axis_physical_parameters.*` | `encoder_counts_per_arcsec` | 0.101136 | 131072/1296000 |
| `hal.axes[].motor_config` | `encoder_counts_per_degree` | 364.08888889 | **używany faktycznie przez HAL** do konwersji stopnie→zliczenia |
| `hal.axes[].encoder_config` | `counts_per_degree` | 364.08888889 | używany przez `CanOpenEncoder::read()` do zliczenia→stopnie |
| `hal.axes[].encoder_config` | `resolution` | 4000 | niespójne z enkoderem 17-bit (informacyjne, nieużywane do konwersji) |

Parsowanie tych pól do struktur HAL odbywa się w [`include/hal/hal_config.h:368`](../../include/hal/hal_config.h:368) (`encoder_counts_per_degree`) oraz [`include/hal/hal_config.h:391`](../../include/hal/hal_config.h:391) (`resolution`, `counts_per_degree`).

### 5.5 Użycie współczynników w kodzie

- **Zadawanie pozycji** [`CanOpenMotor::setPosition()`](../../src/hal/canopen_hal/canopen_hal.cpp:56): `target = position_deg × encoder_counts_per_degree` (→ zapis `607Ah`).
- **Prędkość/przyspieszenie** [`degPerSecToCounts()`](../../src/hal/canopen_hal/canopen_hal.cpp:22): `value × counts_per_degree` (→ `6081h`/`6083h`/`6084h`).
- **Odczyt statusu** [`CanOpenMotor::updateStatus()`](../../src/hal/canopen_hal/canopen_hal.cpp:204): `actual_position = counts / counts_per_degree`.
- **Odczyt enkodera** [`CanOpenEncoder::read()`](../../src/hal/canopen_hal/canopen_hal.cpp:245): `position_deg = counts / config_.counts_per_degree + calibration_offset_`.

### 5.6 Podsumowanie ustawiania rozdzielczości

1. **Raz na sprzęcie** (skrypt setup lub dedykowane skrypty): zapisz `608Fh:01h=131072`, `608Fh:02h=1`, `6091h:01h=1`, `6091h:02h=1`, potem `1010h:01h=save` i NMT reset.
2. **W konfiguracji oprogramowania**: ustaw `encoder_counts_per_degree` / `counts_per_degree = 364.08888889` (= 131072/360), aby przeliczniki w HAL były zgodne z fizyczną rozdzielczością napędu.
3. **Sekwencja `servo_init`** w JSON zawiera wpisy `608Fh`, ale nie jest wykonywana przez kod C++ — rozdzielczość po stronie sprzętu musi być ustawiona skryptami przed pierwszym uruchomieniem, a w kodzie liczy się wyłącznie współczynnik `counts_per_degree`.
