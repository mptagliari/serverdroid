#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Matheus Tagliari
# Build an ext4 root filesystem image for the phone: Alpine minirootfs + dropbear,
# e2fsprogs, the terminal and its libraries (term/, cross-built here), the overlay in
# rootfs/overlay and an SSH key for root. Runs as a normal
# user (fakeroot); nothing is written to a device.
#
# Usage: build.sh ALPINE_MINIROOTFS_TARBALL ROOT_PUBLIC_KEY OUT_DIR [SIZE]
# Requires: fakeroot, mke2fs, curl, tar, openssl, make, aarch64-linux-gnu-gcc.
set -eu

if [ "${1:-}" = --inner ]; then
    # Runs under fakeroot so file owners in the image are root.
    shift; TARBALL=$1; PUBKEY=$2; OUT=$3; SIZE=$4; HERE=$5
    ROOT=$OUT/rootfs
    rm -rf "$ROOT"; mkdir -p "$ROOT"
    tar -xzf "$TARBALL" -C "$ROOT" --numeric-owner
    "$OUT/tools/sbin/apk.static" --root "$ROOT" --arch aarch64 --usermode \
        --keys-dir "$OUT/keys/usr/share/apk/keys" \
        --repository "https://dl-cdn.alpinelinux.org/alpine/$ALPINE_BRANCH/main" \
        --repository "https://dl-cdn.alpinelinux.org/alpine/$ALPINE_BRANCH/community" \
        --no-scripts --no-cache add dropbear e2fsprogs e2fsprogs-extra zlib libvterm font-terminus ncurses-terminfo-base \
        blkid ntfs-3g
    # The committed overlay carries a .sh suffix on every file that is installed without one (the static-screening allowlist that reviews
    # this repository only accepts a handful of extensions): copy it as-is, then rename those specific paths back to their real, extension-
    # less names. A handful of tiny static config files (inittab, motd, mdev.conf) are not committed at all; they are generated below instead.
    cp -a "$HERE/overlay/." "$ROOT/"
    for f in etc/serverdroid-rc usr/local/bin/klog usr/local/bin/serverdroid-battery usr/local/bin/serverdroid-charge-policy \
             usr/local/bin/serverdroid-motd usr/local/bin/serverdroid-power-watch usr/local/bin/serverdroid-status \
             usr/local/bin/serverdroid-touch-start usr/local/bin/serverdroid-touch-stop usr/local/bin/serverdroid-ui \
             usr/local/bin/serverdroid-usb-kick usr/local/sbin/serverdroid-automount; do
        mv "$ROOT/$f.sh" "$ROOT/$f"
        chmod 755 "$ROOT/$f"
    done
    cat > "$ROOT/etc/inittab" << 'INITTAB'
::sysinit:/etc/serverdroid-rc
ttyGS0::respawn:/bin/sh -c 'exec /bin/sh </dev/ttyGS0 >/dev/ttyGS0 2>&1'
::respawn:/usr/local/bin/serverdroid-ui
::ctrlaltdel:/sbin/reboot
::shutdown:/usr/local/bin/serverdroid-touch-stop
::shutdown:/opt/serverdroid-conn/conn-down.sh
::shutdown:/bin/umount -a -r
INITTAB
    : > "$ROOT/etc/motd"
    cat > "$ROOT/etc/mdev.conf" << 'MDEV'
