#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Matheus Tagliari
# Charge policy for a phone that stays plugged in. Every 10 seconds it reads the chosen mode from /etc/serverdroid-charge-mode and sets the
# charger's charge_behaviour (auto, inhibit-charge, force-discharge; see docs/battery-gauge-feasibility.md):
#   limit    (default) hold the battery between 70% and 80%: at 80% or more run from the cable and let the battery rest (inhibit-charge),
#            at 70% or less charge again; below 15% always charge. Needs a cable; without one nothing changes.
#   full     charge to 100% (auto)
#   hold     run from the cable, battery rests (inhibit-charge)
#   battery  run from the battery even with a cable (force-discharge)
# The percentage is the estimate from serverdroid-battery (a few percent of error), so the limit is approximate.

MODEF=/etc/serverdroid-charge-mode
HIGH=80; LOW=70; CRIT=15
CHG=$(ls -d /sys/class/power_supply/mt6360-chg* 2> /dev/null | head -n 1)
[ -n "$CHG" ] || exit 0

setb() { [ "$(sed 's/.*\[\(.*\)\].*/\1/' "$CHG/charge_behaviour")" = "$1" ] || echo "$1" > "$CHG/charge_behaviour"; }

while sleep 10; do
    mode=$(cat $MODEF 2> /dev/null); [ -n "$mode" ] || mode=limit
    case "$mode" in
        full) setb auto ;;
        hold) setb inhibit-charge ;;
        battery) setb force-discharge ;;
        *)
            [ "$(cat $CHG/online)" = 1 ] || continue
            pct=$(/usr/local/bin/serverdroid-battery 2> /dev/null | sed -n 's/^percent=//p')
            [ -n "$pct" ] || continue
            cur=$(sed 's/.*\[\(.*\)\].*/\1/' "$CHG/charge_behaviour")
            if [ "$pct" -le "$CRIT" ] || [ "$pct" -le "$LOW" ]; then setb auto
            elif [ "$pct" -ge "$HIGH" ]; then setb inhibit-charge
            elif [ "$cur" = force-discharge ]; then setb auto
            fi ;;
    esac
done
