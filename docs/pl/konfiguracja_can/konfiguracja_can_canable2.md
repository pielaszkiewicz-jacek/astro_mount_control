# Konfiguracja konwertera CAN-USB CANable2 (RH02 Plus) — openSUSE i Orange Pi

> Dokument opisuje konfigurację konwertera USB↔CAN **CANable2** (sprzedawanego również jako **RH02 Plus**), identyfikowanego jako `16d0:117e` („MCS CANable2"). Urządzenie działa z oprogramowaniem **slcan** — jest widoczne w systemie jako port szeregowy `/dev/ttyACM*`, a nie jako natywny interfejs SocketCAN (`gs_usb`). Konfiguracja jest wymagana do uruchomienia [`astro-mount-controller`](../../../README.md) z napędami CANopen STM42M.

---

## Spis treści

- [Konfiguracja konwertera CAN-USB CANable2 (RH02 Plus) — openSUSE i Orange Pi](#konfiguracja-konwertera-can-usb-canable2-rh02-plus--opensuse-i-orange-pi)
  - [Spis treści](#spis-treści)
  - [1. Identyfikacja urządzenia](#1-identyfikacja-urządzenia)
  - [2. Wymagania](#2-wymagania)
  - [3. Stabilna ścieżka urządzenia](#3-stabilna-ścieżka-urządzenia)
    - [3.1 openSUSE — ścieżka `by-id`](#31-opensuse--ścieżka-by-id)
    - [3.2 Orange Pi — `/dev/ttyACM*` (wymagany moduł `slcan`)](#32-orange-pi--devttyacm-wymagany-moduł-slcan)
  - [4. Konfiguracja doraźna (testowa)](#4-konfiguracja-doraźna-testowa)
  - [5. Konfiguracja trwała (systemd)](#5-konfiguracja-trwała-systemd)
    - [5.1 Skrypt startowy](#51-skrypt-startowy)
    - [5.2 Jednostka systemd](#52-jednostka-systemd)
  - [6. Weryfikacja działania](#6-weryfikacja-działania)
  - [7. Konfiguracja astro-mount-controller](#7-konfiguracja-astro-mount-controller)
  - [8. Różnice openSUSE vs Orange Pi](#8-różnice-opensuse-vs-orange-pi)
    - [8.1 Orange Pi — budowa modułu jądra `slcan`](#81-orange-pi--budowa-modułu-jądra-slcan)
  - [9. Rozwiązywanie problemów](#9-rozwiązywanie-problemów)

---

## 1. Identyfikacja urządzenia

Po podłączeniu konwerter zgłasza się jako:

```bash
lsusb | grep -i 16d0
# Bus 010 Device 028: ID 16d0:117e MCS CANable2 b158aa7 github.com/normaldotcom/canable2.git
```

W trybie **slcan** urządzenie jest portem szeregowym (sterownik `cdc_acm`):

```bash
ls -la /dev/ttyACM*
# crw-rw---- 1 root dialout 166, 0 ... /dev/ttyACM0
```

Numer `/dev/ttyACM*` **zmienia się po każdym przełożeniu wtyczki** (`ttyACM0` → `ttyACM1` → `ttyACM2` …). Dlatego do konfiguracji trwałej używa się stabilnej ścieżki `by-id` (openSUSE) albo reguły udev (Orange Pi) — patrz sekcja [3](#3-stabilna-ścieżka-urządzenia).

> Inny tryb pracy — **candleLight** (natywny `gs_usb`, interfejs `can0` bez `slcand`) — wymaga wgrania innego firmware. Ten dokument opisuje tryb **slcan**, w którym urządzenie jest dostarczane domyślnie.

---

## 2. Wymagania

Potrzebne są narzędzia `slcand` (demona slcan w przestrzeni użytkownika) oraz `candump`/`cansend`:

**openSUSE:**

```bash
sudo zypper install can-utils
```

**Orange Pi / Debian / Ubuntu:**

```bash
sudo apt install can-utils
```

Sprawdzenie:

```bash
command -v slcand candump cansend
# /usr/bin/slcand
# /usr/bin/candump
# /usr/bin/cansend
```

> `slcand` (podobnie jak `slcan_attach`) wymaga **modułu jądra `slcan`** — dyscypliny linii `N_SLCAN`. Na openSUSE moduł jest dostępny i ładowany automatycznie. Na Orange Pi moduł ten **nie istnieje** (jądro `6.6.98-sun60iw2`), dlatego tryb slcan na Orange Pi **nie działa** — patrz sekcja [8](#8-różnice-opensuse-vs-orange-pi).

---

## 3. Stabilna ścieżka urządzenia

### 3.1 openSUSE — ścieżka `by-id`

openSUSE tworzy stabilne dowiązanie w `/dev/serial/by-id`:

```bash
ls -la /dev/serial/by-id/
# usb-Openlight_Labs_CANable2_b158aa7_...-if00 -> ../../ttyACM0
```

Użyj dokładnie tej ścieżki w konfiguracji — nie zależy ona od numeru `ttyACM*`.

### 3.2 Orange Pi — `/dev/ttyACM*` (wymagany moduł `slcan`)

Na Orange Pi CANable2 również działa przez `slcand` i port `/dev/ttyACM*`, ale najpierw trzeba **zbudować moduł jądra `slcan`** (sekcja [8.1](#81-orange-pi--budowa-modułu-jądra-slcan)). Numer `ttyACM*` może się zmieniać po przełożeniu wtyczki — sprawdź `ls /dev/ttyACM*`.

---

## 4. Konfiguracja doraźna (testowa)

Poniższe komendy uruchamiają `can0` na 1 Mbit/s do czasu ponownego uruchomienia (openSUSE).

```bash
# 1. Usuń ewentualne stare procesy i interfejs
sudo pkill -x slcand
sudo ip link del can0 2>/dev/null

# 2. Uruchom slcand (ścieżka by-id):
sudo slcand -o -c -f -s8 "/dev/serial/by-id/usb-Openlight_Labs_CANable2_b158aa7_github.com_normaldotcom_canable2.git_365E33653036-if00" can0

# 3. Podnieś interfejs
sudo ip link set can0 up

# 4. Sprawdź
ip -details link show can0
```

> Sekcja dotyczy **openSUSE**. Na Orange Pi tryb slcan nie działa (brak modułu `slcan`) — wgraj firmware **candleLight** (sekcja [8.1](#81-orange-pi--firmware-candlelight)).

Poprawny wynik:

```
can0: <NOARP,UP> ... state UP
    can state ERROR-ACTIVE restart-ms 0
    bitrate 1000000
    [ 10000, 20000, 50000, 100000, 125000, 250000, 500000, 800000, 1000000 ]
```

Znaczenie przełączników `slcand`:

| Przełącznik | Znaczenie |
|---|---|
| `-o` | wyślij komendę otwarcia kanału (`O`) |
| `-c` | wyślij komendę zamknięcia (`C`) — reset poprzedniego stanu |
| `-f` | odczytaj flagi (`F`) — reset liczników błędów |
| `-s8` | prędkość CAN = **8** = 1 Mbit/s |
| `-F` | nie dajmonizuj — pozostań na pierwszym planie (wymagane w systemd) |

Tabela prędkości `-s`: `0`=10k, `1`=20k, `2`=50k, `3`=100k, `4`=125k, `5`=250k, `6`=500k, `7`=800k, `8`=1M.

---

## 5. Konfiguracja trwała (systemd)

`slcand` **domyślnie dajmonizuje się** (forkuje). Dlatego w systemd nie można użyć `Type=simple` z gołym `slcand` — proces-rodzic kończy się natychmiast, systemd zabija dajmona i usługa wpada w pętlę restartów. Poprawne rozwiązanie: uruchomić `slcand -F` (pierwszy plan) z poziomu skryptu.

### 5.1 Skrypt startowy

**openSUSE** — ścieżka `by-id`:

```bash
sudo tee /usr/local/bin/canable2-slcand.sh > /dev/null << 'EOF'
#!/bin/sh
TTY="/dev/serial/by-id/usb-Openlight_Labs_CANable2_b158aa7_github.com_normaldotcom_canable2.git_365E33653036-if00"
slcand -F -o -c -f -s8 "$TTY" can0 &
SLCAND_PID=$!
sleep 2
ip link set can0 up
trap 'ip link set can0 down 2>/dev/null; kill "$SLCAND_PID" 2>/dev/null' TERM INT
wait "$SLCAND_PID"
EOF
sudo chmod +x /usr/local/bin/canable2-slcand.sh
```

> Sekcja dotyczy **openSUSE**. Na Orange Pi tryb slcan nie działa (brak modułu `slcan`) — wgraj firmware **candleLight** (sekcja [8.1](#81-orange-pi--firmware-candlelight)).

### 5.2 Jednostka systemd

```bash
sudo tee /etc/systemd/system/canable2-slcand.service > /dev/null << 'EOF'
[Unit]
Description=CANable2 slcan (can0 @ 1 Mbit/s)
After=multi-user.target

[Service]
Type=simple
ExecStart=/usr/local/bin/canable2-slcand.sh
ExecStop=/bin/sh -c 'ip link set can0 down 2>/dev/null; pkill -x slcand'
Restart=on-failure
RestartSec=2

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable --now canable2-slcand.service
```

Weryfikacja:

```bash
systemctl status canable2-slcand.service --no-pager | head -8
# Active: active (running)   ← nie „activating (auto-restart)"
ip -details link show can0
```

---

## 6. Weryfikacja działania

Napędy STM42M po włączeniu zasilania wysyłają **heartbeaty** CANopen (COB-ID `0x700 + Node-ID`):

```bash
candump can0
```

Oczekiwany wynik (Node 1 = HA, Node 2 = Dec, stan `7F` = Pre-Operational — normalny przed startem mastera):

```
can0  701   [1]  7F
can0  702   [1]  7F
can0  701   [1]  7F
can0  702   [1]  7F
```

Test komunikacji w obu kierunkach (NMT Start + odczyt SDO `0x1000` typ urządzenia):

```bash
# w drugim terminalu zostaw działający candump can0, następnie:
cansend can0 000#0101
cansend can0 000#0102
cansend can0 601#4000100000000000
cansend can0 602#4000100000000000
```

Zdrowy konwerter + zasilone napędy zwrócą:

```
can0  701   [1]  05                    ← napęd przyjął NMT Start (Operational)
can0  581   [8]  43 00 10 00 ...       ← odpowiedź SDO od Node 1
can0  582   [8]  43 00 10 00 ...       ← odpowiedź SDO od Node 2
```

Jeśli heartbeaty pozostają `7F`, a odpowiedzi `581`/`582` nie ma — konwerter nie nadaje (uszkodzony tor TX) albo błędne podłączenie `CAN_H`/`CAN_L`/`GND`.

---

## 7. Konfiguracja astro-mount-controller

Konfiguracja montażu [`config/mount/canopen_stm42m.json`](../../../config/mount/canopen_stm42m.json) używa `can0` i 1 Mbit/s — nie wymaga zmian:

```json
"canopen": {
    "library": "canopensocket",
    "interface_name": "can0",
    "bitrate": 1000000,
    "node_id": 1
}
```

Uruchomienie sterownika:

```bash
cd /home/jacek/astro-mount-controller   # openSUSE (lokalnie)
# lub
cd /home/astro/astro-mount-controller   # Orange Pi

nohup ./bin/astro_mount_controller ./config/mount/canopen_stm42m.json >> ./logs/mount.log 2>&1 &
sleep 12
grep -E "Enabling HAL|Failed to enable|initialized successfully" ./logs/mount.log | tail -8
```

Sukces: `Mount controller initialized successfully` i **brak** wpisów `Failed to enable axis ...`.

---

## 8. Różnice openSUSE vs Orange Pi

| Aspekt | openSUSE | Orange Pi |
|---|---|---|
| Obsługa CANable2 | slcan (`slcand`) — moduł `slcan` w jądrze | slcan (`slcand`) — po zbudowaniu modułu `slcan` |
| Interfejs CAN | tworzony przez `slcand` (`can0`) | tworzony przez `slcand` (`can0`) |
| Dodatkowy krok | `zypper install can-utils` | jednorazowa kompilacja modułu `slcan` (sekcja 8.1) |

> **Orange Pi — uwaga**: jądro `6.6.98-sun60iw2` nie zawiera modułu `slcan`, więc CANable2 w trybie slcan początkowo nie działa — `slcand` zgłasza `ioctl TIOCSETD: Invalid argument`. Moduł trzeba zbudować z nagłówków jądra (są zainstalowane: `/usr/src/linux-headers-6.6.98-sun60iw2`).

> **CANable2 a candleLight**: CANable2 używa MCU **STM32G431**, którego firmware `candleLight` (gs_usb) **nie obsługuje** (patrz README `candle-usb/candleLight_fw`). Dlatego dla CANable2 jedyną drogą jest tryb slcan — na Orange Pi wymaga zbudowania modułu `slcan`.

### 8.1 Orange Pi — budowa modułu jądra `slcan`

Wymagania są już spełnione: nagłówki jądra (`/usr/src/linux-headers-6.6.98-sun60iw2`), `gcc`, `make` oraz nagłówki podsystemu CAN (`include/linux/can/`).

1. Pobierz źródła `slcan` zgodne z jądrem 6.6:

   ```bash
   mkdir -p /root/slcan-build && cd /root/slcan-build
   curl -sL -o slcan-core.c    https://raw.githubusercontent.com/torvalds/linux/v6.6/drivers/net/can/slcan/slcan-core.c
   curl -sL -o slcan-ethtool.c https://raw.githubusercontent.com/torvalds/linux/v6.6/drivers/net/can/slcan/slcan-ethtool.c
   curl -sL -o slcan.h         https://raw.githubusercontent.com/torvalds/linux/v6.6/drivers/net/can/slcan/slcan.h
   ```

2. Utwórz `Makefile`:

   ```makefile
   obj-m := slcan.o
   slcan-y := slcan-core.o slcan-ethtool.o

   all:
   	make -C /lib/modules/6.6.98-sun60iw2/build M=/root/slcan-build modules

   clean:
   	make -C /lib/modules/6.6.98-sun60iw2/build M=/root/slcan-build clean
   ```

3. Zbuduj i zainstaluj moduł:

   ```bash
   make
   mkdir -p /lib/modules/6.6.98-sun60iw2/kernel/drivers/net/can/slcan
   cp slcan.ko /lib/modules/6.6.98-sun60iw2/kernel/drivers/net/can/slcan/
   depmod -a
   modprobe slcan
   lsmod | grep slcan
   ```

4. Uruchom `slcand` i podnieś `can0` — sekcja [4](#4-konfiguracja-doraźna-testowa).

Aby moduł ładował się przy starcie systemu:

```bash
echo "slcan" | sudo tee /etc/modules-load.d/canable2.conf
```

---

## 9. Rozwiązywanie problemów

| Objaw | Przyczyna | Rozwiązanie |
|---|---|---|
| `/dev/ttyACM0: No such file or directory` | numer `ttyACM*` zmienił się po przełożeniu wtyczki | użyj ścieżki `by-id` (openSUSE) lub reguły udev `/dev/canable2` (Orange Pi); sprawdź `ls /dev/ttyACM*` |
| `can0` istnieje, ale `state DOWN`, `bitrate 0` | interfejs nie został podniesiony po starcie `slcand` | `sudo ip link set can0 up` |
| `systemctl restart` → `activating (auto-restart)` | `Type=simple` + dajmonizujący `slcand` (rodzic wychodzi, systemd zabija dajmona) | użyj `slcand -F` w skrypcie startowym (sekcja 5) |
| `read: Network is down` w `candump` | interfejs DOWN | `sudo ip link set can0 up` |
| kilka procesów `slcand` (stare `ttyACM0`, `ttyACM1`) | stare daemony trzymają nazwę `can0` | `sudo pkill -x slcand && sudo ip link del can0` i uruchom ponownie |
| heartbeaty `7F`, brak odpowiedzi SDO, `can0` przechodzi w `BUS-OFF` | napędy nie potwierdzają ramek (brak zasilania, przerwane `CAN_H`/`CAN_L`/`GND`, brak terminacji, uszkodzony tor TX konwertera) | sprawdź zasilanie napędów i okablowanie; włącz terminację 120 Ω na obu końcach magistrali; przetestuj inny konwerter |
| `cansend` zwraca błąd zapisu | interfejs w stanie `BUS-OFF` | `sudo ip link set can0 down && sudo ip link set can0 up` |
| `ioctl TIOCSETD: Invalid argument` przy `slcand` | brak modułu jądra `slcan` (Orange Pi) | zbuduj moduł `slcan` (sekcja 8.1); na openSUSE moduł jest dostępny w jądrze |

---

**Zobacz także**:

- [`konfiguracja_can_orange_pi_4_pro.md`](konfiguracja_can_orange_pi_4_pro.md) — konfiguracja `can0` na Orange Pi 4 Pro (adapter gs_usb/U2C),
- [`konfiguracja_can_u2c_opensuse.md`](konfiguracja_can_u2c_opensuse.md) — konfiguracja adaptera Lawicel U2C na openSUSE,
- [`inicjalizacja_canopen_stm42m.md`](../inicjalizacja_canopen_stm42m.md) — inicjalizacja napędów CANopen STM42M.
