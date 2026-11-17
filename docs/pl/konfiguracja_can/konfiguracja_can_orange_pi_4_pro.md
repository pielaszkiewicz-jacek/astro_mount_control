# Konfiguracja CAN na Orange Pi 4 Pro (vendor kernel `sun60iw2`) — pełna procedura

> Dokument opisuje szczegółowo, jak uruchomić magistralę CAN z konwerterem USB–CAN `gs_usb` (adapter „budgetcan gs_usb", VID/PID `1d50:606f`) na **Orange Pi 4 Pro** z fabrycznym obrazem Orange Pi (Ubuntu 26.04 „Resolute", jądro `6.6.98-sun60iw2`).
>
> Fabryczne jądro tego obrazu **nie ma w ogóle obsługi SocketCAN** (`# CONFIG_CAN is not set`), dlatego standardowe kroki z [`konfiguracja_can_u2c_ubuntu_debian.md`](konfiguracja_can_u2c_ubuntu_debian.md) kończą się błędem:
>
> ```text
> modprobe: FATAL: Module can not found in directory /lib/modules/6.6.98-sun60iw2
> ```
>
> Poniższa procedura opisuje **przebudowę jądra z obsługą CAN bezpośrednio na Orange Pi** oraz trwałą konfigurację interfejsu `can0`.

---

## Spis treści

