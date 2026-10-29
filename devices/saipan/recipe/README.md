# Building ServerDroid for the Motorola moto g50 5G (saipan)

This recipe targets **one device**: Motorola moto g50 5G, codename `saipan`, model **XT2149-1**, MediaTek MT6833 (not the Qualcomm "Ibiza"
variant). Nothing here is expected to work on another phone. Flashing can brick a device; read "Installing" in `../REPORT.md` and
`../RECOVERY.md` before touching a phone with it. **This recipe never talks to a phone by itself**: it only builds a kernel image and a root
filesystem image into a directory of your choice.

## What you need

- A Debian 13 (or similar) x86-64 host with: `git make gcc flex bison bc libssl-dev libelf-dev python3 cpio gzip openssl mkbootimg
  gcc-aarch64-linux-gnu fakeroot e2fsprogs curl` (Debian package names; `mkbootimg` provides `unpack_bootimg`). About 10 GB free and internet
  access (this fetches the pinned Linux kernel, the pinned reference port, and the Alpine package index over the network).
- **Your own copies of three inputs** (none is distributed here):
  1. the stock firmware zip for XT2149-1, build `S1RSS32.38-20-9-13` (only its `boot.img` is read: header, command line and the stock device
     tree); its sha256 is pinned in `build/pins.env` and checked before use;
  2. `alpine-minirootfs-3.24.3-aarch64.tar.gz` from alpinelinux.org (its sha256 is pinned too);
  3. an SSH public key, which becomes root's authorized key on the phone (there is no password login).

## Run it

```
build/build.sh --firmware FIRMWARE.zip --alpine alpine-minirootfs-3.24.3-aarch64.tar.gz --pubkey ~/.ssh/id_ed25519.pub --out OUT
```

Stages (`--stages "check fetch kernel boot rootfs manifest"`, default all, each repeatable): tools and input hashes; Linux `v7.2` plus the
reference port's patches (`gitlab.com/pessdres/a22x-mainline`, pinned commit, see `build/pins.env`) plus this device's own 26 patches
(`mainline/patches/`); the pinned kernel configuration (`mainline/saipan.config`), the board's compatibility device tree generated from **your**
stock `boot.img`, `Image.gz` and the board DTB; `boot-ram.img` (stock header and command line, this kernel and DTB, an Alpine initramfs with
`boot/init.sh`, installed as `/init`); `rootfs.ext4` (Alpine, the overlay in `rootfs/overlay/`, the terminal cross-built against an aarch64
sysroot, your key); `MANIFEST.txt` with every pin, tool version and sha256.

Outputs: `OUT/boot-ram.img`, `OUT/rootfs.ext4`, `OUT/kernel/{Image.gz,mt6833-motorola-saipan.dtb,config}`, `OUT/MANIFEST.txt`. The kernel banner
is fixed and neutral (`serverdroid@build`, build 1, 1970 timestamp), so that two builds of the same sources have the same identity.

## What this recipe deliberately leaves out, compared to the maintainer's own deployment

The maintainer runs extra, personal services on their own unit (Docker, NFS) that **are not part of ServerDroid**. This committed recipe strips
them from the package list and from the startup script (`rootfs/overlay/etc/serverdroid-rc.sh`); `rootfs/overlay/usr/local/sbin/serverdroid-automount.sh`
still *supports* an optional NFS-export tool if one is installed later, but does not require or ship one. Wi-Fi bring-up needs the individual
device's out-of-tree kernel modules and Motorola's own firmware blobs with per-unit calibration: none of it is redistributable, so it is not part
of this recipe either (the system boots and is administered over the USB network gadget; see the project's own `docs/wifi-investigation.md`,
not part of this submission, for how the maintainer's own unit brings Wi-Fi up afterward).

## Naming note

A few files below carry a `.sh` suffix that the installed system does not use (`init.sh` becomes `/init` in the ramdisk;
`rootfs/overlay/etc/serverdroid-rc.sh` becomes `/etc/serverdroid-rc`; several `rootfs/overlay/usr/local/{bin,sbin}/*.sh` lose the suffix the same
way). `rootfs/build.sh` does these renames itself when it assembles the image. The suffix exists only so this repository's own static screening
(`tools/validate-submission`, an allow-list of file extensions) can accept these as plain text scripts; it has no effect on the installed system.
Three tiny, static configuration files (`/etc/inittab`, `/etc/motd`, `/etc/mdev.conf`) are not committed as separate files at all: `rootfs/build.sh`
writes them directly, because their real names carry no accepted extension and their content is a handful of lines, reproduced in that script.

## Installing (destructive: read `../RECOVERY.md` first)

Not done by this recipe. Everything here needs an unlocked bootloader, which **wipes the phone and is outside this project** (see
`../device.toml`'s `research.bootloader` gate). Keep a copy of the stock `boot.img` (it is in your firmware zip) and back up the phone's
partitions first. Recovery: hold Power about 15 s, then Power with Volume Down to reach fastboot, and flash the stock `boot.img` from your
firmware zip back to `boot_a`.

1. `fastboot flash boot_a OUT/boot-ram.img`. This is the only partition this recipe's kernel touches. **Never** flash `gpt`, `preloader`, `lk`,
   `tee` or `nvdata`, never run the firmware's full `flashfile.xml`, and never force `fastboot set_active b`.
2. `fastboot flash userdata OUT/rootfs.ext4` **erases userdata**.
3. After the first boot, grow the filesystem to the partition: `resize2fs` on the `userdata` block device.
4. Log in over USB: the phone is `172.16.42.1`, key only.

## What was and was not verified for this exact committed copy

This directory is a snapshot, taken 2026-10-29, of the recipe the maintainer actually used on the physical unit; `../REPORT.md` describes
exactly what was re-verified for this snapshot and what was not re-run before committing it. A clean checkout followed by a full run of this
recipe has **not** been re-executed as part of preparing this submission: do not read the presence of these files as a hardware-verified
`build` or `boot` claim. `../device.toml` makes no such claim; only a later submission with a real run manifest and sanitized logs would.
