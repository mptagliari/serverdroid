# Investigation report

## Identity and variant boundaries

Motorola moto g50 5G, codename `saipan`, model **XT2149-1**, MediaTek MT6833. Only this exact model is declared (`xt2149-1`); a Qualcomm "Ibiza"
variant of the moto g50 5G exists and is explicitly **not** covered or implied by this submission; nothing here should be read as applying to it.

## Feasibility gates

- **Bootloader: feasible.** This specific unit's bootloader was unlocked through Motorola's own account-bound process before this project began.
  The unlock conditions (account sign-in, per-device eligibility, waiting period) were not re-read from Motorola's own page for this report; they
  are known from the device's own bootloader-warning prompt and from third-party install guides, and are reported as secondhand here.
- **Kernel: feasible.** A mainline Linux kernel (v7.2 at the time of writing) has repeatedly booted this physical unit as a native Linux server:
  UFS storage, MT6359P power rails, thermal management, CPU frequency scaling, RTC, backlight, a Type-C host with USB Power Delivery, a battery
  charge-limit policy, touch input and Wi-Fi are all confirmed working on hardware by the owner. The kernel recipe combines a pinned revision of a
  community mainline reference port, made for the related MT6833 "a22x" device family, with a set of 26 patches specific to this device, written
  by this project. `recipe/` commits both the resolved kernel build configuration and this project's own 26 patches (the reference port's own
  136 patches are not committed; they are fetched and applied from their own pinned repository by `recipe/build/build.sh`, as a GPL-2.0 source
  in its own right, never vendored here).
- **Storage, network and power: feasible.** Running on the physical unit, with SSH administration and no Android userspace: UFS storage mounts
  and is used as the root filesystem; Wi-Fi associates and routes; USB host and device networking both work. Charging works, governed by a
  charge-limit policy. Two gaps remain open on this unit: a warm reboot currently leaves the display panel black until the Power key is pressed,
  and there is no software-initiated power-off yet.

## Claims and limitations

No claim above `not_tested` is made in `device.toml`, and `device.toml` carries no `build` table. `recipe/` is a real, complete, buildable copy
of what produced the working unit this report describes — file-by-file reviewed for private identifiers, per-unit calibration and out-of-project
wiring (see "What was reviewed and changed when committing the recipe" below) — but **the evidence protocol's `build` claim is a stricter,
machine-checkable thing than "the files are here"**: it needs the resolved recipe's exact artifact set hashed (`docs/evidence-protocol.md`,
"Optional build section": `artifacts`, "every component of the tested system"), and this submission was not accompanied by a fresh, from-empty-
directory run of this exact recipe in which every output, including `rootfs.ext4`, was rebuilt and hashed. The project's own `build/README.md`
already records that `rootfs.ext4` specifically was **not yet verified** from an empty directory as of the last clean-build check; asserting a
`build` claim here, with a fabricated or stale artifact hash for it, would be exactly the kind of unverified claim CLAUDE.md's evidence-labeling
rule forbids. So: the files are real and usable right now by anyone who runs them; the formal claim that a specific, hashed artifact set comes
out the other end is left for whoever (the maintainer or another owner of an identical unit) next runs `recipe/build/build.sh` end to end and
submits that run's manifest and sanitized logs. Derived maturity stays **Investigated** for `xt2149-1`, unchanged by committing the recipe files.

## What was reviewed and changed when committing the recipe

The recipe in `recipe/` is not a byte-for-byte copy of the maintainer's own private tree. Reviewed and changed before committing it:

- **Out-of-project services removed.** The maintainer's own unit also runs Docker and NFS, which are the maintainer's personal use of the
  hardware, not ServerDroid features (the project's own CLAUDE.md: "nothing public may name another project" and "no wiring for it belongs in
  the core or the public tree"). `rootfs/build.sh` no longer installs `docker docker-cli containerd runc iptables ip6tables unfs3 rpcbind`;
  `rootfs/overlay/etc/serverdroid-rc.sh` no longer starts a container runtime or an NFS export daemon; `serverdroid-automount.sh` still *supports*
  an optional NFS-export tool if one is installed later, but requires nothing of the kind.
- **A real private identifier found and fixed.** Eight of this device's own 26 kernel patches carried the maintainer's personal email address in
  their Git commit metadata (`From:` header). It was replaced with their public, GitHub-provided no-reply address before committing; nothing else
  in any patch was changed.
- **A static-screening constraint, not a content change.** This repository's own file-extension allow-list (`tools/validate-submission`) does not
  accept extensionless files. Every script that is installed under a name with no extension (`/init`, `/etc/serverdroid-rc`, and several
  `/usr/local/{bin,sbin}/*` tools) is committed with a `.sh` suffix instead; `rootfs/build.sh` renames each one back to its real name when it
  assembles the image, and `recipe/README.md`'s "Naming note" explains this. Three very small, static configuration files (`/etc/inittab`,
  `/etc/motd`, `/etc/mdev.conf`) are not committed as separate files at all, for the same reason; `rootfs/build.sh` writes their real content
  directly. None of this changes what gets installed on the phone.
- **Not reviewed as hardware-tested for this exact commit.** The files are the ones already running on the physical unit (same logic, same
  patches, same configuration), but this submission did not re-run the recipe from a clean checkout to reconfirm that; see "Claims and
  limitations" above.

## Provenance and licensing

`recipe/mainline/patches/` holds this device's own 26 kernel patches, each a separate file under `GPL-2.0-only` or the license of the file it
touches (several new `.dts` files already carry their own `SPDX-License-Identifier` line, e.g. `GPL-2.0-only OR MIT`), never combined into or
relicensed as part of this project's `GPL-3.0-only` original material, per `LICENSING.md` question 1. `recipe/mainline/saipan.config` is the
resolved kernel configuration, itself a derivative of Linux's own Kconfig system (GPL-2.0). None of the 136 reference-port patches, the Linux
kernel source itself, or the Alpine Linux root filesystem are committed: `recipe/build/pins.env` pins their exact revisions and the recipe fetches
them from their own repositories at build time, per `LICENSING.md` question 2's default (reference, don't vendor, unless a device-specific patch
is needed — which is exactly this device's own 26 patches). No vendor firmware, binary, per-unit calibration or other private identifier is
included; see "What was reviewed and changed when committing the recipe" above for what was found and fixed.
