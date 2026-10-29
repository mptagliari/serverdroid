#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Matheus Tagliari
# ServerDroid system start: network over the USB NCM gadget, then SSH.
# The gadget itself is created by the initramfs init before it switches root.

hostname serverdroid
mkdir -p /run /dev/pts
mount -t devpts devpts /dev/pts
mount -t tmpfs tmpfs /run
# Only present in kernels built with DEBUG_FS / tracing (the lab kernel); harmless otherwise.
mount -t debugfs debugfs /sys/kernel/debug 2> /dev/null
mount -t tracefs tracefs /sys/kernel/tracing 2> /dev/null

# The nodes every program opens must be world accessible. The initramfs runs `mdev -s` with no mdev.conf, which sets every node to 660 at each
# boot. The rules of /etc/mdev.conf only cover hotplug events after this point.
chmod 666 /dev/null /dev/zero /dev/full /dev/random /dev/urandom /dev/tty /dev/ptmx

ifconfig lo up
ifconfig ncm0 172.16.42.1 netmask 255.255.255.0 up

# Key-only root login; host keys are generated on the first connection.
mkdir -p /etc/dropbear
dropbear -s -R -p 22

# Connectivity (Wi-Fi), if its modules and firmware are installed on this system (they are not part of the image: Motorola blobs and
# out-of-tree modules, see docs/wifi-investigation.md). Runs in the background so SSH over USB never waits for it; the log is /run/conn-boot.log.
if [ -x /opt/serverdroid-conn/conn-bringup.sh ]; then
    (
        /opt/serverdroid-conn/conn-bringup.sh \
            && [ -r /etc/wpa_supplicant.conf ] && /opt/serverdroid-conn/wifi-up.sh
    ) > /run/conn-boot.log 2>&1 &
fi

# Vibration feedback when a charger is plugged in or removed.
[ -x /usr/local/bin/serverdroid-power-watch ] && /usr/local/bin/serverdroid-power-watch &

# Charge policy (keeps the battery between 70% and 80% by default; /etc/serverdroid-charge-mode).
[ -x /usr/local/bin/serverdroid-charge-policy ] && /usr/local/bin/serverdroid-charge-policy &

# Touch screen (see serverdroid-touch-start).
[ -x /usr/local/bin/serverdroid-touch-start ] && /usr/local/bin/serverdroid-touch-start &

# A hub attached at boot: make the host controller see it (see serverdroid-usb-kick).
[ -x /usr/local/bin/serverdroid-usb-kick ] && /usr/local/bin/serverdroid-usb-kick &

# USB disks: mount on plug (busybox mdev as the kernel's hotplug helper, rules in /etc/mdev.conf).
echo /sbin/mdev > /proc/sys/kernel/hotplug

# Scheduled stability log (optional): /etc/serverdroid-stability sets AT and UNTIL, see tools/stability-log.sh. Survives a reboot.
[ -r /etc/serverdroid-stability ] && [ -x /opt/serverdroid-conn/stability-log.sh ] && /opt/serverdroid-conn/stability-log.sh > /dev/null 2>&1 &

# Swap (a build that peaks is not killed), if the file exists; swapping lightly.
[ -e /swapfile ] && /sbin/swapon /swapfile 2> /dev/null && echo 10 > /proc/sys/vm/swappiness

# Battery log (optional): /etc/serverdroid-battery-log sets INTERVAL and WHILE (the condition for a session), see tools/battery-log.sh.
[ -r /etc/serverdroid-battery-log ] && [ -x /usr/local/bin/battery-log ] && ( . /etc/serverdroid-battery-log; export INTERVAL WHILE; exec /usr/local/bin/battery-log ) >> /var/log/serverdroid/battery-log.events 2>&1 &
