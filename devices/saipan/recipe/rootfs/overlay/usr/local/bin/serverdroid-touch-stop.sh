#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Matheus Tagliari
# Called at shutdown and reboot (inittab). Stops the touch service and puts the touch controller back in SPI program mode, the state the
# bootloader finds it in: left running its firmware across a warm reboot, the bootloader cannot start the panel (black screen).
for p in /proc/[0-9]*; do
    pid=${p#/proc/}
    # the start script first, so that it does not restart ctsd (this script has the same process name: skip its own pid)
    [ "$pid" != "$$" ] && [ "$(cat "$p/comm" 2> /dev/null)" = serverdroid-tou ] && kill "$pid"
done
for p in /proc/[0-9]*; do
    [ "$(cat "$p/comm" 2> /dev/null)" = ctsd ] && kill "${p#/proc/}"
done
sleep 0.2
rm -f /run/touch-present
for d in /dev/spidev*; do
    [ -c "$d" ] && /usr/local/bin/ctsboot "$d" /dev/null park > /dev/null 2>&1
done
