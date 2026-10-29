#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Matheus Tagliari
# The kernel log with the wall clock time (HH:MM:SS, local) in place of the seconds since boot. The kernel only stamps its messages with the
# time since boot, so the clock time is worked out from the uptime now: boot time = time now - uptime. Same input as dmesg; options go to dmesg.
dmesg "$@" | awk -v up="$(cut -d' ' -f1 /proc/uptime)" -v now="$(date +%H:%M:%S)" '
BEGIN { split(now, t, ":"); boot = t[1] * 3600 + t[2] * 60 + t[3] - up }
{
    if (match($0, /^\[ *[0-9]+\.[0-9]+\]/)) {
        ts = substr($0, 2, RLENGTH - 2) + 0
        s = int(boot + ts); s = ((s % 86400) + 86400) % 86400
        printf "%02d:%02d:%02d%s\n", int(s / 3600), int((s % 3600) / 60), s % 60, substr($0, RLENGTH + 1)
    } else print
}'
