#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Matheus Tagliari
# One host build for the Motorola moto g50 5G (saipan, XT2149-1): from the pinned sources to a boot image and a root filesystem, in an output
# directory of your choice. It never talks to a phone: writing the two artifacts to a device is a separate, deliberate step (build/README.md).
#
#   build/build.sh --firmware FIRMWARE.zip --alpine alpine-minirootfs-VERSION-aarch64.tar.gz --pubkey ROOT_AUTHORIZED_KEY.pub --out OUT [--jobs N] [--stages "..."]
#
# Stages (default: all, in this order; each can be repeated alone and skips what it needs from earlier ones that is already in OUT):
#   check    tools, compiler, input hashes (the firmware zip, the Alpine tarball)
#   fetch    Linux v7.2 and the reference port at their pinned commits; apply its 136 patches, then this project's 26 (git am, no conflicts allowed)
#   kernel   the pinned configuration, the board's compatibility device tree from YOUR stock boot.img, Image.gz and the board DTB
#   boot     boot-ram.img: the stock boot.img header and command line with this kernel, this DTB and an Alpine initramfs (boot/build.py)
#   rootfs   rootfs.ext4: Alpine, the overlay, the terminal built against an aarch64 sysroot, your SSH key (rootfs/build.sh)
#   manifest MANIFEST.txt: the pins, tool versions and the sha256 of every input and output
# Outputs: OUT/boot-ram.img  OUT/rootfs.ext4  OUT/kernel/{Image.gz,mt6833-motorola-saipan.dtb,config}  OUT/MANIFEST.txt
# Needs (Debian 13): git make gcc flex bison bc libssl-dev libelf-dev python3 cpio gzip openssl mkbootimg gcc-aarch64-linux-gnu fakeroot e2fsprogs curl
set -euo pipefail
export PATH=$PATH:/usr/sbin:/sbin
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
# shellcheck source=pins.env
. "$HERE/pins.env"

FIRMWARE=; ALPINE=; PUBKEY=; OUT=; JOBS=$(nproc); STAGES="check fetch kernel boot rootfs manifest"
while [ $# -gt 0 ]; do
    case $1 in
    --firmware) FIRMWARE=$2; shift 2 ;;
    --alpine) ALPINE=$2; shift 2 ;;
    --pubkey) PUBKEY=$2; shift 2 ;;
    --out) OUT=$2; shift 2 ;;
    --jobs) JOBS=$2; shift 2 ;;
    --stages) STAGES=$2; shift 2 ;;
    *) sed -n '2,19p' "$0"; exit 2 ;;
    esac
done
die() { echo "build.sh: $*" >&2; exit 1; }
say() { echo "== $*"; }
[ -n "$OUT" ] || die "--out is required"
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
SRC=$OUT/src; LINUX=$SRC/linux; A22X=$SRC/a22x-mainline; KB=$OUT/kbuild; KOUT=$OUT/kernel
export KBUILD_BUILD_USER KBUILD_BUILD_HOST KBUILD_BUILD_TIMESTAMP KBUILD_BUILD_VERSION
MAKE_K=(make -C "$LINUX" O="$KB" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-)
want() { case " $STAGES " in *" $1 "*) return 0 ;; *) return 1 ;; esac; }
sha() { sha256sum "$1" | cut -d' ' -f1; }

stage_check() {
    say "check: tools and inputs"
    for t in git make gcc flex bison bc python3 cpio gzip openssl mkbootimg unpack_bootimg aarch64-linux-gnu-gcc fakeroot mke2fs curl; do
        command -v "$t" > /dev/null || die "missing tool: $t (Debian 13: see the header of this script)"
    done
    got=$(aarch64-linux-gnu-gcc --version | head -n 1)
    [ "$got" = "$GCC_EXPECT" ] || echo "WARNING: compiler is '$got', the configuration was resolved with '$GCC_EXPECT': the kernel will build, but it is not the tested toolchain" >&2
    [ -f "$FIRMWARE" ] || die "--firmware: the stock firmware zip is required (your own download for XT2149-1)"
    [ "$(sha "$FIRMWARE")" = "$FIRMWARE_SHA256" ] || die "firmware zip sha256 is not the tested build (S1RSS32.38-20-9-13): $(sha "$FIRMWARE")"
    [ -f "$ALPINE" ] || die "--alpine: $ALPINE_FILE is required"
    [ "$(sha "$ALPINE")" = "$ALPINE_SHA256" ] || die "Alpine tarball sha256 differs from the pin: $(sha "$ALPINE")"
    [ -f "$PUBKEY" ] || die "--pubkey: the SSH public key that will be root's authorized key is required"
    grep -q '^ssh-' "$PUBKEY" || die "--pubkey does not look like an SSH public key"
}

