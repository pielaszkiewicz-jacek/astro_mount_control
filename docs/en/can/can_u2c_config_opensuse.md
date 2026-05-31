# CAN Bus Configuration on openSUSE with U2C Converter

> This document describes step-by-step configuration of the CAN (Controller Area Network) bus on **openSUSE** systems using the Lawicel U2C USB-to-CAN converter (or compatible). This configuration is required to run [`astro-mount-controller`](index.md) with real CANopen drives.

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

openSUSE systems by default use:

- **[wicked](https://github.com/openSUSE/wicked)** — network interface management tool,
- **firewalld** — firewall,
- **systemd** — init and service management system.

**What is wicked?**

Wicked is a low-level network configuration framework, originally developed by SUSE (now openSUSE). Unlike `NetworkManager` (which is a user-space layer with GUI, `nmcli`) or `systemd-networkd` (part of systemd), wicked runs as a standalone system service that:

- manages network interfaces through files in `/etc/sysconfig/network/ifcfg-*`
- supports advanced features: bonding, bridging, VLAN, TUN/TAP, wireless interfaces
- has a built-in configuration script system in `/etc/wicked/scripts/` — called during `ifup`/`ifdown`, allowing configuration of any interface type, including **CAN**
- unlike `NetworkManager`, does not try to auto-detect and manage all interfaces — it requires explicit configuration in `ifcfg-*`

> **Why wicked instead of NetworkManager on openSUSE?**
> SUSE has always used the `/etc/sysconfig/network/` configuration model, which is simpler to automate (scripts, ansible) and more predictable than NetworkManager's automatic detection. In openSUSE Leap, it is the default network stack from installation.

**Consequences for CAN:**

- Wicked **does not have native CAN interface support** (no built-in `Kind=can` like `systemd-networkd`), so an external script is required for CAN configuration (section [5.1](#51-option-a-wicked-default))
- If NetworkManager is also installed (common on GNOME/KDE desktops), it may attempt to manage `can0` — this must be disabled via `unmanaged-devices` (described in section 5.1)
- Alternatively, you can completely disable wicked and switch to `systemd-networkd`, which has built-in CAN support (section [5.2](#52-option-b-systemd-networkd-alternative))

This guide has been prepared for **openSUSE Leap 15.4+ / Tumbleweed**, but general principles apply to other distributions as well.

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

Clones may have different IDs — check with `lsusb` and optionally load `gs_usb` with the `quirks` option if needed.

---

## 3. Driver and Tool Installation

### 3.1 Installing System Packages

```bash
# ── CAN tools (can-utils) ──
sudo zypper install -y can-utils

# ── Diagnostic tools (optional) ──
sudo zypper install -y      \
    wireshark               \   # CAN frame analysis
    iproute2                \   # ip link, ip -details
    usbutils                    # lsusb
```

If building [`astro-mount-controller`](installation.md) from source, additionally:

```bash
sudo zypper install -y      \
    gcc-c++ gcc make cmake   \
    git pkg-config           \
    libopenssl-devel         \
    protobuf-devel           \
    grpc-devel               \
    nlohmann-json-devel      \
    eigen3-devel             \
    fmt-devel spdlog-devel   \
    gtest-devel              \
    sqlite3-devel
```

### 3.2 Loading Kernel Modules

The `gs_usb` module is built into the standard openSUSE kernel. Load it manually:

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

openSUSE can use several methods for interface management. Below are three options — choose the one appropriate for your environment.

### 5.1 Option A: wicked (Default)

```bash
# ── Create CAN setup script ──
sudo tee /etc/wicked/scripts/can-setup.sh << 'EOF'
#!/bin/bash
# CAN bus configuration with U2C converter
# Called by wicked during ifup
if [ "$INTERFACE" = "can0" ]; then
    ip link set can0 type can bitrate 1000000
    ip link set can0 up
fi
EOF
sudo chmod +x /etc/wicked/scripts/can-setup.sh

# ── Create wicked configuration for can0 ──
sudo tee /etc/sysconfig/network/ifcfg-can0 << 'EOF'
STARTMODE='auto'
BOOTPROTO='static'
USERCONTROL='no'
EOF

# ── Enable and start wicked ──
sudo systemctl enable wicked
sudo systemctl restart wicked
```

> **Note**: If using **NetworkManager** (e.g., in openSUSE Tumbleweed with Gnome/KDE), disable it for `can0`:
> ```bash
> sudo nmcli device set can0 managed no
> ```
> or create `/etc/NetworkManager/conf.d/90-can-unmanaged.conf`:
> ```
> [keyfile]
> unmanaged-devices=interface-name:can0
> ```

### 5.2 Option B: systemd-networkd (Alternative)

```bash
# ── Disable wicked, enable systemd-networkd ──
sudo systemctl disable wicked
sudo systemctl enable systemd-networkd

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

### 5.3 Option C: Custom systemd Service

If you prefer full control (e.g., with error handling and restart):

> **Note**: The service below **loads required kernel modules** before configuring the interface — unlike options A and B, which rely on `modules-load.d`. This is necessary because `After=network.target` does not guarantee that CAN modules are already loaded.

```bash
sudo tee /etc/systemd/system/can-setup.service << 'EOF'
[Unit]
Description=CAN bus configuration with U2C converter
After=network.target

[Service]
Type=oneshot
RemainAfterExit=yes
# Step 1: Load kernel modules (if not already loaded)
# Step 2: Wait for can0 interface to appear (up to 5 seconds)
# Step 3: Configure bitrate and enable
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
                echo "Warning: cannot change bitrate (U2C may already be configured by another mechanism). Trying to bring up existing interface..." && \
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
| 2 | Loop `for i in $(seq 1 10)` | Waits up to 5 seconds for `can0` to appear (U2C needs time for USB initialization) |
| 3 | `ip -details link show can0` | Checks if interface exists and reads current bitrate/state |
| 4 | Condition: UP + bitrate=1000000 | If already configured correctly — skip (prevents conflict with wicked/NetworkManager) |
| 5 | `ip link set can0 down` | Disables interface before bitrate change |
| 6 | `ip link set can0 type can bitrate ...` | Attempts to set the desired speed |
| 7 | Fallback: `ip link set can0 up` | If bitrate change failed (e.g., U2C already locked by another driver) — brings up existing interface with current bitrate |

**Most common cause of `Device or resource busy` error:**

The `can0` interface has already been configured by another mechanism (wicked, NetworkManager, udev, or a previous service run) and its timing parameters (bit timing) are already locked by the `gs_usb` driver. The new service handles this by:

1. Checking if the interface is already UP with correct bitrate → if so, skips configuration
2. If bitrate change fails → brings up the interface as-is (with U2C default bitrate ~500 kbit/s or the last configured one)

To check current configuration:
```bash
ip -details link show can0
# Look for: bitrate XXXX, state UP/DOWN
```

If you need a specific bitrate different from the default, disable other management mechanisms (sections 5.1, 5.2) first, or reset the U2C by disconnecting and reconnecting the USB cable.

**Diagnostics when the service fails to start:**

```bash
# View errors
sudo journalctl -xeu can-setup.service

# Manual step-by-step execution:
sudo modprobe can can_raw can_dev gs_usb
lsmod | grep can
# Check if gs_usb is loaded

# Is U2C visible on USB?
lsusb | grep -i "1b7d\|Lawicel\|CAN"

# Did can0 appear after module loading?
ip link show can0

# Manual configuration:
sudo ip link set can0 type can bitrate 1000000
sudo ip link set can0 up
```

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
- Different ground potentials between devices.

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

Lawicel U2C has an LED indicating status:

| LED | Meaning |
|-----|---------|
| Solid green | Interface configured and enabled (`UP`) |
| Flashing green | Frame transmission/reception |
| Flashing red | Frame errors (e.g., missing terminator, wrong speed) |
| Solid red | Hardware error (disconnect and reconnect) |

### 8.5 Low Stability at 1 Mbit/s

**Symptom**: Sporadic CRC errors, `state BUS-OFF`.

**Causes**:

- U2C clone with inferior transceiver (e.g., SN65HVD230 instead of ISO1050).
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

### 8.6 AppArmor Profile Issues

openSUSE uses AppArmor (not SELinux). If you have AppArmor profiles enabled for custom services, they may block access to SocketCAN interfaces. Check with:

```bash
sudo aa-status | grep can-setup
```

If needed, create an AppArmor profile for the CAN setup service (see [`testing_and_running.md`](testing_and_running.md#124-apparmor-profile) for detailed instructions).

---

## 9. Advanced — U2C Timing Parameters

The U2C converter uses **SJA1000 mode** (default for `gs_usb`). Timing parameters can be tuned for non-standard speeds or longer buses.

### 9.1 Calculating Timing Parameters

```bash
# SocketCAN timing calculator tool
# (part of can-utils on openSUSE)
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
- **PCAN‑USB FD** — Peak Systems CAN FD interface.
- **Kvaser USBcan Pro FD** — professional CAN FD interface.

U2C clones with CAN FD support are practically non-existent on the market.

---

## 10. Appendix — U2C Pinout

### 10.1 Lawicel U2C‑A (mini, JST‑PH 3‑pin connector)

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

> **Important**: Connect the shielded cable (DSub pin 9 → cable shield/housing) on only one side to avoid ground loops.

### 10.4 120 Ω Terminator

If the drive does not have a built-in terminator, connect a 120 Ω resistor between CAN H and CAN L at **both ends** of the bus. In amateur settings, you can use a DSub‑9 terminator adapter:

```
CAN H ──╮
        ├── 120 Ω ──┐
CAN L ──╯           │
                    ═══
GND ────────────────╯
```

---

## Summary

Configuring the U2C converter on openSUSE comes down to a few steps:

1. **Connect** the USB converter — the `gs_usb` module will automatically create the `can0` interface.
2. **Configure** the speed: `sudo ip link set can0 type can bitrate 1000000 && sudo ip link set can0 up`.
3. **Verify**: `ip -details link show can0` → state `UP`, no errors.
4. **Create persistent configuration** via wicked, systemd-networkd, or a custom systemd service.
5. **Configure** [`astro-mount-controller`](installation.md) with parameters `can_interface: can0`, `bitrate: 1000000`.

After completing these steps, the CAN bus will be ready for communication with CANopen drives (RA, Dec, derotator) according to the CiA 402 profile.

---

**See also**:

- [`testing_and_running.md`](../testing_and_running.md) — running the controller with CANopen,
- [`hal_layer.md`](../hal_layer.md) — CANopen HAL implementation details,
- [`canopen_alternatives.md`](../canopen_alternatives.md) — communication protocol comparison,
- [`data_flow.md`](../data_flow.md) — system data flow with CAN bus,

**CAN configuration on other distributions:**

- [`can_u2c_config_ubuntu_debian.md`](can_u2c_config_ubuntu_debian.md) — Ubuntu/Debian,
- [`can_u2c_config_fedora.md`](can_u2c_config_fedora.md) — Fedora/RHEL/CentOS,
- [`can_u2c_config_arch.md`](can_u2c_config_arch.md) — Arch Linux/Manjaro.
