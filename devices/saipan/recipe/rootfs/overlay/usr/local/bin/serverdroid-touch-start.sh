#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Matheus Tagliari
# Start the touch screen: the controller is a Chipone TDDI on SPI (no kernel driver for it in mainline), so its SPI node is handed to
# spidev and ctsd (tools/ctsd.c) does the rest: it resets the chip, which then runs the firmware stored in its own flash, and feeds the
# interface's FIFO with the tokens UP, DOWN (drag), TAP x y and LONG x y. Restarted if it ever exits.

# On by default (it only starts when the firmware file is on the phone); /etc/serverdroid-touch-disable turns it off. It was opt-in for a day,
# while a black screen after a reboot was being chased (it was not the touch: docs/touch-input-investigation.md).
[ -e /etc/serverdroid-touch-disable ] && exit 0

FIFO=/run/keys.fifo
for d in /sys/bus/spi/devices/*; do
    [ "$(cat "$d/modalias" 2> /dev/null)" = "spi:chipone-tddi" ] && DEV=$d
done
[ -n "$DEV" ] || exit 0
echo spidev > "$DEV/driver_override"
[ -e "$DEV/driver" ] || echo "$(basename "$DEV")" > /sys/bus/spi/drivers/spidev/bind 2> /dev/null
n=0
while [ ! -e "$DEV/spidev"/spidev* ] && [ "$n" -lt 20 ]; do sleep 0.2; n=$((n + 1)); done
SD=$(ls "$DEV/spidev" 2> /dev/null | head -n 1)
[ -n "$SD" ] || exit 0
[ -c "/dev/$SD" ] || { d=$(cat "$DEV/spidev/$SD/dev"); mknod "/dev/$SD" c "${d%:*}" "${d#*:}"; }
# The chip has no usable firmware of its own after a cold start (its RAM is empty and a plain reset starts nothing): load the firmware file
# into the chip's RAM (reset, program-mode magic, write, verify), then reset it again without the magic so that it starts from the RAM
# (tools/ctsboot.c; found out on 2026-10-26, docs/touch-input-investigation.md). The file is Motorola's and is only on the phone.
FW=/lib/firmware/ICNL9911.bin
# The sequence is not always accepted at the first try (2026-10-27: after a reboot the program mode was not entered, or the second reset left
# the chip silent, and the touch stayed dead until the sequence was run again by hand): check that the chip answers afterwards and repeat it
# up to four times.
chip_up() { /usr/local/bin/ctsread "/dev/$SD" 0x000a 2 > /dev/null 2>&1; }
boot_chip() {
    [ -r "$FW" ] || return 1
    t=0
    while [ "$t" -lt 4 ]; do
        /usr/local/bin/ctsboot "/dev/$SD" "$FW" reset > /run/touch-boot.log 2>&1
        /usr/local/bin/ctsboot "/dev/$SD" "$FW" flash >> /run/touch-boot.log 2>&1
        chip_up && return 0
        t=$((t + 1)); sleep 2
    done
    return 1
}
chip_up || boot_chip || exit 0
while :; do
    : > /run/touch-present            # the interface offers the keyboard and taps while this file exists
    /usr/local/bin/ctsd "/dev/$SD" "$FIFO"
    rm -f /run/touch-present
    sleep 5
    chip_up || boot_chip || sleep 30   # the chip went silent: start it again instead of restarting ctsd against nothing
done
