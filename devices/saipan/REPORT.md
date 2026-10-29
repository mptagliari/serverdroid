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
  community mainline reference port, made for the related MT6833 "a22x" device family, with a set of patches specific to this device, written by
  this project. Neither the reference-port patches nor this project's own patches, nor the resolved kernel build configuration, are committed in
  this submission: that is a separate, larger step (see "What this submission deliberately leaves out" below).
- **Storage, network and power: feasible.** Running on the physical unit, with SSH administration and no Android userspace: UFS storage mounts
  and is used as the root filesystem; Wi-Fi associates and routes; USB host and device networking both work. Charging works, governed by a
  charge-limit policy. Two gaps remain open on this unit: a warm reboot currently leaves the display panel black until the Power key is pressed,
  and there is no software-initiated power-off yet.

## Claims and limitations

No claim above `not_tested` is made in `device.toml`. This submission establishes that the device *is* feasible on all three gates, from real,
repeated hardware operation — but it does not yet carry a machine-checkable `build` or `boot` claim under the evidence protocol's stricter
`serverdroid-minimum` test suite (`docs/evidence-protocol.md`), because that requires committing the actual recipe (kernel patches, build
configuration, root filesystem overlay) and sanitized run logs, not only a narrative summary.

## What this submission deliberately leaves out, and why

A `build` claim needs a committed recipe file, an ordered list of committed patch files with hashes, a committed build configuration, and pinned
source revisions (`docs/evidence-protocol.md`, "Optional build section"). A `boot` claim additionally needs a run manifest and a sanitized hardware
log. All of that material currently lives in a private repository and has not yet been reviewed file-by-file for private identifiers, per-unit
calibration, or content whose redistribution rights are unclear — the protocol's own rule is that "the owner must inspect every file and full diff
before export," and that review has not happened yet. Elevating this submission from Investigated to Buildable or Boot-verified is a deliberate,
separately authorized follow-up, not an oversight here.

## Provenance and licensing

No kernel patch, build configuration, firmware, binary, calibration data or per-unit identifier is included in this submission; only links and a
narrative summary of findings already made on physical hardware. The kernel sources this device's recipe would eventually reference (upstream
Linux and the community a22x reference port) are GPL-2.0; this project's own code and documentation are GPL-3.0-only (`LICENSE`, `NOTICE.md`).
Should kernel patches be committed in a later submission, they remain separate files under GPL-2.0(-only), never combined into or relicensed as
part of the GPL-3.0 original material, per `LICENSING.md` question 1.