stage_fetch() {
    say "fetch: Linux $LINUX_TAG and the reference port"
    mkdir -p "$SRC"
    if [ ! -e "$LINUX/.git" ]; then
        git clone --depth 1 --branch "$LINUX_TAG" "$LINUX_URL" "$LINUX"
    fi
    [ "$(git -C "$LINUX" rev-parse "$LINUX_TAG^{commit}")" = "$LINUX_COMMIT" ] || die "$LINUX_TAG is not commit $LINUX_COMMIT"
    if [ ! -e "$A22X/.git" ]; then
        git clone "$A22X_URL" "$A22X"
    fi
    git -C "$A22X" checkout -q "$A22X_COMMIT" || die "reference port commit $A22X_COMMIT not found"
    n=$(git -C "$LINUX" rev-list --count "$LINUX_COMMIT..HEAD")
    if [ "$n" = 0 ]; then
        say "fetch: applying $A22X_PATCHES reference patches, then $OWN_PATCHES of this project"
        series=$(ls "$A22X"/kernel/patches/*.patch | wc -l)
        [ "$series" = "$A22X_PATCHES" ] || die "the reference port has $series patches, expected $A22X_PATCHES"
        own=$(ls "$REPO"/mainline/patches/*.patch | wc -l)
        [ "$own" = "$OWN_PATCHES" ] || die "mainline/patches has $own patches, expected $OWN_PATCHES"
        git -C "$LINUX" checkout -q -b serverdroid
        g=(git -C "$LINUX" -c user.name=serverdroid-build -c user.email=build@localhost -c core.hooksPath=/dev/null)
        "${g[@]}" am --quiet "$A22X"/kernel/patches/*.patch || die "git am failed on the reference patches in $LINUX"
        "${g[@]}" am --quiet "$REPO"/mainline/patches/*.patch || die "git am failed on mainline/patches in $LINUX"
        n=$(git -C "$LINUX" rev-list --count "$LINUX_COMMIT..HEAD")
    fi
    [ "$n" = $((A22X_PATCHES + OWN_PATCHES)) ] || die "$n commits above $LINUX_TAG, expected $((A22X_PATCHES + OWN_PATCHES)): $LINUX is not the pinned tree"
    echo "source tree: $LINUX_TAG + $n patches, tree $(git -C "$LINUX" rev-parse 'HEAD^{tree}')"
}

stage_kernel() {
    say "kernel: configuration, compatibility device tree, Image.gz, DTB"
    [ -e "$LINUX/.git" ] || die "run the fetch stage first"
    mkdir -p "$KB" "$KOUT"
    cp "$REPO/mainline/saipan.config" "$KB/.config"
    "${MAKE_K[@]}" olddefconfig > /dev/null
    # olddefconfig must not change a resolved configuration; if it does, the compiler or the tree is not the one it was resolved with.
    if ! diff -q <(grep -v '^#' "$REPO/mainline/saipan.config") <(grep -v '^#' "$KB/.config") > /dev/null; then
        echo "WARNING: olddefconfig changed the pinned configuration (compiler or tree differ); see $KOUT/config.diff" >&2
        diff <(grep -v '^#' "$REPO/mainline/saipan.config") <(grep -v '^#' "$KB/.config") > "$KOUT/config.diff" || true
    fi
    # The board's tree is the stock one with unneeded nodes disabled: generated from the stock boot.img of the owner's own firmware, with
    # the dtc of this kernel tree (the one of the old Motorola 4.14 tree drops labels the board file needs).
    "${MAKE_K[@]}" scripts_dtc > /dev/null
    tmp=$(mktemp -d); trap 'rm -rf "$tmp"' RETURN
    python3 -I -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]).extract('boot.img', sys.argv[2])" "$FIRMWARE" "$tmp"
    python3 -I "$HERE/extract-dtb.py" "$tmp/boot.img" "$tmp/stock.dtb"
    PATH="$KB/scripts/dtc:$PATH" python3 -I "$A22X/tools/gen-lk-compat-dtsi.py" "$tmp/stock.dtb" \
        "$LINUX/arch/arm64/boot/dts/mediatek/mt6833-motorola-saipan-lk-compat.dtsi"
    echo "compat dtsi: $(wc -l < "$LINUX/arch/arm64/boot/dts/mediatek/mt6833-motorola-saipan-lk-compat.dtsi") lines"
    "${MAKE_K[@]}" -j"$JOBS" Image.gz mediatek/mt6833-motorola-saipan.dtb
    cp "$KB/arch/arm64/boot/Image.gz" "$KOUT/Image.gz"
    cp "$KB/arch/arm64/boot/dts/mediatek/mt6833-motorola-saipan.dtb" "$KOUT/mt6833-motorola-saipan.dtb"
    cp "$KB/.config" "$KOUT/config"
    echo "kernel: $(strings "$KB/vmlinux" 2> /dev/null | grep -m1 'Linux version' || true)"
}

stage_boot() {
    say "boot: boot-ram.img (nothing is flashed)"
    [ -f "$KOUT/Image.gz" ] || die "run the kernel stage first"
    DTB="$KOUT/mt6833-motorola-saipan.dtb" EXTRA_CMDLINE="$BOOT_EXTRA_CMDLINE" \
        python3 -I "$REPO/boot/build.py" "$FIRMWARE" "$ALPINE" "$OUT/boot" "$KOUT/Image.gz"
    mv "$OUT/boot/boot-ram.img" "$OUT/boot-ram.img"; rmdir "$OUT/boot"
}

stage_rootfs() {
    say "rootfs: rootfs.ext4 (nothing is flashed)"
    sh "$REPO/rootfs/build.sh" "$ALPINE" "$PUBKEY" "$OUT/rootfs-build"
    cp "$OUT/rootfs-build/rootfs.ext4" "$OUT/rootfs.ext4"
}

stage_manifest() {
    say "manifest"
    {
        echo "ServerDroid build manifest (moto g50 5G, saipan, XT2149-1)"
        echo "repository: $(git -C "$REPO" rev-parse HEAD 2> /dev/null || echo unknown)$(git -C "$REPO" status --porcelain 2> /dev/null | grep -q . && echo ' (+ uncommitted changes)')"
        echo "linux: $LINUX_TAG $LINUX_COMMIT, patches $A22X_PATCHES (reference $A22X_COMMIT) + $OWN_PATCHES (mainline/patches)"
        [ -e "$LINUX/.git" ] && echo "source tree: $(git -C "$LINUX" rev-parse 'HEAD^{tree}')"
        echo "compiler: $(aarch64-linux-gnu-gcc --version | head -n 1); binutils: $(aarch64-linux-gnu-ld --version | head -n 1)"
        echo "identity: $KBUILD_BUILD_USER@$KBUILD_BUILD_HOST, timestamp '$KBUILD_BUILD_TIMESTAMP', build $KBUILD_BUILD_VERSION"
        echo "inputs:"; echo "  firmware.zip  $(sha "$FIRMWARE")"; echo "  alpine        $(sha "$ALPINE")"
        echo "  pinned config $(sha "$REPO/mainline/saipan.config")"
        echo "outputs:"
        for f in kernel/Image.gz kernel/mt6833-motorola-saipan.dtb kernel/config boot-ram.img rootfs.ext4; do
            [ -f "$OUT/$f" ] && echo "  $f  $(sha "$OUT/$f")  $(stat -c %s "$OUT/$f") bytes"
        done
    } > "$OUT/MANIFEST.txt"
    cat "$OUT/MANIFEST.txt"
}

for s in check fetch kernel boot rootfs manifest; do
    want "$s" || continue
    case $s in
    check) stage_check ;; fetch) stage_fetch ;; kernel) stage_kernel ;; boot) stage_boot ;; rootfs) stage_rootfs ;; manifest) stage_manifest ;;
    esac
done
say "done: $OUT"
