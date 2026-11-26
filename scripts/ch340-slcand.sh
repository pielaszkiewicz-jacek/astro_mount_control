#!/bin/sh
# CH340 USB-CAN (SLCAN) adapter -> can0 @ 1 Mbit/s.
# Uses the stable by-id path first, then falls back to the udev symlink
# (/dev/ttyUSB-CAN) and finally /dev/ttyUSB0.
TTY="/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0"
if [ ! -e "$TTY" ]; then TTY="/dev/ttyUSB-CAN"; fi
if [ ! -e "$TTY" ]; then TTY="/dev/ttyUSB0"; fi

echo "Starting slcand on $TTY -> can0 @ 1 Mbit/s"
# -F = stay in foreground (do NOT daemonize); otherwise the parent exits and
#      the systemd unit deactivates immediately, tearing down can0.
# -o = send the SLCAN open command; -f = reset error flags via status read.
# -s8 = CAN bitrate 1 Mbit/s (the Lawicel SLCAN code for 1 Mbps).
SLCAN_ARGS="-F -o -f -s8"
# Optional higher UART (serial) speed via -S.  The CH340 SLCAN adapter's serial
# line is the real throughput bottleneck (~220 frames/s), so raising it off the
# default 115200 baud helps.  The value MUST match the adapter firmware
# (Lawicel SLCAN), otherwise the link breaks entirely — leave unset unless the
# firmware speed is known.  Example: SLCAN_UART_BAUD=2000000
if [ -n "$SLCAN_UART_BAUD" ]; then
    SLCAN_ARGS="$SLCAN_ARGS -S $SLCAN_UART_BAUD"
fi
# shellcheck disable=SC2086
slcand $SLCAN_ARGS "$TTY" can0 &
SLCAND_PID=$!

# Wait for the slcan netdev to appear (slcand creates it asynchronously after
# the adapter accepts the open command).
i=0
while [ "$i" -lt 50 ]; do
    [ -d /sys/class/net/can0 ] && break
    sleep 0.2
    i=$((i + 1))
done

if [ -d /sys/class/net/can0 ]; then
    # Raise the TX queue: bursts of SDO + the dual-frame RPDO2 "new setpoint"
    # sequence can otherwise overflow the small default slcan queue (ENOBUFS),
    # which surfaces as failed setPosition().
    ip link set can0 txqueuelen 1000
    if ip link set can0 up; then
        echo "can0 is up"
    else
        echo "ERROR: failed to bring can0 up" >&2
    fi
else
    echo "ERROR: slcand did not create can0" >&2
fi

trap 'ip link set can0 down 2>/dev/null; kill "$SLCAND_PID" 2>/dev/null' TERM INT
wait "$SLCAND_PID"