- [Konfiguracja CAN na Orange Pi 4 Pro (vendor kernel `sun60iw2`) — pełna procedura](#konfiguracja-can-na-orange-pi-4-pro-vendor-kernel-sun60iw2--pełna-procedura)
  - [Spis treści](#spis-treści)
  - [1. Diagnostyka wstępna](#1-diagnostyka-wstępna)
  - [2. Przygotowanie systemu budowania](#2-przygotowanie-systemu-budowania)
  - [3. Poprawki w `orangepi-build` dla Ubuntu 26.04](#3-poprawki-w-orangepi-build-dla-ubuntu-2604)
    - [3.1 `scripts/general.sh`](#31-scriptsgeneralsh)
    - [3.2 `scripts/main.sh` — ograniczenie równoległości kompilacji](#32-scriptsmainsh--ograniczenie-równoległości-kompilacji)
    - [3.3 Obejście kontroli wersji hosta](#33-obejście-kontroli-wersji-hosta)
  - [4. Włączenie opcji CAN w konfiguracji jądra](#4-włączenie-opcji-can-w-konfiguracji-jądra)
  - [5. Budowanie jądra](#5-budowanie-jądra)
  - [6. Naprawa pakietów `.deb` (build przy `-j1`)](#6-naprawa-pakietów-deb-build-przy--j1)
  - [7. Instalacja nowego jądra](#7-instalacja-nowego-jądra)
  - [8. Weryfikacja po restarcie](#8-weryfikacja-po-restarcie)
  - [9. Trwała konfiguracja `can0` (usługa systemd)](#9-trwała-konfiguracja-can0-usługa-systemd)
  - [10. Test działania](#10-test-działania)
  - [11. Napotkane problemy i rozwiązania](#11-napotkane-problemy-i-rozwiązania)
  - [12. Uwagi dodatkowe](#12-uwagi-dodatkowe)

---

## 1. Diagnostyka wstępna

```bash
# Tożsamość płyty
uname -r                                   # 6.6.98-sun60iw2
cat /etc/os-release                        # Ubuntu 26.04 (Resolute)
cat /etc/orangepi-release                  # BOARD=orangepi4pro, BOARDFAMILY=sun60iw2

# Stan obsługi CAN w fabrycznym jądrze
grep CONFIG_CAN /boot/config-$(uname -r)   # # CONFIG_CAN is not set

# Adapter USB
lsusb | grep -i can
# Bus 003 Device 004: ID 1d50:606f OpenMoko, Inc. Geschwister Schneider CAN adapter

dmesg | grep -i "Product:"
# usb 3-1.3: Product: budgetcan gs_usb
# usb 3-1.3: Manufacturer: budgetcan
```

**Wniosek:** `lsusb` widzi urządzenie (enumeracja USB), ale `# CONFIG_CAN is not set` oznacza, że jądro nie ma wbudowanej obsługi SocketCAN. Bez przebudowy jądra interfejs `can0` **nigdy się nie pojawi**. Adapter `1d50:606f` jest urządzeniem referencyjnym sterownika `gs_usb` — po przebudowie jądra zadziała od razu, bez żadnych `quirks` ani `new_id`.

---

## 2. Przygotowanie systemu budowania

Budujemy **natywnie na Orange Pi** (architektura `arm64`), więc nie jest potrzebny żaden cross-kompilator ani Docker. Wymagane narzędzia:

```bash
sudo apt update
sudo apt install -y git build-essential bc bison flex libssl-dev libncurses-dev rsync

cd /root
git clone --depth 1 https://github.com/orangepi-xunlong/orangepi-build.git
cd orangepi-build
```

Dodatkowo potrzebny będzie `ccache` (patrz sekcja [5](#5-budowanie-jądra)):

```bash
sudo apt install -y ccache
```

> Kompilator `gcc-aarch64-linux-gnu` jest już zainstalowany na obrazie; `ccache` pochodzi z repozytorium `universe` (domyślnie włączonego na tym obrazie).

---

## 3. Poprawki w `orangepi-build` dla Ubuntu 26.04

System `orangepi-build` wspiera hosty Ubuntu 22.04 (`jammy`) / 24.04 (`noble`) i odmawia pracy na „resolute". Dodatkowo lista zależności hosta zawiera pakiety, które nie istnieją już w Ubuntu 26.04 (`libpython2.7-dev`, `python3-distutils`, `ntpdate`, `liblz4-tool`, `python`, `libpython-dev`). Wprowadzamy trzy poprawki.

### 3.1 `scripts/general.sh`

Zamień wielowierszową definicję `local hostdeps="..."` na minimalny zestaw pakietów dostępnych w `main`:

```bash
local hostdeps="bc bison build-essential ca-certificates cpio curl device-tree-compiler dwarves fakeroot flex gawk git kmod libelf-dev libncurses-dev libssl-dev make perl pkg-config python3 rsync u-boot-tools xz-utils zlib1g-dev"
```

Następnie popraw gałąź `arm64` (usuwamy niepotrzebne cross-kompilatory z `universe`) oraz gałąź Pythona:

```bash
# arm64: zamiast cross-kompilatorów zostaje tylko libc6
hostdeps+=" libc6"

# python: zamiast usuniętych pakietów python/libpython-dev
hostdeps+=" python3"
```

Skrypt realizujący obie poprawki (do uruchomienia w katalogu `orangepi-build`):

```bash
python3 << 'PYEOF'
import re
p = 'scripts/general.sh'
s = open(p).read()

new_hostdeps = (
    'local hostdeps="bc bison build-essential ca-certificates cpio curl '
    'device-tree-compiler dwarves fakeroot flex gawk git kmod libelf-dev '
    'libncurses-dev libssl-dev make perl pkg-config python3 rsync '
    'u-boot-tools xz-utils zlib1g-dev"\n'
)
pat = re.compile(r'local hostdeps=".*?"\n', re.S)
s, n = pat.subn(lambda m: new_hostdeps, s, count=1)
assert n == 1, 'hostdeps block not found'

s = s.replace(
    'hostdeps+=" gcc-arm-linux-gnueabi gcc-arm-none-eabi libc6 libc6-amd64-cross qemu"',
    'hostdeps+=" libc6"'
)
s = s.replace('hostdeps+=" python libpython-dev"', 'hostdeps+=" python3"')

open(p, 'w').write(s)
print('patched general.sh')
PYEOF
```

### 3.2 `scripts/main.sh` — ograniczenie równoległości kompilacji

Orange Pi 4 Pro ma 8 rdzeni, ale tylko 3,7 GB RAM. Domyślne `-j12` grozi OOM. Ograniczamy do `-j6`:

```bash
python3 << 'PYEOF'
p = 'scripts/main.sh'
s = open(p).read()
s = s.replace('CTHREADS="-j$((CPUS + CPUS/2))"', 'CTHREADS="-j6"')
open(p, 'w').write(s)
print('patched main.sh')
PYEOF
```

### 3.3 Obejście kontroli wersji hosta

Kontrolę wersji systemu hosta pomijamy zmienną środowiskową `NO_HOST_RELEASE_CHECK=yes` (przekazywaną przy uruchomieniu `build.sh`).

---

## 4. Włączenie opcji CAN w konfiguracji jądra

Plik konfiguracji dla Orange Pi 4 Pro (rodzina `sun60iw2`, gałąź `current`):

```text
external/config/kernel/linux-sun60iw2-current-a733.config
```

```bash
cd /root/orangepi-build
KCFG="external/config/kernel/linux-sun60iw2-current-a733.config"
cp "$KCFG" "$KCFG.bak"

sed -i 's/^# CONFIG_CAN is not set/CONFIG_CAN=y/' "$KCFG"

grep -q '^CONFIG_CAN='        "$KCFG" || echo 'CONFIG_CAN=y'        >> "$KCFG"
grep -q '^CONFIG_CAN_RAW='    "$KCFG" || echo 'CONFIG_CAN_RAW=y'    >> "$KCFG"
grep -q '^CONFIG_CAN_DEV='    "$KCFG" || echo 'CONFIG_CAN_DEV=y'    >> "$KCFG"
grep -q '^CONFIG_CAN_GS_USB=' "$KCFG" || echo 'CONFIG_CAN_GS_USB=m' >> "$KCFG"
```

> `CONFIG_CAN`, `CONFIG_CAN_RAW` i `CONFIG_CAN_DEV` jako wbudowane (`=y`), `CONFIG_CAN_GS_USB` jako moduł (`=m`). Po uruchomieniu `olddefconfig` jądro domyślnie dołącza również `CONFIG_CAN_BCM=y` i `CONFIG_CAN_GW=y`.

---

## 5. Budowanie jądra

```bash
cd /root/orangepi-build
NO_HOST_RELEASE_CHECK=yes NO_APT_CACHER=yes SYNC_CLOCK=no \
  ./build.sh BOARD=orangepi4pro BRANCH=current BUILD_OPT=kernel KERNEL_CONFIGURE=no
```

- Czas budowy: **ok. 53 min** (natywnie, 8 rdzeni, `-j6`).
- Podczas pierwszego uruchomienia build system instaluje zależności hosta i klonuje źródła jądra (`orange-pi-6.6-sun60iw2`).

**Znany błąd w trakcie kompilacji (nieszkodliwy dla CAN):**

```text
make[1]: /aarch64-linux-gnu-gcc: No such file or directory
... bsp/modules/gpu ...
make: *** [Makefile:154: build] Error 2
```

Moduł GPU (`pvrsrvkm.ko`) nie zostanie zbudowany, ponieważ skrypt [`compilation.sh`](https://github.com/orangepi-xunlong/orangepi-build/blob/master/scripts/compilation.sh) (linia 529) przekazuje ścieżkę `$toolchain/$KERNEL_COMPILER` z pustą zmienną `toolchain` (powstaje `/aarch64-linux-gnu-gcc`). **Nie ma to wpływu na CAN ani na start systemu** — dotyczy wyłącznie akceleracji 3D. Naprawa opisana w sekcji [12](#12-uwagi-dodatkowe).

---

## 6. Naprawa pakietów `.deb` (build przy `-j1`)

Domyślny krok pakowania (`make bindeb-pkg` z `-j6`) kończy się błędem wyścigu przy instalowaniu drzew urządzeń:

```text
install: cannot create directory 'debian/tmp/usr/lib/linux-image-current-sun60iw2/allwinner'
make[8]: *** [scripts/Makefile.dtbinst:33: .../sun50i-a64-amarula-relic.dtb] Error 1
```

Rozwiązanie: ponownie wygenerować pakiety z `-j1` (kompilacja jest już ukończona, więc trwa to tylko kilka minut):

```bash
cd /root/orangepi-build/kernel/orange-pi-6.6-sun60iw2
make -j1 bindeb-pkg \
  ARCH=arm64 KBUILD_DEBARCH=arm64 \
  KDEB_PKGVERSION=1.1.0 BRANCH=current LOCALVERSION=-sun60iw2 \
  CROSS_COMPILE="ccache aarch64-linux-gnu-"
```

Wynikowe pakiety pojawią się w katalogu nadrzędnym `/root/orangepi-build/kernel/`:

```text
linux-image-current-sun60iw2_1.1.0_arm64.deb
linux-dtb-current-sun60iw2_1.1.0_arm64.deb
linux-headers-current-sun60iw2_1.1.0_arm64.deb
linux-image-current-sun60iw2-dbg_1.1.0_arm64.deb
```

> `BRANCH=current` jest wymagane, aby nazwa pakietu brzmiała `linux-image-current-sun60iw2` (zgodnie z zainstalowanym fabrycznie pakietem). Bez tego powstaje błędna nazwa `linux-image--sun60iw2`.

---

## 7. Instalacja nowego jądra

```bash
cd /root/orangepi-build/kernel
dpkg -i \
  linux-image-current-sun60iw2_1.1.0_arm64.deb \
  linux-dtb-current-sun60iw2_1.1.0_arm64.deb \
  linux-headers-current-sun60iw2_1.1.0_arm64.deb

reboot
```

Skrypt `postinst` pakietu `linux-image` automatycznie:

1. regeneruje `initrd` (`update-initramfs`) i konwertuje go do formatu u-boot (`uInitrd`),
2. regeneruje `/boot/uImage` z nowego `vmlinux-6.6.98-sun60iw2` (`mkimage`),
3. tworzy `/boot/.next`.

> Ostrzeżenia `mkinitramfs: copy_file: binary '...usb_modeswitch...' not found` są niegroźne — initrd powstaje poprawnie.

---

## 8. Weryfikacja po restarcie

```bash
uname -r                                  # 6.6.98-sun60iw2
grep CONFIG_CAN /boot/config-$(uname -r)
# CONFIG_CAN=y
# CONFIG_CAN_RAW=y
# CONFIG_CAN_BCM=y
# CONFIG_CAN_GW=y

ls /sys/class/net/                        # can0 powinien się pojawić
lsmod | grep gs_usb                       # gs_usb 20480 0

dmesg | grep -iE "gs_usb|can "
# CAN device driver interface
# NET: Registered PF_CAN protocol family
# usb 3-1.3: Product: budgetcan gs_usb
# gs_usb 3-1.3:1.0: Configuring for 1 interfaces
# usbcore: registered new interface driver gs_usb
```

Sterownik `gs_usb` wiąże adapter automatycznie (udev ładuje moduł po dopasowaniu modalias), więc `can0` pojawia się bez ręcznego `modprobe`.

---

## 9. Trwała konfiguracja `can0` (usługa systemd)

Ponieważ rdzeń CAN jest wbudowany w jądro, a `gs_usb` ładuje się sam, wystarczy usługa, która ustawia bitrate i podnosi interfejs.

`/etc/systemd/system/can-setup.service`:

```ini
[Unit]
Description=Configure CAN bus (gs_usb/U2C adapter) on can0 at 1 Mbit/s
After=network.target
Wants=network.target

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/bin/sh -c 'modprobe gs_usb 2>/dev/null || true; for i in $(seq 1 20); do ip link show can0 >/dev/null 2>&1 && break; sleep 0.5; done; ip link set can0 down 2>/dev/null || true; ip link set can0 type can bitrate 1000000 2>/dev/null || true; ip link set can0 up'
ExecStop=/bin/sh -c 'ip link set can0 down 2>/dev/null || true'
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

Aktywacja:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now can-setup.service
systemctl is-enabled can-setup.service   # enabled
systemctl is-active  can-setup.service   # active
```

Bitrate **1 Mbit/s** wynika z konfiguracji montażu: [`canopen_stm42m.json`](../../../config/mount/canopen_stm42m.json:124) oraz [`canopen.json`](../../../config/mount/canopen.json:225) (`"bitrate": 1000000`).

---

## 10. Test działania

```bash
# Stan interfejsu
ip -details link show can0
# 8: can0: <NOARP,UP,LOWER_UP,ECHO> mtu 16 ... state UP
#     can state ERROR-ACTIVE restart-ms 0
#     bitrate 1000000 sample-point 0.750
#     gs_usb: tseg1 1..16 tseg2 1..8 sjw 1..4 brp 1..1024 brp_inc 1
#     clock 64000000 ... parentbus usb parentdev 3-1.3:1.0

# Test pętli zwrotnej (bez zewnętrznej magistrali)
ip link set can0 down
ip link set can0 type can bitrate 1000000 loopback on
ip link set can0 up

candump can0 > /tmp/candump.txt 2>&1 &
sleep 0.5
cansend can0 123#DEADBEEF
sleep 1
cat /tmp/candump.txt
# can0  123   [4]  DE AD BE EF

# Powrót do trybu normalnego
ip link set can0 down
ip link set can0 type can bitrate 1000000 loopback off
ip link set can0 up
```

> `state ERROR-ACTIVE` przy braku podłączonych napędów / terminacji jest normalne. `NO-CARRIER` (gdy interfejs jest bez ruchu na magistrali) również jest normalne.

---

## 11. Napotkane problemy i rozwiązania

| Objaw | Przyczyna | Rozwiązanie |
|-------|-----------|-------------|
| `modprobe: FATAL: Module can not found ...` | Jądro bez `CONFIG_CAN` | Przebudowa jądra (sekcje 3–7) |
| `unsupported build system: resolute` | `orangepi-build` nie zna Ubuntu 26.04 | `NO_HOST_RELEASE_CHECK=yes` |
| `C compiler 'ccache aarch64-linux-gnu-gcc' not found` | Brak `ccache` (jest w `universe`) | `apt install -y ccache` |
| `install: cannot create directory 'debian/tmp/.../allwinner'` | Wyścig w `dtbs_install` przy `-j` | Pakowanie przy `-j1` (sekcja 6) |
| `/aarch64-linux-gnu-gcc: No such file or directory` (moduł GPU) | Pusta zmienna `toolchain` w `compilation.sh` | Nieszkodliwe dla CAN; naprawa w sekcji 12 |
| `mkinitramfs: copy_file: ... usb_modeswitch ... not found` | Brak pakietu `usb-modeswitch` w obrazie | Niegroźne ostrzeżenie; initrd powstaje |

---

## 12. Uwagi dodatkowe

- **Adapter**: „budgetcan gs_usb" (VID/PID `1d50:606f`) jest w pełni obsługiwany przez standardowy sterownik `gs_usb` — nie są potrzebne `new_id` ani `quirks`.
- **Moduł GPU** (`pvrsrvkm.ko`) nie został zbudowany. Naprawa: w [`compilation.sh`](https://github.com/orangepi-xunlong/orangepi-build/blob/master/scripts/compilation.sh) (linia 529) ustawić `LICHEE_TOOLCHAIN_PATH=/usr/bin` (zamiast pustego `$toolchain`) i ponownie uruchomić krok modułu GPU. Dotyczy tylko akceleracji 3D — start systemu, DRM/HDMI i CAN działają bez tego modułu.
- **Artefakty** pozostały na Orange Pi w `/root/orangepi-build/` (źródła jądra, konfiguracja z backupem `.bak`, pakiety `.deb` w `/root/orangepi-build/kernel/`), co umożliwia szybką przebudowę lub reinstalację.
- Przy wielu adapterach CAN można dodać regułę udev przypisującą nazwę `can0` do konkretnego VID/PID — analogicznie do [`konfiguracja_can_u2c_ubuntu_debian.md`](konfiguracja_can_u2c_ubuntu_debian.md) (sekcja 5.4).

---

**Zobacz także**:

- [`konfiguracja_can_u2c_ubuntu_debian.md`](konfiguracja_can_u2c_ubuntu_debian.md) — konfiguracja CAN na standardowym Ubuntu/Debian (z jądrem z obsługą CAN),
- [`inicjalizacja_canopen_stm42m.md`](../inicjalizacja_canopen_stm42m.md) — inicjalizacja napędów CANopen STM42M,
- [`installation.md`](../installation.md) — instalacja `astro-mount-controller`.
