#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Matheus Tagliari
# Vibrate the phone when a charger is plugged in (two short pulses) or removed (one). It only reads the charger's "online" flag once a second.
# The motor is the force-feedback input device of the "regulator-haptic" node; without it (other kernels) the script exits quietly.

# The charger and the motor are probed some time after this starts at boot: wait for them (up to a minute) instead of giving up, which left the
# watcher silently absent on some boots. Without them after that (other kernels) it exits quietly.
n=0
while :; do
    CHG=$(ls -d /sys/class/power_supply/mt6360-chg* 2> /dev/null | head -n 1)
    EV=""
    for d in /sys/class/input/input*; do
        [ "$(cat "$d/name" 2> /dev/null)" = "regulator-haptic" ] && EV=/dev/input/$(ls "$d" | grep '^event' | head -n 1)
    done
    [ -n "$CHG" ] && [ -c "$EV" ] && break
    n=$((n + 1))
    [ "$n" -ge 60 ] && exit 0
    sleep 1
done

prev=$(cat "$CHG/online")
while sleep 1; do
    now=$(cat "$CHG/online")
    if [ "$now" != "$prev" ]; then
        if [ "$now" = 1 ]; then /usr/local/bin/buzz "$EV" 120; sleep 0.15; /usr/local/bin/buzz "$EV" 120; else /usr/local/bin/buzz "$EV" 300; fi
        prev=$now
    fi
done
