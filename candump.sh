#!/bin/sh
# Raw CANopen traffic dump for the STM42M (CANopen) mount on can0.
# (The old MF7025V2 frame filter for 0x141/0x142 was removed — that hardware
# is no longer in use.)
candump can0
