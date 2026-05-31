# Konfiguracja magistrali CAN na Fedora/RHEL z konwerterem U2C

> Dokument opisuje krok po kroku konfigurację magistrali CAN (Controller Area Network) w systemach **Fedora**, **Red Hat Enterprise Linux (RHEL)** oraz pochodnych (CentOS, Rocky Linux, AlmaLinux) przy użyciu konwertera USB–CAN Lawicel U2C (lub kompatybilnego). Konfiguracja jest niezbędna do uruchomienia [`astro-mount-controller`](index.md) z rzeczywistymi napędami CANopen.

---

## Spis treści

1.  [Wprowadzenie](#1-wprowadzenie)
2.  [Wymagania sprzętowe](#2-wymagania-sprzętowe)
3.  [Instalacja sterowników i narzędzi](#3-instalacja-sterowników-i-narzędzi)
4.  [Konfiguracja interfejsu CAN (doraźna)](#4-konfiguracja-interfejsu-can-doraźna)
5.  [Trwała konfiguracja (przy starcie systemu)](#5-trwała-konfiguracja-przy-starcie-systemu)
6.  [Weryfikacja działania](#6-weryfikacja-działania)
7.  [Konfiguracja astro-mount-controller](#7-konfiguracja-astro-mount-controller)
8.  [Rozwiązywanie problemów](#8-rozwiązywanie-problemów)
9.  [Zaawansowane — parametry czasowe U2C](#9-zaawansowane--parametry-czasowe-u2c)
10. [Dodatek — mapa pinów U2C](#10-dodatek--mapa-pinów-u2c)

---

## 1. Wprowadzenie

**Lawicel U2C** (USB‑to‑CAN) to popularny, gotowy konwerter USB ↔ CAN 2.0B, kompatybilny z implementacją **SocketCAN** w jądrze Linux. Dla użytkownika systemu Linux urządzenie jest widziane jako interfejs sieciowy (np. `can0`), co pozwala korzystać z całego ekosystemu narzędzi CAN (can-utils, wireshark, a przede wszystkim stosów CANopen jak [`CANopenSocket`](https://github.com/CANopenNode/CANopenSocket) czy `lely-canopen`).

Systemy Fedora/RHEL domyślnie używają:

- **NetworkManager** (z `nmcli` / GUI) — domyślnego narzędzia do zarządzania interfejsami sieciowymi, zarządzanego przez `systemd`,
- **firewalld** — zapory sieciowej,
- **systemd** — systemu inicjalizacji i zarządzania usługami,
- **SELinux** — domyślnie włączony (w trybie enforcing na RHEL/Fedora), co może wpływać na działanie niektórych narzędzi.

**Konsekwencje dla CAN:**

- NetworkManager **nie ma natywnej obsługi interfejsów CAN** i może próbować zarządzać `can0` jak zwykłym interfejsem sieciowym — należy go wyłączyć dla CAN przez `unmanaged-devices` (sekcja [5.1](#51-wyłączenie-can0-w-networkmanager))
- **systemd-networkd** ma **wbudowaną obsługę CAN** (`Kind=can`) i jest zalecanym rozwiązaniem dla trwałej konfiguracji — jednak na Fedora/RHEL **nie jest domyślnie włączony**, ponieważ domyślnym backendem jest NetworkManager
- **NetworkManager + niestandardowa usługa systemd** to praktyczne rozwiązanie dla użytkowników desktopowych Fedory (sekcja [5.3](#53-opcja-b-niestandardowa-usługa-systemd))
- **nmcli** może być użyte do wyłączenia zarządzania `can0` bez wyłączania NetworkManagera dla innych interfejsów
- **SELinux** domyślnie nie blokuje SocketCAN, ale może blokować dostęp aplikacji do interfejsów CAN, jeśli działają z niewłaściwym kontekstem — w razie problemów sprawdź sekcję [8.5](#85-selinux-blocking-dostępu-do-can)

Poniższy przewodnik został przygotowany dla **Fedora 39/40/41 oraz RHEL 9.x**, ale ogólne zasady dotyczą również CentOS Stream, Rocky Linux i AlmaLinux.

---

## 2. Wymagania sprzętowe

### 2.1 Konwerter U2C

Lawicel U2C (lub klon) to konwerter USB → CAN 2.0B z izolacją galwaniczną. Dostępne warianty:

| Model | Interfejs hosta | Złącze CAN | Izolacja |
|-------|-----------------|------------|----------|
| U2C‑A (mini) | USB 2.0 (micro‑USB) | 3‑pin JST‑PH (CAN H, CAN L, GND) | ✅ 1 kV |
| U2C‑B | USB 2.0 | 5‑pin screw terminal | ✅ 1 kV |
| U2C‑E (Ethernet) | Ethernet 100Base‑T | 5‑pin screw terminal | ✅ 1 kV |

> **Uwaga**: Wiele tanich klonów (np. z AliExpress, oznaczonych jako „USB‑CAN U2C") również działa z modułem `gs_usb`, ale mogą mieć gorszą izolację lub brak zgodności z trybem ``SJA1000`` — w razie problemów z prędkościami >500 kbit/s sprawdź sekcję [Rozwiązywanie problemów](#8-rozwiązywanie-problemów).

### 2.2 Okablowanie CAN

Do poprawnej komunikacji wymagane są **dwa przewody sygnałowe** oraz **wspólna masa**:

| Sygnał | Kolor (zwyczajowy) | Opis                             |
|--------|--------------------|----------------------------------|
| CAN H  | Żółty              | Dominanta wysoka (2,5 V → 3,5 V) |
| CAN L  | Zielony            | Dominanta niska (2,5 V → 1,5 V)  |
| GND    | Czarny             | Masa odniesienia                  |

**Zasada działania**:

- Na końcach magistrali CAN muszą znajdować się **terminatory** 120 Ω między CAN H a CAN L.
- Długość magistrali przy 1 Mbit/s nie powinna przekraczać ~40 m; dla 250 kbit/s — ~250 m.
- Wspólna masa (GND) między konwerterem a napędami znacząco redukuje błędy ramek.

### 2.3 Zgodność z Linux

Układ U2C oparty jest na mikrokontrolerze z firmware zgodnym z protokołem **Lawicel SLCAN** (Serial‑Line CAN). Linux rozpoznaje go przez moduł jądra:

```
gs_usb      — Generic CAN USB driver
```

Identyfikatory USB (dla oryginalnego Lawicel U2C):

```
VID=0x1b7d  (Lawicel)
PID=0x0001  (U2C)
```

Klonów może być więcej — w razie problemów sprawdź `lsusb` i ewentualnie załaduj moduł `gs_usb` z opcją `quirks`.

---

## 3. Instalacja sterowników i narzędzi

### 3.1 Instalacja pakietów systemowych

```bash
# ── Narzędzia CAN (can-utils) ──
sudo dnf install -y can-utils

# ── Narzędzia diagnostyczne (opcjonalne) ──
sudo dnf install -y         \
    wireshark               \   # analiza ramek CAN
    iproute                 \   # ip link, ip -details (już zainstalowane)
    usbutils                    # lsusb
```

> **Uwaga**: Na RHEL 9.x może być konieczne włączenie repozytorium `epel` (Extra Packages for Enterprise Linux) dla pakietów takich jak `can-utils`:
> ```bash
> sudo dnf install -y epel-release
> sudo dnf install -y can-utils
> ```

Jeśli budujesz [`astro-mount-controller`](installation.md) ze źródeł, dodatkowo:

```bash
sudo dnf install -y         \
    gcc-c++ gcc make cmake   \
    git pkg-config           \
    openssl-devel            \
    protobuf-devel protobuf-compiler \
    grpc-devel grpc-cpp-devel \
    nlohmann-json-devel      \
    eigen3-devel             \
    fmt-devel spdlog-devel   \
    gtest-devel              \
    sqlite-devel
```

### 3.2 Ładowanie modułów jądra

Moduł `gs_usb` jest wbudowany w standardowe jądro Fedora/RHEL. Załaduj go ręcznie:

```bash
sudo modprobe can
sudo modprobe can_raw
sudo modprobe can_dev
sudo modprobe gs_usb
```

**Sprawdzenie, czy moduły są dostępne**:

```bash
lsmod | grep can
# Oczekiwany wynik (przykład):
# can_raw              36864  0
# can                  81920  1 can_raw
# gs_usb               28672  0
# can_dev              16384  1 gs_usb
```

Aby moduły ładowały się automatycznie po restarcie:

```bash
sudo tee /etc/modules-load.d/can.conf << 'EOF'
can
can_raw
can_dev
gs_usb
EOF
```

### 3.3 Podłączenie konwertera U2C

Podłącz U2C przez USB. Sprawdź, czy został rozpoznany:

```bash
# ── Identyfikacja urządzenia USB ──
lsusb | grep -i "Lawicel\|U2C\|CAN"
# Przykładowy wynik:
# Bus 001 Device 003: ID 1b7d:0001 Lawicel U2C CAN adapter

# ── Czy pojawił się interfejs sieciowy? ──
ip link show
# Powinieneś zobaczyć: can0 (lub can1, can2...)
# can0: <NO-CARRIER,BROADCAST,UP> ...
```

Jeśli interfejs `can0` **nie pojawił się**:

```bash
# Sprawdź logi jądra
dmesg | tail -20
# Szukaj komunikatów: "gs_usb: signed", "gs_usb: probe", "CAN device driver interface"

# Jeśli urządzenie ma inny VID/PID, wymuś załadowanie gs_usb:
sudo modprobe gs_usb
# lub dodaj identyfikatory do modułu:
sudo sh -c 'echo "1b7d 0001" > /sys/bus/usb/drivers/usb/new_id'
```

---

## 4. Konfiguracja interfejsu CAN (doraźna)

Przed trwałą konfiguracją przetestuj działanie, konfigurując interfejs ręcznie.

### 4.1 Ustawienie prędkości i włączenie interfejsu

```bash
# ── Ustaw typ interfejsu CAN i prędkość (np. 1 Mbit/s) ──
sudo ip link set can0 type can bitrate 1000000

# ── Włącz interfejs ──
sudo ip link set can0 up
```

Jeśli potrzebujesz innej prędkości (często używane w astronomii to 125 kbit/s, 250 kbit/s, 500 kbit/s, 1 Mbit/s):

```bash
sudo ip link set can0 type can bitrate 250000
sudo ip link set can0 up
```

### 4.2 Sprawdzenie stanu interfejsu

```bash
ip -details link show can0
```

Oczekiwany wynik:

```
2: can0: <NO-CARRIER,BROADCAST,UP> mtu 16 qdisc pfifo_fast state DOWN ...
    link/can
    can <BERR-REPORTING> state ERROR-ACTIVE restart-ms 100
    bitrate 1000000 sample-point 0.750
    tq 125 prop-seg 6 phase-seg1 7 phase-seg2 2 sjw 1
    ...
```

> **`NO-CARRIER`** przy braku podłączonej magistrali (lub wyłączonych zasilaniach napędów) jest normalne — oznacza, że warstwa fizyczna nie wykrywa dominujących sygnałów.

### 4.3 Test pętli wewnętrznej (loopback)

```bash
# ── Konfiguracja w trybie loopback (bez potrzeby zewnętrznej magistrali) ──
sudo ip link set can0 type can bitrate 1000000 loopback on
sudo ip link set can0 up

# ── Wysłanie testowej ramki ──
cansend can0 123#DEADBEEF

# ── Odbior ramki (powinieneś zobaczyć echo) ──
candump can0
```

Aby wrócić do trybu normalnego:

```bash
sudo ip link set can0 type can bitrate 1000000 loopback off
sudo ip link set can0 up
```

---

## 5. Trwała konfiguracja (przy starcie systemu)

Fedora/RHEL domyślnie używają **NetworkManagera**. Poniżej trzy opcje trwałej konfiguracji — wybierz odpowiednią dla swojego środowiska.

### 5.1 Wyłączenie can0 w NetworkManager

Niezależnie od wybranej opcji konfiguracji CAN, **najpierw** wyłącz zarządzanie `can0` przez NetworkManager:

```bash
sudo tee /etc/NetworkManager/conf.d/90-can-unmanaged.conf << 'EOF'
[keyfile]
unmanaged-devices=interface-name:can0
EOF

sudo systemctl restart NetworkManager
```

Można też użyć `nmcli`:

```bash
sudo nmcli device set can0 managed no
```

### 5.2 Opcja A: systemd-networkd (zalecana dla serwerów)

**systemd-networkd** ma wbudowaną obsługę interfejsów CAN przez `Kind=can`. Na Fedora/RHEL nie jest domyślnie włączony, ale można go aktywować:

```bash
# ── Wyłącz NetworkManager (opcjonalnie, jeśli chcesz w pełni przejść na networkd) ──
# sudo systemctl disable NetworkManager
# sudo systemctl stop NetworkManager

# ── Włącz systemd-networkd ──
sudo systemctl enable --now systemd-networkd

# ── Utwórz konfigurację CAN netdev ──
sudo tee /etc/systemd/network/10-can0.netdev << 'EOF'
[NetDev]
Name=can0
Kind=can

[CAN]
BitRate=1M
EOF

# ── Utwórz konfigurację CAN network ──
sudo tee /etc/systemd/network/10-can0.network << 'EOF'
[Match]
Name=can0

[CAN]
BitRate=1000000
EOF

# ── Restart network ──
sudo systemctl restart systemd-networkd
```

> **Uwaga**: Jeśli pozostawiasz NetworkManager aktywny dla innych interfejsów (WiFi, Ethernet), upewnij się, że sekcja [5.1](#51-wyłączenie-can0-w-networkmanager) została wykonana.

### 5.3 Opcja B: niestandardowa usługa systemd

Jeśli używasz NetworkManagera na desktopie Fedora i nie chcesz przełączać się na systemd-networkd, utwórz samodzielną usługę systemd:

> **Uwaga**: Usługa poniżej **załadowuje niezbędne moduły jądra** przed konfiguracją interfejsu. Jest to konieczne, ponieważ `After=network.target` nie gwarantuje, że moduły CAN są już załadowane.

```bash
sudo tee /etc/systemd/system/can-setup.service << 'EOF'
[Unit]
Description=Konfiguracja magistrali CAN z konwerterem U2C
After=network.target

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/bin/sh -c '\
    modprobe can && \
    modprobe can_raw && \
    modprobe can_dev && \
    modprobe gs_usb && \
    for i in $(seq 1 10); do \
        if ip -details link show can0 >/dev/null 2>&1; then \
            BITRATE=$(ip -details link show can0 2>/dev/null | grep -o "bitrate [0-9]*" | cut -d" " -f2); \
            STATE=$(ip -details link show can0 2>/dev/null | grep -o "state [A-Z-]*" | cut -d" " -f2); \
            if [ "$STATE" = "UP" ] && [ "$BITRATE" = "1000000" ]; then \
                echo "CAN0 już skonfigurowany: bitrate $BITRATE, state $STATE — pomijam"; \
                exit 0; \
            fi; \
            if [ "$STATE" = "UP" ]; then \
                ip link set can0 down; \
            fi; \
            if ip link set can0 type can bitrate 1000000 2>/dev/null; then \
                ip link set can0 up && \
                echo "CAN0 skonfigurowany: bitrate 1000000, state UP" && \
                exit 0; \
            else \
                echo "Uwaga: nie można zmienić bitrate (U2C może być już skonfigurowany przez inny mechanizm). Próbuję podnieść istniejący interfejs..." && \
                ip link set can0 up 2>/dev/null && \
                echo "CAN0 podniesiony (bieżący bitrate: ${BITRATE:-nieznany})" && \
                exit 0; \
            fi; \
        fi; \
        sleep 0.5; \
    done; \
    echo "Błąd: interfejs can0 nie pojawił się po 5s - sprawdź czy U2C jest podłączony" >&2; \
    exit 1'
ExecStop=/bin/sh -c '\
    ip link set can0 down 2>/dev/null; \
    exit 0'
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable --now can-setup.service
```

**Jak działa ta usługa:**

| Krok | Polecenie | Opis |
|------|-----------|------|
| 1 | `modprobe can` .. `gs_usb` | Ładuje moduły jądra dla CAN |
| 2 | Pętla `for i in $(seq 1 10)` | Czeka do 5 sekund aż `can0` się pojawi (U2C potrzebuje chwili na inicjalizację USB) |
| 3 | `ip -details link show can0` | Sprawdza czy interfejs istnieje i odczytuje bieżący bitrate/stan |
| 4 | Warunek: UP + bitrate=1000000 | Jeśli już skonfigurowany poprawnie — pomija (zapobiega konfliktowi z NetworkManager) |
| 5 | `ip link set can0 down` | Wyłącza interfejs przed zmianą bitrate |
| 6 | `ip link set can0 type can bitrate ...` | Próbuje ustawić żądaną prędkość |
| 7 | Fallback: `ip link set can0 up` | Jeśli zmiana bitrate się nie powiodła — podnosi istniejący interfejs z domyślnym bitrate |

### 5.4 Reguła udev dla statycznej nazwy interfejsu

Aby mieć pewność, że konwerter U2C zawsze pojawi się jako `can0` (szczególnie przy wielu kartach CAN):

```bash
sudo tee /etc/udev/rules.d/99-u2c-can.rules << 'EOF'
# Lawicel U2C - przypisz nazwę can0
SUBSYSTEM=="net", ACTION=="add", ATTRS{idVendor}=="1b7d", ATTRS{idProduct}=="0001", NAME="can0"

# Dla klonów - odkomentuj i dostosuj VID/PID:
# SUBSYSTEM=="net", ACTION=="add", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="55d4", NAME="can0"
EOF

sudo udevadm control --reload-rules
sudo udevadm trigger
```

---

## 6. Weryfikacja działania

### 6.1 Podstawowe testy

```bash
# ── Status interfejsu ──
ip -details link show can0
# can0: <NO-CARRIER,BROADCAST,UP> ... state UP
# bitrate 1000000 sample-point 0.750

# ── Liczniki błędów ──
cat /sys/class/net/can0/stats
# rx_packets: 0  tx_packets: 0  rx_errors: 0  tx_errors: 0

# ── Stan kontrolera CAN ──
cat /sys/class/net/can0/can_state
# ERROR-ACTIVE
```

### 6.2 Test komunikacji z napędem CANopen

Jeśli masz podłączony napęd CANopen (np. serwonapęd zgodny z CiA 402):

```bash
# ── Nasłuch wszystkich ramek ──
candump can0 &

# ── Wyślij NMT Start Remote Node (ID=0 = wszystkie węzły) ──
cansend can0 000#01

# ── Sprawdź czy napęd odpowiedział bootup (ID=701 → 700+node_id) ──
# Oczekiwana ramka: 701#00 (jeśli node_id=1)
# Candump powinien pokazać: can0 701 [1] 00

# ── Odczytaj statusword napędu (SDO Read, node_id=1, index=0x6041) ──
cansend can0 601#40 41 60 00 00 00 00 00
# Oczekiwana odpowiedź (SDO Write Response):
# can0 581#xx xx xx xx 00 00 00 00  (gdzie xx to statusword)
```

### 6.3 Test wydajności (obciążenie magistrali)

```bash
# ── Generator ramek testowych (1000 ramek) ──
cangen can0 -L 8 -g 0 -n 1000 -v

# ── Sprawdź czy nie pojawiły się błędy ──
ip -s -details link show can0
# Sprawdź pole: rx-errors, tx-errors — powinny być 0
```

---

## 7. Konfiguracja astro-mount-controller

Szczegółowa konfiguracja kontrolera znajduje się w dokumentach:

- [`installation.md`](installation.md) — instalacja i budowanie,
- [`testowanie_i_uruchamianie.md`](testowanie_i_uruchamianie.md) — tryby uruchomienia,
- [`hal_layer.md`](hal_layer.md) — warstwa HAL (w tym CANopen HAL).

Poniżej przykładowa konfiguracja JSON dla CANopen z U2C:

```json
{
  "hal": {
    "interface_type": "CANopen",
    "can_interface": "can0",
    "can_node_id": 1,
    "can_baud_rate": 1000000,
    "heartbeat_interval_ms": 1000
  },
  "canopen": {
    "library": "canopensocket",
    "interface_name": "can0",
    "bitrate": 1000000,
    "node_id": 1,
    "sync_interval_ms": 100,
    "nmt": {
      "enable_nmt": true,
      "heartbeat_producer": true,
      "heartbeat_consumer": true,
      "heartbeat_period_ms": 1000,
      "heartbeat_timeout_ms": 3000,
      "bootup_timeout_ms": 5000,
      "enable_auto_recovery": true,
      "recovery_interval_s": 5
    }
  },
  "mount": {
    "type": "equatorial",
    "max_slew_rate": 3.0,
    "tracking_acceleration": 0.5
  }
}
```

**Najważniejsze parametry**:

| Parametr | Wartość dla U2C | Uwagi |
|----------|----------------|-------|
| `can_interface` | `can0` | Nazwa interfejsu SocketCAN |
| `can_baud_rate` / `bitrate` | `1000000` | 1 Mbit/s (maks. dla U2C) |
| `can_node_id` | `1` | ID węzła CANopen kontrolera |
| `sync_interval_ms` | `100` | 10 Hz — zgodne z typowym cyklem PDO |

> **Uwaga**: Nie wszystkie konwertery U2C (zwłaszcza klony) stabilnie pracują przy 1 Mbit/s. Jeśli występują błędy ramek, obniż prędkość do 500 kbit/s lub 250 kbit/s.

---

## 8. Rozwiązywanie problemów

### 8.1 `can0` nie pojawia się po podłączeniu U2C

**Przyczyny**:

- Moduł `gs_usb` nie jest załadowany.
- Urządzenie ma nieobsługiwany VID/PID.
- Konflikt z innym sterownikiem (np. `cp210x` dla UART).

**Diagnostyka**:

```bash
# Czy moduły są załadowane?
lsmod | grep can

# Czy USB jest widoczne?
lsusb | grep -i "1b7d\|Lawicel\|CAN"

# Logi jądra
dmesg | grep -i "gs_usb\|can"
```

**Rozwiązanie**:

```bash
# Wymuś załadowanie
sudo modprobe gs_usb

# Jeśli urządzenie ma inny PID, dodaj je do gs_usb:
sudo sh -c 'echo "1b7d 0001" > /sys/bus/usb/drivers/usb/new_id'

# Jeśli to klon z CH340/CH341 (np. VID=1a86):
sudo modprobe gs_usb
sudo sh -c 'echo "1a86 55d4" > /sys/bus/usb/drivers/usb/new_id'
```

### 8.2 Błędy ramek (rx-errors / tx-errors)

**Objaw**: `ip -s link show can0` pokazuje rosnące `rx-errors`.

**Przyczyny**:

- Zbyt wysoka prędkość transmisji dla długości kabla.
- Brak terminatora 120 Ω.
- Zakłócenia EMI (kabel CAN biegnący równolegle do przewodów zasilających).
- Różne potencjały masy (GND) między urządzeniami.

**Rozwiązanie**:

```bash
# Obniż prędkość
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 250000
sudo ip link set can0 up

# Włącz raportowanie błędów (BERR)
sudo ip link set can0 type can bitrate 1000000 berr-reporting on

# Podgląd ramek błędów
candump can0,0x000~0x080
```

### 8.3 `Permission denied` dla interfejsu can0

```bash
# Dodaj użytkownika do grupy can (jeśli istnieje)
sudo groupadd -f can
sudo usermod -a -G can $USER
newgrp can

# Lub nadaj uprawnienia 666 (mniej bezpieczne)
sudo chmod 666 /dev/can0
```

Dla trwałego rozwiązania — reguła udev:

```bash
sudo tee /etc/udev/rules.d/99-can-permissions.rules << 'EOF'
# Nadaj grupie can dostęp do interfejsów CAN
ACTION=="add", SUBSYSTEM=="net", KERNEL=="can*", RUN+="/bin/sh -c 'chown :can /sys/class/net/%k && chmod 660 /sys/class/net/%k'"
EOF
```

### 8.4 U2C nie odpowiada — migający LED

Lawicel U2C ma diodę LED sygnalizującą stan:

| LED | Znaczenie |
|-----|-----------|
| Ciągły zielony | Interfejs skonfigurowany i włączony (`UP`) |
| Miga zielony | Transmisja/odbiór ramek |
| Miga czerwony | Błędy ramek (np. brak terminatora, zła prędkość) |
| Ciągły czerwony | Błąd sprzętowy (odłącz i podłącz ponownie) |

### 8.5 SELinux blokujący dostęp do CAN

**Objaw**: Aplikacja typu `astro-mount-controller` nie może otworzyć interfejsu CAN, mimo poprawnych uprawnień.

**Diagnostyka**:

```bash
# Sprawdź logi SELinux
sudo ausearch -m avc -ts recent | grep can
# lub
sudo journalctl | grep -i "selinux\|avc.*can"
```

**Rozwiązanie**:

```bash
# Tymczasowe wyłączenie SELinux (dla testów):
sudo setenforce 0
# Przetestuj działanie; jeśli problem zniknął — SELinux blokuje dostęp.

# Trwałe rozwiązanie — utwórz regułę zezwalającą:
sudo ausearch -m avc -ts recent -c can-setup | tee /tmp/can-avc.log
# Wygeneruj i załaduj regułę:
# (wymaga pakietu policycoreutils-python-utils)
sudo grep "avc" /tmp/can-avc.log | audit2allow -M can_setup
sudo semodule -i can_setup.pp

# Przywróć SELinux enforcing:
sudo setenforce 1
```

### 8.6 Niska stabilność przy 1 Mbit/s

**Objaw**: Sporadyczne błędy CRC, `state BUS-OFF`.

**Przyczyny**:

- Klon U2C z gorszym transceiverem (np. SN65HVD230 zamiast ISO1050).
- Zbyt długa magistrala (>40 m przy 1 Mbit/s).
- Zbyt mała liczba punktów próbkowania (sample point).

**Rozwiązanie**:

```bash
# Dostosuj sample point dla dłuższych kabli
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 1000000 sample-point 0.800
sudo ip link set can0 up

# Lub obniż prędkość do 500 kbit/s
sudo ip link set can0 type can bitrate 500000
```

---

## 9. Zaawansowane — parametry czasowe U2C

Konwerter U2C używa **trybu SJA1000** (domyślny dla `gs_usb`). Można dostroić parametry czasowe dla niestandardowych prędkości lub dłuższych magistral.

### 9.1 Obliczanie parametrów czasowych

```bash
# Narzędzie do obliczania parametrów dla SocketCAN
# (część can-utils)
can-calc-bit-timing can0 1000000
```

Przykładowe wyniki dla popularnych prędkości:

| Bitrate | tq | Prop‑Seg | Phase‑Seg1 | Phase‑Seg2 | SJW | Sample point |
|---------|----|----------|------------|------------|-----|--------------|
| 1 Mbit/s | 125 ns | 6 | 7 | 2 | 1 | 75,0 % |
| 500 kbit/s | 250 ns | 6 | 7 | 2 | 1 | 75,0 % |
| 250 kbit/s | 500 ns | 6 | 7 | 2 | 1 | 75,0 % |
| 125 kbit/s | 1000 ns | 6 | 7 | 2 | 1 | 75,0 % |

Ręczne ustawienie:

```bash
sudo ip link set can0 down
sudo ip link set can0 type can \
    tq 125 \
    prop-seg 6 \
    phase-seg1 7 \
    phase-seg2 2 \
    sjw 1
sudo ip link set can0 up
```

### 9.2 Tryb FD (CAN FD) — Uwaga

**Oryginalny Lawicel U2C nie obsługuje CAN FD**. Jeśli potrzebujesz CAN FD, rozważ:

- **Lawicel U2C‑FD** — nowszy model z obsługą CAN FD do 8 Mbit/s.
- **PCAN‑USB FD** — karta Peak Systems z obsługą CAN FD.
- **Kvaser USBcan Pro FD** — profesjonalny interfejs CAN FD.

Klonów U2C z CAN FD praktycznie nie ma na rynku.

---

## 10. Dodatek — mapa pinów U2C

### 10.1 Lawicel U2C‑A (mini, złącze JST‑PH 3‑pin)

```
Pin 1 (czerwony) — CAN H
Pin 2 (biały)    — CAN L
Pin 3 (czarny)   — GND
```

### 10.2 Lawicel U2C‑B (złącze screw terminal 5‑pin)

```
1 — CAN H
2 — CAN L
3 — CAN GND
4 — (NC) niepodłączony
5 — (NC) niepodłączony / ekran
```

### 10.3 Podłączenie do napędu CANopen (CiA 303‑1, DR‑303)

Standardowe złącze napędów CANopen (DSub‑9, zgodne z CiA 303‑1):

| Pin DSub‑9 | Sygnał   | U2C‑A (JST) | U2C‑B (screw) |
|-----------|----------|-------------|---------------|
| 2         | CAN L    | Biały       | Terminal 2    |
| 3         | GND      | Czarny      | Terminal 3    |
| 7         | CAN H    | Czerwony    | Terminal 1    |

> **Ważne**: Przewód ekranowany (pin DSub 9 → obudowa/ekran kabla) podłącz tylko z jednej strony, aby uniknąć pętli masy.

### 10.4 Terminator 120 Ω

Jeśli napęd nie ma wbudowanego terminatora, podłącz rezystor 120 Ω między CAN H a CAN L na **obu końcach** magistrali. W warunkach amatorskich można użyć przejściówki DSub‑9 z terminatorem:

```
CAN H ──╮
        ├── 120 Ω ──┐
CAN L ──╯           │
                    ═══
GND ────────────────╯
```

---

## Podsumowanie

Konfiguracja konwertera U2C na Fedora/RHEL sprowadza się do kilku kroków:

1. **Podłącz** konwerter USB — moduł `gs_usb` automatycznie utworzy interfejs `can0`.
2. **Skonfiguruj** prędkość: `sudo ip link set can0 type can bitrate 1000000 && sudo ip link set can0 up`.
3. **Zweryfikuj**: `ip -details link show can0` → stan `UP`, brak błędów.
4. **Utwórz trwałą konfigurację** — przez systemd-networkd lub własną usługę systemd.
5. **Wyłącz NetworkManager dla can0** przez `unmanaged-devices` (sekcja 5.1).
6. **Skonfiguruj** [`astro-mount-controller`](installation.md) z parametrami `can_interface: can0`, `bitrate: 1000000`.
7. W razie problemów z dostępem — **sprawdź SELinux** (sekcja 8.5).

Po wykonaniu tych kroków magistrala CAN będzie gotowa do komunikacji z napędami CANopen (RA, Dec, derotator) zgodnie z profilem CiA 402.

---

**Zobacz także**:

- [`testowanie_i_uruchamianie.md`](testowanie_i_uruchamianie.md) — uruchamianie kontrolera z CANopen,
- [`hal_layer.md`](hal_layer.md) — szczegóły implementacji CANopen HAL,
- [`alternatywy_dla_canopen.md`](alternatywy_dla_canopen.md) — porównanie protokołów komunikacji,
- [`przeplyw_danych.md`](przeplyw_danych.md) — przepływ danych w systemie z magistralą CAN,
- [`konfiguracja_can_u2c_opensuse.md`](konfiguracja_can_u2c_opensuse.md) — wersja dla openSUSE,
- [`konfiguracja_can_u2c_ubuntu_debian.md`](konfiguracja_can_u2c_ubuntu_debian.md) — wersja dla Ubuntu/Debian,
- [`konfiguracja_can_u2c_arch.md`](konfiguracja_can_u2c_arch.md) — wersja dla Arch Linux.
