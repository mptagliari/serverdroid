#!/bin/busybox sh
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Matheus Tagliari
# ServerDroid first-boot diagnostic init.
# Brings up a USB gadget (ACM serial shell + NCM network) and nothing else.
# The ACM shell has no authentication: use only for first-boot bring-up.

/bin/busybox mount -t proc proc /proc
/bin/busybox mount -t sysfs sysfs /sys
/bin/busybox mount -t tmpfs tmpfs /dev
/bin/busybox --install -s
# `mdev -s` sets every node it sees to 660 unless a rule says otherwise, and this ramdisk has no mdev.conf: keep the nodes every program opens world
# accessible (a privileged container sees the host's nodes as they are; serverdroid-rc also repeats this on the root filesystem).
mkdir -p /etc
cat > /etc/mdev.conf << 'MDEV'
-null		0:0	666
-zero		0:0	666
-full		0:0	666
-random		0:0	666
-urandom	0:0	666
-tty		0:0	666
-ptmx		0:0	666
MDEV
mdev -s
[ -c /dev/console ] || mknod /dev/console c 5 1
[ -c /dev/null ] || mknod /dev/null c 1 3
[ -c /dev/tty ] || mknod /dev/tty c 5 0
[ -c /dev/ptmx ] || mknod /dev/ptmx c 5 2
[ -c /dev/urandom ] || mknod /dev/urandom c 1 9

echo "serverdroid: init started" > /dev/kmsg

mount -t configfs configfs /sys/kernel/config
G=/sys/kernel/config/usb_gadget/g1
mkdir $G
echo 0x1d6b > $G/idVendor
echo 0x0104 > $G/idProduct
mkdir $G/strings/0x409
echo "ServerDroid" > $G/strings/0x409/manufacturer
echo "ServerDroid saipan" > $G/strings/0x409/product
echo "serverdroid-saipan" > $G/strings/0x409/serialnumber
mkdir $G/configs/c.1
mkdir $G/configs/c.1/strings/0x409
echo "ACM+NCM" > $G/configs/c.1/strings/0x409/configuration
mkdir $G/functions/acm.usb0
mkdir $G/functions/ncm.usb0
ln -s $G/functions/acm.usb0 $G/configs/c.1/
ln -s $G/functions/ncm.usb0 $G/configs/c.1/

UDC=$(ls /sys/class/udc | head -n 1)
echo "serverdroid: binding UDC '$UDC'" > /dev/kmsg
echo "$UDC" > $G/UDC
sleep 1
mdev -s

# The NCM interface is called ncm0 on the downstream kernel and usb0 on mainline.
for NIC in ncm0 usb0; do
    ifconfig $NIC 172.16.42.1 netmask 255.255.255.0 up 2> /dev/null && break
done

# Hand over to the real root filesystem if the "userdata" partition holds one.
# On any failure fall through to the diagnostic shell below.
PART=$(grep -l "^PARTNAME=userdata$" /sys/class/block/*/uevent 2> /dev/null | head -n 1)
PART=$(basename "$(dirname "$PART")" 2> /dev/null)
# serverdroid.noroot=1 on the kernel command line keeps the initramfs shell (first boots of a
# new kernel: look at the storage by hand before anything mounts it read-write).
if grep -q "serverdroid.noroot=1" /proc/cmdline; then
    echo "serverdroid: noroot requested, staying in initramfs (userdata=${PART:-none})" > /dev/kmsg
    PART=
fi
if [ -n "$PART" ] && [ -b "/dev/$PART" ]; then
    mkdir -p /newroot
    if mount -t ext4 -o rw "/dev/$PART" /newroot 2> /dev/kmsg && [ -x /newroot/sbin/init ]; then
        echo "serverdroid: switching root to /dev/$PART" > /dev/kmsg
        mkdir -p /newroot/proc /newroot/sys /newroot/dev
        mount --move /proc /newroot/proc
        mount --move /sys /newroot/sys
        mount --move /dev /newroot/dev
        exec switch_root /newroot /sbin/init
    fi
    echo "serverdroid: no usable root on /dev/$PART, staying in initramfs" > /dev/kmsg
fi

# Respawn a root shell on the serial gadget forever; PID 1 must never exit.
while true; do
    if [ -c /dev/ttyGS0 ]; then
        echo "serverdroid: saipan up, $(uname -r)" > /dev/ttyGS0
        sh < /dev/ttyGS0 > /dev/ttyGS0 2>&1
    fi
    sleep 1
done