# Hotplug rules for busybox mdev (the kernel calls it for every device event, see serverdroid-rc).
# USB disks: mount when a partition (or a disk without partitions) appears, unmount when it goes away. The leading "-" means mdev creates no
# device node itself: devtmpfs already did. Everything else is left alone.
-sd[a-z][0-9]*	0:0	660	*/usr/local/sbin/serverdroid-automount
-sd[a-z]	0:0	660	*/usr/local/sbin/serverdroid-automount
# The nodes every program opens must be world accessible: with the catch-all below at 660 a service that drops privileges cannot open
# /dev/null ("Permission denied") and fails to start.
-null		0:0	666
-zero		0:0	666
-full		0:0	666
-random		0:0	666
-urandom		0:0	666
-tty		0:0	666
-ptmx		0:0	666
-.*		0:0	660
MDEV
    aarch64-linux-gnu-gcc -static -nostdlib -ffreestanding -Os \
        -o "$ROOT/usr/sbin/reboot-bootloader" "$HERE/../tools/reboot-bootloader.c"
    aarch64-linux-gnu-gcc -static -nostdlib -ffreestanding -Os \
        -o "$ROOT/usr/local/bin/keys" "$HERE/../tools/keys.c"
    aarch64-linux-gnu-gcc -static -nostdlib -ffreestanding -Os \
        -o "$ROOT/usr/local/bin/buzz" "$HERE/../tools/buzz.c"
    aarch64-linux-gnu-gcc -static -nostdlib -ffreestanding -Os \
        -o "$ROOT/usr/local/bin/ctsd" "$HERE/../tools/ctsd.c"
    aarch64-linux-gnu-gcc -static -nostdlib -ffreestanding -Os \
        -o "$ROOT/usr/local/bin/ctsboot" "$HERE/../tools/ctsboot.c"
    aarch64-linux-gnu-gcc -static -nostdlib -ffreestanding -Os \
        -o "$ROOT/usr/local/bin/ctsread" "$HERE/../tools/ctsread.c"
    # The terminal and the text entry: built out of tree against the aarch64 sysroot made below (term/Makefile, SYSROOT)
    rm -rf "$OUT/term-build"; mkdir -p "$OUT/term-build"; cp -a "$HERE/../term/." "$OUT/term-build/"
    make -C "$OUT/term-build" SYSROOT="$OUT/sysroot" serverdroid-term serverdroid-input > "$OUT/term-build.log" 2>&1 \
        || { cat "$OUT/term-build.log" >&2; exit 1; }
    install -m 755 "$OUT/term-build/serverdroid-term" "$OUT/term-build/serverdroid-input" "$ROOT/usr/local/bin/"
    mkdir -p "$ROOT/root/.ssh"
    cp "$PUBKEY" "$ROOT/root/.ssh/authorized_keys"
    chmod 700 "$ROOT/root" "$ROOT/root/.ssh"; chmod 600 "$ROOT/root/.ssh/authorized_keys"
    mke2fs -q -t ext4 -L serverdroid -d "$ROOT" -F "$OUT/rootfs.ext4" "$SIZE"
    exit 0
fi

[ $# -ge 3 ] || { sed -n '2,8p' "$0"; exit 1; }
TARBALL=$(readlink -f "$1"); PUBKEY=$(readlink -f "$2"); OUT=$(readlink -f "$3"); SIZE=${4:-1G}
HERE=$(cd "$(dirname "$0")" && pwd)
export ALPINE_BRANCH=v3.24
CDN=https://dl-cdn.alpinelinux.org/alpine/$ALPINE_BRANCH/main
mkdir -p "$OUT/tools" "$OUT/keys"

# Pinned bootstrap tools. Trust chain: TLS to the Alpine CDN for alpine-keys, then
# apk.static is checked against those keys, then apk checks every package.
[ -x "$OUT/tools/sbin/apk.static" ] || {
    curl -fsSL -o "$OUT/tools/apk-tools-static.apk" "$CDN/x86_64/apk-tools-static-3.0.8-r0.apk"
    tar -xzf "$OUT/tools/apk-tools-static.apk" -C "$OUT/tools" 2>/dev/null
}
[ -d "$OUT/keys/usr/share/apk/keys" ] || {
    curl -fsSL -o "$OUT/keys/alpine-keys.apk" "$CDN/aarch64/alpine-keys-2.6-r0.apk"
    tar -xzf "$OUT/keys/alpine-keys.apk" -C "$OUT/keys" 2>/dev/null
}
KEY=$OUT/keys/usr/share/apk/keys/alpine-devel@lists.alpinelinux.org-6165ee59.rsa.pub
openssl dgst -sha256 -verify "$KEY" \
    -signature "$OUT/tools/sbin/apk.static.SIGN.RSA.sha256.alpine-devel@lists.alpinelinux.org-6165ee59.rsa.pub" \
    "$OUT/tools/sbin/apk.static" > /dev/null || { echo "apk.static signature check failed" >&2; exit 1; }

# The aarch64 development sysroot the terminal is compiled against (musl, libvterm, zlib, kernel headers).
[ -d "$OUT/sysroot/usr/include" ] || {
    rm -rf "$OUT/sysroot"; mkdir -p "$OUT/sysroot"
    "$OUT/tools/sbin/apk.static" --root "$OUT/sysroot" --arch aarch64 --usermode --initdb \
        --keys-dir "$OUT/keys/usr/share/apk/keys" \
        --repository "$CDN" --repository "https://dl-cdn.alpinelinux.org/alpine/$ALPINE_BRANCH/community" \
        --no-scripts --no-cache add musl-dev libvterm-dev zlib-dev linux-headers
}

PATH=$PATH:/sbin:/usr/sbin fakeroot -- sh "$0" --inner "$TARBALL" "$PUBKEY" "$OUT" "$SIZE" "$HERE"
echo "$OUT/rootfs.ext4"
