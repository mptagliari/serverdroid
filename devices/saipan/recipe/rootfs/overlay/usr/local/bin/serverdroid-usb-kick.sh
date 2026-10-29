#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Matheus Tagliari
# Boot with a USB-C hub already attached: the port settles as host (and sink) but the host controller sometimes never sees the hub (no device
# on the bus, no connect line in dmesg), which left the drives unmounted and NFS empty on 2026-10-27 (docs/typec-power.md). Writing "none" and
# then "host" to the USB role switch makes it enumerate. This waits, and only when a partner is attached, the port is the data host and the bus
# is still empty does it do that, up to three times. A cable to a PC (device role) or nothing plugged in is left alone.
R=$(ls /sys/class/usb_role/*/role 2> /dev/null | head -n 1)
[ -w "$R" ] || exit 0
sleep 20
n=0
while [ "$n" -lt 3 ]; do
    [ -e /sys/class/typec/port0-partner ] || exit 0
    grep -q '\[host\]' /sys/class/typec/port0/data_role 2> /dev/null || exit 0
    [ -e /sys/bus/usb/devices/1-1 ] && exit 0
    echo "serverdroid-usb-kick: partner attached, host role, empty bus: toggling the role switch (try $((n + 1)))" > /dev/kmsg 2> /dev/null
    echo none > "$R"; sleep 2; echo host > "$R"; sleep 12
    n=$((n + 1))
done
