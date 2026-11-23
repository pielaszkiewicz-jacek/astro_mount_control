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
slcand -F -o -f -s8 "$TTY" can0 &
SLCAND_PID=$!
sleep 2
ip link set can0 up

trap 'ip link set can0 down 2>/dev/null; kill "$SLCAND_PID" 2>/dev/null' TERM INT
wait "$SLCAND_PID"
