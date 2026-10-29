#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Matheus Tagliari
"""Print the stock kernel device tree of a Motorola boot.img as a plain DTB.

usage: extract-dtb.py BOOT_IMG OUT.dtb      (needs unpack_bootimg from the Debian package "mkbootimg")
The bootloader keeps the tree in an Android DT table (magic 0xD7B7AB1E); entry 0 is the one the stock kernel is booted with (dtb_idx=0).
"""
import struct, subprocess, sys, tempfile
from pathlib import Path

with tempfile.TemporaryDirectory() as d:
    subprocess.run(["unpack_bootimg", "--boot_img", sys.argv[1], "--out", d], check=True, capture_output=True)
    blob = (Path(d) / "dtb").read_bytes()
if blob[:4] == bytes.fromhex("d00dfeed"):
    dtb = blob
elif struct.unpack(">I", blob[:4])[0] == 0xD7B7AB1E:
    magic, total, hsize, esize, count, eoff, pagesize, version = struct.unpack(">8I", blob[:32])
    size, off = struct.unpack(">2I", blob[eoff:eoff + 8])
    dtb = blob[off:off + size]
    print(f"extract-dtb: DT table with {count} entries, using entry 0 ({size} bytes)", file=sys.stderr)
else:
    sys.exit("extract-dtb: the dtb section is neither a DTB nor an Android DT table")
if dtb[:4] != bytes.fromhex("d00dfeed"):
    sys.exit("extract-dtb: entry 0 is not a flattened device tree")
Path(sys.argv[2]).write_bytes(dtb)
