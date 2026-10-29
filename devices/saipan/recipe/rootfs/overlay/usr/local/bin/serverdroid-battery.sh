#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Matheus Tagliari
# Battery readout and a percentage estimate. Prints key=value lines:
#   voltage_mv  current_ma (positive = charging)  temp_c  percent  state  eta_min  charge_mode
# Measurements: voltage and thermistor from the PMIC AUXADC (IIO device MT6359), current from the fuel gauge power supply mt6359-fg
# (docs/battery-gauge-feasibility.md); without it the current reads 0. The percentage is an ESTIMATE, kept apart from the measurements: the open-circuit voltage (measured voltage minus
# current times the stock internal resistance) is looked up on the stock 25 C curve in /etc/serverdroid-battery.tbl
# (tools/battery-table.py, made from the stock device tree; not part of the repository). Accuracy is a few percent at best: the curve is
# for 25 C and a constant resistance.
# The thermistor is read as a divider: 100 kOhm to 1.8 V, NTC 100 kOhm with beta 4250 K (assumed, checked against the stock reading).

TBL=/etc/serverdroid-battery.tbl
CAP_MAH=5100      # the stock table's capacity
R_MOHM=128        # stock first-row internal resistance, 0.1 mOhm units read as mOhm

iio() { for d in /sys/bus/iio/devices/iio:device*; do [ "$(cat $d/name)" = "$1" ] && echo $d; done; }
A=$(iio MT6359)
CHG=$(ls -d /sys/class/power_supply/mt6360-chg* | head -n 1)
FG=/sys/class/power_supply/mt6359-fg

vraw=$(cat $A/in_resistance0_raw); traw=$(cat $A/in_temp1_raw)
if [ -r $FG/current_now ]; then ima=$(( $(cat $FG/current_now) / 1000 )); else ima=0; fi
online=$(cat $CHG/online); beh=$(cat $CHG/charge_behaviour | sed 's/.*\[\(.*\)\].*/\1/')

awk -v vraw=$vraw -v traw=$traw -v ima=$ima -v online=$online -v beh="$beh" -v tbl=$TBL -v cap=$CAP_MAH -v r=$R_MOHM '
BEGIN {
    mv = vraw * 6300 / 32768
    vt = traw * 4500 / 4096; rn = 100000 * vt / (1800 - vt)
    tc = 1 / (1 / 298.15 + log(rn / 100000) / 4250) - 273.15
    ocv = mv - ima * r / 1000
    n = 0
    while ((getline l < tbl) > 0) { if (l ~ /^#/) continue; split(l, f, " "); n++; V[n] = f[1]; P[n] = f[2] }
    pct = 0
    if (ocv >= V[1]) pct = 100
    else for (i = 2; i <= n; i++) if (ocv >= V[i]) { pct = P[i] + (P[i-1] - P[i]) * (ocv - V[i]) / (V[i-1] - V[i]); break }
    if (ima > 30) st = (beh == "auto" ? "charging" : "powered"); else if (online && ima > -30) st = "powered"; else st = "discharging"
    if (online && ima <= 30 && ima > -30) st = "powered"
    eta = -1
    if (ima > 30) { tgt = (beh == "inhibit-charge") ? pct : 100; if (tgt > pct) eta = (tgt - pct) / 100 * cap / ima * 60 }
    else if (ima < -30) eta = pct / 100 * cap / (-ima) * 60
    printf "voltage_mv=%d\ncurrent_ma=%d\ntemp_c=%.1f\npercent=%d\nstate=%s\neta_min=%d\ncharge_mode=%s\n", mv + 0.5, ima, tc, pct + 0.5, st, eta, beh
}'
