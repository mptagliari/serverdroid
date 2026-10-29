#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Matheus Tagliari
# Mount USB disks when they are plugged in and unmount them when they are unplugged. Called by mdev (etc/mdev.conf) with ACTION=add or
# ACTION=remove and MDEV=sdd1 (a partition) or sdd (a disk with no partitions). A drive appears under /srv/usb/<label>, <UUID> or <device
# name>; if the optional serverdroid-nfs export tool is installed, its exports are refreshed after each change. NFS is not part of the
# core recipe (see devices/saipan/REPORT.md).
#   /etc/serverdroid-automount-disable   its presence turns mounting on plug off
#   /etc/serverdroid-automount   optional: the mount options, one line (default rw,noatime,nodev,nosuid); use ro to be careful
# Only USB disks are touched: the phone's own storage (sdc*) never is. Drives that fail to mount (no or unknown filesystem) are left alone.

BASE=/srv/usb
STATE=/run/automount
OPTS="rw,noatime,nodev,nosuid"
[ -r /etc/serverdroid-automount ] && OPTS=$(head -n 1 /etc/serverdroid-automount)
log() { echo "serverdroid-automount: $*" > /dev/kmsg 2> /dev/null; }

[ -n "$MDEV" ] || exit 0
# /etc/serverdroid-automount-disable: nothing is mounted on plug (a host that must never see a drive's files, e.g. one that runs privileged containers).
[ "$ACTION" = add ] && [ -e /etc/serverdroid-automount-disable ] && exit 0
mkdir -p "$BASE" "$STATE"

case "$ACTION" in
add)
    # Only disks on the USB bus (AUTOMOUNT_TEST=1 lets a test use another device).
    [ -n "$AUTOMOUNT_TEST" ] || readlink -f "/sys/class/block/$MDEV" | grep -q /usb || exit 0
    [ -e "$STATE/$MDEV" ] && exit 0
    # A whole disk is mounted only when it has no partitions (they show up a moment after the disk).
    case "$MDEV" in
    sd[a-z]) sleep 2; ls -d /sys/block/$MDEV/$MDEV[0-9]* > /dev/null 2>&1 && exit 0 ;;
    esac
    dev=/dev/$MDEV
    type=$(blkid -s TYPE -o value "$dev" 2> /dev/null)
    [ -n "$type" ] || { log "$dev: no filesystem it knows, left alone"; exit 0; }
    label=$(blkid -s LABEL -o value "$dev" 2> /dev/null)
    [ -n "$label" ] || label=$(blkid -s UUID -o value "$dev" 2> /dev/null)
    [ -n "$label" ] || label=$MDEV
    name=$(printf '%s' "$label" | tr -c 'A-Za-z0-9._-' '_')
    dir=$BASE/$name
    n=1
    while [ -e "$dir" ] && mountpoint -q "$dir"; do n=$((n + 1)); dir=$BASE/${name}_$n; done
    mkdir -p "$dir"
    case "$type" in
    ntfs) mount -t ntfs-3g -o "$OPTS" "$dev" "$dir" ;;
    *) mount -t "$type" -o "$OPTS" "$dev" "$dir" ;;
    esac || { log "$dev ($type) did not mount"; rmdir "$dir" 2> /dev/null; exit 1; }
    echo "$dir" > "$STATE/$MDEV"
    log "$dev ($type, $OPTS) mounted on $dir"
    [ -x /usr/local/sbin/serverdroid-nfs ] && /usr/local/sbin/serverdroid-nfs refresh
    ;;
remove)
    [ -r "$STATE/$MDEV" ] || exit 0
    dir=$(cat "$STATE/$MDEV")
    rm -f "$STATE/$MDEV"
    [ -x /usr/local/sbin/serverdroid-nfs ] && /usr/local/sbin/serverdroid-nfs refresh   # stop exporting before the directory goes away, if installed
    umount -l "$dir" 2> /dev/null                        # the disk is already gone: lazy, so that open files do not block it
    rmdir "$dir" 2> /dev/null
    log "/dev/$MDEV removed, $dir unmounted"
    ;;
esac
exit 0
