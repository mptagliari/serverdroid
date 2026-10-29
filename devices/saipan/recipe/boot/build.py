#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Matheus Tagliari
"""Build a RAM-bootable ServerDroid image from the stock Motorola boot.img.

The stock kernel, DTB, cmdline and header fields are kept byte-for-byte;
only the ramdisk is replaced with an Alpine minirootfs plus boot/init.

Usage: build.py FIRMWARE_ZIP ALPINE_MINIROOTFS_TARBALL OUT_DIR [KERNEL_IMAGE_GZ]
Requires: mkbootimg and unpack_bootimg (Debian package "mkbootimg"), cpio, gzip,
aarch64-linux-gnu-gcc (bare cross compiler is enough).
Environment: EXTRA_CMDLINE is appended to the kernel command line, e.g. to trace boot:
  EXTRA_CMDLINE='trace_event=clk:*,regulator:*,power:power_domain_target,power:clock_enable,power:clock_disable,power:clock_set_rate,power:clock_set_parent trace_buf_size=16M'
Nothing is flashed. The Motorola bootloader has no "fastboot boot": flash the result
with "fastboot flash boot_a OUT_DIR/boot-ram.img" (keep the stock boot.img to restore).
"""
import hashlib
import os
import re
import shlex
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, **kw)


def md5(path):
    return hashlib.md5(path.read_bytes()).hexdigest()


def main():
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__)
    firmware, alpine, out = (Path(a).resolve() for a in sys.argv[1:4])
    kernel = Path(sys.argv[4]).resolve() if len(sys.argv) > 4 else None
    out.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        with zipfile.ZipFile(firmware) as z:
            z.extract("boot.img", tmp)
            manifest = z.read("flashfile.xml").decode()

        # Check boot.img against the MD5 listed in the firmware's own manifest.
        m = re.search(r'MD5="([0-9a-f]{32})" filename="boot.img"', manifest)
        if not m or m.group(1) != md5(tmp / "boot.img"):
            sys.exit("boot.img does not match flashfile.xml")

        unpacked = tmp / "unpacked"
        args = run(
            ["unpack_bootimg", "--boot_img", str(tmp / "boot.img"),
             "--out", str(unpacked), "--format=mkbootimg"],
            capture_output=True, text=True).stdout
        args = shlex.split(args)

        # Initramfs: Alpine minirootfs + our init.
        root = tmp / "root"
        root.mkdir()
        run(["tar", "-xzf", str(alpine), "-C", str(root)])
        init = root / "init"
        init.write_bytes((HERE / "init.sh").read_bytes())
        init.chmod(0o755)
        # reboot-bootloader: lets the running phone go back to fastboot without keys.
        (root / "usr/sbin").mkdir(parents=True, exist_ok=True)
        run(["aarch64-linux-gnu-gcc", "-static", "-nostdlib", "-ffreestanding", "-Os",
             "-o", str(root / "usr/sbin/reboot-bootloader"),
             str(HERE.parent / "tools/reboot-bootloader.c")])
        files = run(["find", ".", "-print0"], cwd=root,
                    capture_output=True).stdout
        cpio = run(["cpio", "--null", "-o", "-H", "newc", "--owner=0:0", "--quiet"],
                   cwd=root, input=files, capture_output=True).stdout
        ramdisk = tmp / "ramdisk.cpio.gz"
        ramdisk.write_bytes(run(["gzip", "-9", "-n"], input=cpio,
                                capture_output=True).stdout)

        # Swap only the ramdisk path in the original mkbootimg arguments.
        i = args.index("--ramdisk")
        args[i + 1] = str(ramdisk)
        if kernel:   # optional: boot a kernel we built instead of the stock one
            args[args.index("--kernel") + 1] = str(kernel)
        dtb_env = os.environ.get("DTB", "").strip()
        if dtb_env:  # optional: replace the DTB; the bootloader reads it as an Android DT table
            import struct
            dtb = Path(dtb_env).read_bytes()
            hdr, ent = 0x20, 0x20
            off = hdr + ent
            table = struct.pack(">8I", 0xD7B7AB1E, off + len(dtb), hdr, ent, 1, hdr, 2048, 0)
            table += struct.pack(">8I", len(dtb), off, 0, 0, 0, 0, 0, 0) + dtb
            (tmp / "dtb.table").write_bytes(table)
            args[args.index("--dtb") + 1] = str(tmp / "dtb.table")
        extra = os.environ.get("EXTRA_CMDLINE", "").strip()
        if extra:    # appended after the stock header cmdline; the bootloader adds its own
            i = args.index("--cmdline")
            args[i + 1] = (args[i + 1] + " " + extra).strip()
        run(["mkbootimg", *args, "-o", str(out / "boot-ram.img")])

    print(out / "boot-ram.img")


if __name__ == "__main__":
    main()
