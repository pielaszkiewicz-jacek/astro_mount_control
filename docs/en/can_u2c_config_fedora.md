# CAN Bus Configuration on Fedora/RHEL with U2C Converter

> This document describes step-by-step configuration of the CAN (Controller Area Network) bus on **Fedora**, **Red Hat Enterprise Linux (RHEL)** and derivatives (CentOS, Rocky Linux, AlmaLinux) using the Lawicel U2C USB-to-CAN converter (or compatible). This configuration is required to run [`astro-mount-controller`](index.md) with real CANopen drives.

---

## Table of Contents

1.  [Introduction](#1-introduction)
2.  [Hardware Requirements](#2-hardware-requirements)
3.  [Driver and Tool Installation](#3-driver-and-tool-installation)
4.  [Temporary CAN Interface Configuration](#4-temporary-can-interface-configuration)
5.  [Persistent Configuration (at System Boot)](#5-persistent-configuration-at-system-boot)
6.  [Verification](#6-verification)
7.  [Configuring astro-mount-controller](#7-configuring-astro-mount-controller)
8.  [Troubleshooting](#8-troubleshooting)
9.  [Advanced — U2C Timing Parameters](#9-advanced--u2c-timing-parameters)
10. [Appendix — U2C Pinout](#10-appendix--u2c-pinout)

---

## 1. Introduction

**Lawicel U2C** (USB-to-CAN) is a popular, ready-to-use USB ↔ CAN 2.0B converter, compatible with the **SocketCAN** implementation in the Linux kernel. For the Linux user, the device appears as a network interface (e.g., `can0`), allowing use of the entire CAN tool ecosystem (can-utils, wireshark, and CANopen stacks such as [`CANopenSocket`](https://github.com/CANopenNode/CANopenSocket) or `lely-canopen`).

Fedora/RHEL systems by default use:

- **NetworkManager** (with `nmcli` / GUI) — default network interface management tool, managed by `systemd`,
- **firewalld** — firewall,
- **systemd** — init and service management system,
- **SELinux** — enabled by default (in enforcing mode on RHEL/Fedora), which may affect some tools.

**Consequences for CAN:**

- NetworkManager **does not have native CAN interface support** and may attempt to manage `can0` like a regular network interface — it should be disabled for CAN via `unmanaged-devices` (section [5.1](#51-disabling-can0-in-networkmanager))
- **systemd-networkd** has **built-in CAN support** (`Kind=can`) and is the recommended solution for persistent configuration — however on Fedora/RHEL it is **not enabled by default**, as the default backend is NetworkManager
- **NetworkManager + custom systemd service** is a practical solution for desktop Fedora users (section [5.3](#53-option-b-custom-systemd-service))
- **nmcli** can be used to disable management of `can0` without disabling NetworkManager for other interfaces
- **SELinux** by default does not block SocketCAN, but may block application access to CAN interfaces if they run with the wrong context — see section [8.5](#85-selinux-blocking-can-access) if you encounter issues

This guide has been prepared for **Fedora 39/40/41 and RHEL 9.x**, but general principles also apply to CentOS Stream, Rocky Linux, and AlmaLinux.

---

## 2. Hardware Requirements

### 2.1 U2C Converter

Lawicel U2C (or clone) is a USB → CAN 2.0B converter with galvanic isolation. Available variants:

| Model | Host Interface | CAN Connector | Isolation |
|-------|----------------|---------------|-----------|
| U2C‑A (mini) | USB 2.0 (micro‑USB) | 3‑pin JST‑PH (CAN H, CAN L, GND) | ✅ 1 kV |
| U2C‑B | USB 2.0 | 5‑pin screw terminal | ✅ 1 kV |
| U2C‑E (Ethernet) | Ethernet 100Base‑T | 5‑pin screw terminal | ✅ 1 kV |

> **Note**: Many cheap clones (e.g., from AliExpress, labeled "USB-CAN U2C") also work with the `gs_usb` module, but may have poorer isolation or lack compatibility with `SJA1000` mode — if you experience issues at speeds >500 kbit/s, see the [Troubleshooting](#8-troubleshooting) section.

### 2.2 CAN Wiring

Proper communication requires **two signal wires** and **common ground**:

| Signal | Color (typical) | Description |
|--------|-----------------|-------------|
| CAN H  | Yellow          | High dominant (2.5 V → 3.5 V) |
| CAN L  | Green           | Low dominant (2.5 V → 1.5 V)  |
| GND    | Black           | Reference ground               |

**Operating principles:**

- **120 Ω terminators** must be placed between CAN H and CAN L at both ends of the bus.
- Bus length at 1 Mbit/s should not exceed ~40 m; at 250 kbit/s — ~250 m.
- Common ground (GND) between the converter and drives significantly reduces frame errors.

### 2.3 Linux Compatibility

The U2C is based on a microcontroller with firmware compatible with the **Lawicel SLCAN** (Serial-Line CAN) protocol. Linux recognizes it through the kernel module:

```
gs_usb      — Generic CAN USB driver
```

USB identifiers (for original Lawicel U2C):

```
VID=0x1b7d  (Lawicel)
PID=0x0001  (U2C)
```

Clones may have other identifiers — check with `lsusb` and optionally load `gs_usb` with the `quirks` option if needed.

---

## 3. Driver and Tool Installation

### 3.1 Installing System Packages

```bash
# ── CAN tools (can-utils) ──
sudo dnf install -y can-utils

# ── Diagnostic tools (optional) ──
sudo dnf install -y         \
    wireshark               \   # CAN frame analysis
    iproute                 \   # ip link, ip -details (already installed)
    usbutils                    # lsusb
```

> **Note**: On RHEL 9.x, you may need to enable the `epel` repository (Extra Packages for Enterprise Linux) for packages such as `can-utils`:
> ```bash
> sudo dnf install -y epel-release
> sudo dnf install -y can-utils
> ```

If building [`astro-mount-controller`](installation.md) from source, additionally:

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

### 3.2 Loading Kernel Modules

The `gs_usb` module is built into the standard Fedora/RHEL kernel. Load it manually:

```bash
sudo modprobe can
sudo modprobe can_raw
sudo modprobe can_dev
sudo modprobe gs_usb
```

**Check if modules are available**:

```bash
lsmod | grep can
# Expected output (example):
# can_raw              36864  0
# can                  81920  1 can_raw
# gs_usb               28672  0
# can_dev              16384  1 gs_usb
```

To load modules automatically after reboot:

```bash
sudo tee /etc/modules-load.d/can.conf << 'EOF'
can
can_raw
can_dev
gs_usb
EOF
```

### 3.3 Connecting the U2C Converter

Plug in the U2C via USB. Check if it was recognized:

```bash
# ── Identify USB device ──
lsusb | grep -i "Lawicel\|U2C\|CAN"
# Example output:
# Bus 001 Device 003: ID 1b7d:0001 Lawicel U2C CAN adapter

# ── Did the network interface appear? ──
ip link show
# You should see: can0 (or can1, can2...)
# can0: <NO-CARRIER,BROADCAST,UP> ...
```

If the `can0` interface **did not appear**:

```bash
# Check kernel logs
dmesg | tail -20
# Look for: "gs_usb: signed", "gs_usb: probe", "CAN device driver interface"

# If the device has a different VID/PID, force gs_usb loading:
sudo modprobe gs_usb
# or add identifiers to the module:
sudo sh -c 'echo "1b7d 0001" > /sys/bus/usb/drivers/usb/new_id'
```

---

## 4. Temporary CAN Interface Configuration

Before persistent configuration, test operation by configuring the interface manually.

### 4.1 Setting Bitrate and Enabling the Interface

```bash
# ── Set CAN interface type and bitrate (e.g., 1 Mbit/s) ──
sudo ip link set can0 type can bitrate 1000000

# ── Enable the interface ──
sudo ip link set can0 up
```

If you need a different speed (commonly used in astronomy: 125 kbit/s, 250 kbit/s, 500 kbit/s, 1 Mbit/s):

```bash
sudo ip link set can0 type can bitrate 250000
sudo ip link set can0 up
```

### 4.2 Checking Interface Status

```bash
ip -details link show can0
```

Expected output:

```
2: can0: <NO-CARRIER,BROADCAST,UP> mtu 16 qdisc pfifo_fast state DOWN ...
    link/can
    can <BERR-REPORTING> state ERROR-ACTIVE restart-ms 100
    bitrate 1000000 sample-point 0.750
    tq 125 prop-seg 6 phase-seg1 7 phase-seg2 2 sjw 1
    ...
```

> **`NO-CARRIER`** when no bus is connected (or drives are powered off) is normal — it means the physical layer does not detect dominant signals.

### 4.3 Loopback Test

```bash
# ── Configure in loopback mode (no external bus needed) ──
sudo ip link set can0 type can bitrate 1000000 loopback on
sudo ip link set can0 up

# ── Send a test frame ──
cansend can0 123#DEADBEEF

# ── Receive frames (you should see an echo) ──
candump can0
```

To return to normal mode:

```bash
sudo ip link set can0 type can bitrate 1000000 loopback off
sudo ip link set can0 up
```

---

## 5. Persistent Configuration (at System Boot)

Fedora/RHEL by default use **NetworkManager**. Below are three options for persistent configuration — choose the one appropriate for your environment.

### 5.1 Disabling can0 in NetworkManager

Regardless of your chosen CAN configuration method, **first** disable NetworkManager management of `can0`:

```bash
sudo tee /etc/NetworkManager/conf.d/90-can-unmanaged.conf << 'EOF'
[keyfile]
unmanaged-devices=interface-name:can0
EOF

sudo systemctl restart NetworkManager
```

You can also use `nmcli`:

```bash
sudo nmcli device set can0 managed no
```

### 5.2 Option A: systemd-networkd (Recommended for Servers)

**systemd-networkd** has built-in CAN interface support via `Kind=can`. On Fedora/RHEL it is not enabled by default, but can be activated:

```bash
# ── Disable NetworkManager (optional, if you want to fully switch to networkd) ──
# sudo systemctl disable NetworkManager
# sudo systemctl stop NetworkManager

# ── Enable systemd-networkd ──
sudo systemctl enable --now systemd-networkd

# ── Create CAN netdev configuration ──
sudo tee /etc/systemd/network/10-can0.netdev << 'EOF'
[NetDev]
Name=can0
Kind=can

[CAN]
BitRate=1M
EOF

# ── Create CAN network configuration ──
sudo tee /etc/systemd/network/10-can0.network << 'EOF'
[Match]
Name=can0

[CAN]
BitRate=1000000
EOF

# ── Restart network ──
sudo systemctl restart systemd-networkd
```

> **Note**: If you keep NetworkManager active for other interfaces (WiFi, Ethernet), make sure section [5.1](#51-disabling-can0-in-networkmanager) has been executed.

### 5.3 Option B: Custom systemd Service

If you use NetworkManager on a Fedora desktop and prefer not to switch to systemd-networkd, create a standalone systemd service:

> **Note**: The service below **loads the necessary kernel modules** before configuring the interface. This is necessary because `After=network.target` does not guarantee CAN modules are already loaded.

```bash
sudo tee /etc/systemd/system/can-setup.service << 'EOF'
[Unit]
Description=CAN bus configuration with U2C converter
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
                echo "CAN0 already configured: bitrate $BITRATE, state $STATE — skipping"; \
                exit 0; \
            fi; \
            if [ "$STATE" = "UP" ]; then \
                ip link set can0 down; \
            fi; \
            if ip link set can0 type can bitrate 1000000 2>/dev/null; then \
                ip link set can0 up && \
                echo "CAN0 configured: bitrate 1000000, state UP" && \
                exit 0; \
            else \
                echo "Warning: cannot change bitrate (U2C may already be configured by another mechanism). Attempting to bring up existing interface..." && \
                ip link set can0 up 2>/dev/null && \
                echo "CAN0 brought up (current bitrate: ${BITRATE:-unknown})" && \
                exit 0; \
            fi; \
        fi; \
        sleep 0.5; \
    done; \
    echo "Error: can0 interface did not appear after 5s - check if U2C is connected" >&2; \
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

**How this service works:**

| Step | Command | Description |
|------|---------|-------------|
| 1 | `modprobe can` .. `gs_usb` | Loads kernel modules for CAN |
| 2 | Loop `for i in $(seq 1 10)` | Waits up to 5 seconds for `can0` to appear (U2C needs a moment for USB initialization) |
| 3 | `ip -details link show can0` | Checks if the interface exists and reads current bitrate/state |
| 4 | Condition: UP + bitrate=1000000 | If already configured correctly — skip (prevents conflict with NetworkManager) |
| 5 | `ip link set can0 down` | Disables interface before changing bitrate |
| 6 | `ip link set can0 type can bitrate ...` | Attempts to set the desired speed |
| 7 | Fallback: `ip link set can0 up` | If bitrate change fails — brings up existing interface with default bitrate |

### 5.4 udev Rule for Static Interface Name

To ensure the U2C converter always appears as `can0` (especially with multiple CAN interfaces):

```bash
sudo tee /etc/udev/rules.d/99-u2c-can.rules << 'EOF'
# Lawicel U2C - assign name can0
SUBSYSTEM=="net", ACTION=="add", ATTRS{idVendor}=="1b7d", ATTRS{idProduct}=="0001", NAME="can0"

# For clones - uncomment and adjust VID/PID:
# SUBSYSTEM=="net", ACTION=="add", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="55d4", NAME="can0"
EOF

sudo udevadm control --reload-rules
sudo udevadm trigger
```

---

## 6. Verification

### 6.1 Basic Tests

```bash
# ── Interface status ──
ip -details link show can0
# can0: <NO-CARRIER,BROADCAST,UP> ... state UP
# bitrate 1000000 sample-point 0.750

# ── Error counters ──
cat /sys/class/net/can0/stats
# rx_packets: 0  tx_packets: 0  rx_errors: 0  tx_errors: 0

# ── CAN controller state ──
cat /sys/class/net/can0/can_state
# ERROR-ACTIVE
```

### 6.2 Test Communication with CANopen Drive

If you have a CANopen drive connected (e.g., CiA 402-compatible servo drive):

```bash
# ── Listen to all frames ──
candump can0 &

# ── Send NMT Start Remote Node (ID=0 = all nodes) ──
cansend can0 000#01

# ── Check if the drive responds with bootup (ID=701 → 700+node_id) ──
# Expected frame: 701#00 (if node_id=1)
# Candump should show: can0 701 [1] 00

# ── Read drive statusword (SDO Read, node_id=1, index=0x6041) ──
cansend can0 601#40 41 60 00 00 00 00 00
# Expected response (SDO Write Response):
# can0 581#xx xx xx xx 00 00 00 00  (where xx is the statusword)
```

### 6.3 Performance Test (Bus Load)

```bash
# ── Test frame generator (1000 frames) ──
cangen can0 -L 8 -g 0 -n 1000 -v

# ── Check for errors ──
ip -s -details link show can0
# Check: rx-errors, tx-errors — should be 0
```

---

## 7. Configuring astro-mount-controller

Detailed controller configuration is in the following documents:

- [`installation.md`](installation.md) — installation and building,
- [`testing_and_running.md`](testing_and_running.md) — running modes,
- [`hal_layer.md`](hal_layer.md) — HAL layer (including CANopen HAL).

Below is an example JSON configuration for CANopen with U2C:

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

**Key parameters**:

| Parameter | Value for U2C | Notes |
|-----------|---------------|-------|
| `can_interface` | `can0` | SocketCAN interface name |
| `can_baud_rate` / `bitrate` | `1000000` | 1 Mbit/s (max for U2C) |
| `can_node_id` | `1` | CANopen node ID of the controller |
| `sync_interval_ms` | `100` | 10 Hz — compatible with typical PDO cycle |

> **Note**: Not all U2C converters (especially clones) work stably at 1 Mbit/s. If frame errors occur, reduce speed to 500 kbit/s or 250 kbit/s.

---

## 8. Troubleshooting

### 8.1 `can0` Does Not Appear After Connecting U2C

**Causes**:

- `gs_usb` module not loaded.
- Device has unsupported VID/PID.
- Conflict with another driver (e.g., `cp210x` for UART).

**Diagnostics**:

```bash
# Are modules loaded?
lsmod | grep can

# Is USB visible?
lsusb | grep -i "1b7d\|Lawicel\|CAN"

# Kernel logs
dmesg | grep -i "gs_usb\|can"
```

**Solution**:

```bash
# Force loading
sudo modprobe gs_usb

# If device has a different PID, add it to gs_usb:
sudo sh -c 'echo "1b7d 0001" > /sys/bus/usb/drivers/usb/new_id'

# If it's a CH340/CH341 clone (e.g., VID=1a86):
sudo modprobe gs_usb
sudo sh -c 'echo "1a86 55d4" > /sys/bus/usb/drivers/usb/new_id'
```

### 8.2 Frame Errors (rx-errors / tx-errors)

**Symptom**: `ip -s link show can0` shows increasing `rx-errors`.

**Causes**:

- Transmission speed too high for cable length.
- Missing 120 Ω terminator.
- EMI interference (CAN cable running parallel to power cables).
- Different ground (GND) potentials between devices.

**Solution**:

```bash
# Reduce speed
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 250000
sudo ip link set can0 up

# Enable error reporting (BERR)
sudo ip link set can0 type can bitrate 1000000 berr-reporting on

# View error frames
candump can0,0x000~0x080
```

### 8.3 `Permission denied` for can0 Interface

```bash
# Add user to can group (if it exists)
sudo groupadd -f can
sudo usermod -a -G can $USER
newgrp can

# Or grant 666 permissions (less secure)
sudo chmod 666 /dev/can0
```

For a persistent solution — udev rule:

```bash
sudo tee /etc/udev/rules.d/99-can-permissions.rules << 'EOF'
# Grant can group access to CAN interfaces
ACTION=="add", SUBSYSTEM=="net", KERNEL=="can*", RUN+="/bin/sh -c 'chown :can /sys/class/net/%k && chmod 660 /sys/class/net/%k'"
EOF
```

### 8.4 U2C Not Responding — Flashing LED

The Lawicel U2C has an LED that indicates status:

| LED | Meaning |
|-----|---------|
| Solid green | Interface configured and enabled (`UP`) |
| Flashing green | Frame transmission/reception |
| Flashing red | Frame errors (e.g., missing terminator, wrong speed) |
| Solid red | Hardware error (disconnect and reconnect) |

### 8.5 SELinux Blocking CAN Access

**Symptom**: Application like `astro-mount-controller` cannot open the CAN interface, despite correct permissions.

**Diagnostics**:

```bash
# Check SELinux logs
sudo ausearch -m avc -ts recent | grep can
# or
sudo journalctl | grep -i "selinux\|avc.*can"
```

**Solution**:

```bash
# Temporarily disable SELinux (for testing):
sudo setenforce 0
# Test the operation; if the problem disappears — SELinux is blocking access.

# Permanent solution — create an allow rule:
sudo ausearch -m avc -ts recent -c can-setup | tee /tmp/can-avc.log
# Generate and load the rule:
# (requires policycoreutils-python-utils package)
sudo grep "avc" /tmp/can-avc.log | audit2allow -M can_setup
sudo semodule -i can_setup.pp

# Restore SELinux enforcing:
sudo setenforce 1
```

### 8.6 Low Stability at 1 Mbit/s

**Symptom**: Occasional CRC errors, `state BUS-OFF`.

**Causes**:

- Clone U2C with inferior transceiver (e.g., SN65HVD230 instead of ISO1050).
- Bus too long (>40 m at 1 Mbit/s).
- Suboptimal sample point.

**Solution**:

```bash
# Adjust sample point for longer cables
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 1000000 sample-point 0.800
sudo ip link set can0 up

# Or reduce speed to 500 kbit/s
sudo ip link set can0 type can bitrate 500000
```

---

## 9. Advanced — U2C Timing Parameters

The U2C converter uses **SJA1000 mode** (default for `gs_usb`). Timing parameters can be tuned for non-standard speeds or longer buses.

### 9.1 Calculating Timing Parameters

```bash
# SocketCAN timing calculator tool
# (part of can-utils)
can-calc-bit-timing can0 1000000
```

Example results for common speeds:

| Bitrate | tq | Prop‑Seg | Phase‑Seg1 | Phase‑Seg2 | SJW | Sample point |
|---------|----|----------|------------|------------|-----|--------------|
| 1 Mbit/s | 125 ns | 6 | 7 | 2 | 1 | 75.0 % |
| 500 kbit/s | 250 ns | 6 | 7 | 2 | 1 | 75.0 % |
| 250 kbit/s | 500 ns | 6 | 7 | 2 | 1 | 75.0 % |
| 125 kbit/s | 1000 ns | 6 | 7 | 2 | 1 | 75.0 % |

Manual setting:

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

### 9.2 FD Mode (CAN FD) — Caution

**Original Lawicel U2C does not support CAN FD**. If you need CAN FD, consider:

- **Lawicel U2C‑FD** — newer model with CAN FD support up to 8 Mbit/s.
- **PCAN‑USB FD** — Peak Systems interface with CAN FD support.
- **Kvaser USBcan Pro FD** — professional CAN FD interface.

Clone U2C devices with CAN FD are practically unavailable on the market.

---

## 10. Appendix — U2C Pinout

### 10.1 Lawicel U2C‑A (mini, JST‑PH 3‑pin)

```
Pin 1 (red)    — CAN H
Pin 2 (white)  — CAN L
Pin 3 (black)  — GND
```

### 10.2 Lawicel U2C‑B (5‑pin screw terminal)

```
1 — CAN H
2 — CAN L
3 — CAN GND
4 — (NC) not connected
5 — (NC) not connected / shield
```

### 10.3 Connecting to CANopen Drive (CiA 303‑1, DR‑303)

Standard CANopen drive connector (DSub‑9, compliant with CiA 303‑1):

| DSub‑9 Pin | Signal   | U2C‑A (JST) | U2C‑B (screw) |
|-----------|----------|-------------|---------------|
| 2         | CAN L    | White       | Terminal 2    |
| 3         | GND      | Black       | Terminal 3    |
| 7         | CAN H    | Red         | Terminal 1    |

> **Important**: Connect the shield (DSub 9 pin → cable shield/enclosure) on **one side only** to avoid ground loops.

### 10.4 120 Ω Terminator

If the drive does not have a built-in terminator, connect a 120 Ω resistor between CAN H and CAN L on **both ends** of the bus. For hobby use, a DSub‑9 terminator adapter can be used:

```
CAN H ──╮
        ├── 120 Ω ──┐
CAN L ──╯           │
                    ═══
GND ────────────────╯
```

---

## Summary

Configuring the U2C converter on Fedora/RHEL involves a few steps:

1. **Connect** the USB converter — the `gs_usb` module will automatically create the `can0` interface.
2. **Configure** the speed: `sudo ip link set can0 type can bitrate 1000000 && sudo ip link set can0 up`.
3. **Verify**: `ip -details link show can0` → state `UP`, no errors.
4. **Create persistent configuration** — via systemd-networkd or a custom systemd service.
5. **Disable NetworkManager for can0** via `unmanaged-devices` (section 5.1).
6. **Configure** [`astro-mount-controller`](installation.md) with parameters `can_interface: can0`, `bitrate: 1000000`.
7. If you encounter access issues — **check SELinux** (section 8.5).

After completing these steps, the CAN bus will be ready for communication with CANopen drives (RA, Dec, derotator) according to the CiA 402 profile.

---

**See also**:

- [`testing_and_running.md`](testing_and_running.md) — running the controller with CANopen,
- [`hal_layer.md`](hal_layer.md) — CANopen HAL implementation details,
- [`canopen_alternatives.md`](canopen_alternatives.md) — communication protocol comparison,
- [`data_flow.md`](data_flow.md) — system data flow with CAN bus,
- [`can_u2c_config_opensuse.md`](can_u2c_config_opensuse.md) — openSUSE version,
- [`can_u2c_config_ubuntu_debian.md`](can_u2c_config_ubuntu_debian.md) — Ubuntu/Debian version,
- [`can_u2c_config_arch.md`](can_u2c_config_arch.md) — Arch Linux version.
