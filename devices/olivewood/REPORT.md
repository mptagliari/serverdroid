# Investigation report

Desk research on 2026-10-29 from public pages, plus a read-only check of an owned unit added the same day. Nothing was written to the device; no
bootloader action, no flash, no partition read.

## Identity and variant boundaries

The Redmi 8A Dual belongs to the Xiaomi SDM439 family (Snapdragon 439, eight Cortex-A53). The shared LineageOS device tree accepts the board names pine, olive,
olivelite and olivewood, and the Wikipedia page gives the codename olivewood to the 8A Dual and 8A Pro.

**The sources disagreed on the model number; an owned unit resolves it for that unit.** The LineageOS wiki data lists M2001C3K3I; Wikipedia lists M2003C3K3I
(shared with the 8A Pro, suggesting Wikipedia's row conflates the two). The maintainer's own unit's Settings > About phone screen reads **M2001C3K3I**,
matching the LineageOS data; `device.toml` now declares the single variant `m2001c3k3i`. This confirms one real unit, not the whole family: a different
unit could still legitimately read M2003C3K3I (for instance an 8A Pro), and nothing here extends this submission's claims to one.

A read-only `adb shell getprop` check of that same unit (device owner authorized USB debugging; no bootloader action) found: codename `olivewood`
confirmed in software (`ro.product.device`); Android 10, security patch 2021-12-01 (an old, unmodified stock build); platform `msm8937`; region BR,
`cust_variant=br`; **bootloader currently locked**, verified boot state `green` (`ro.boot.flash.locked=1`), the stock condition, not yet attempted to
unlock; and a 32-bit-only userspace (`ro.product.cpu.abilist64` is empty), despite the SoC's 64-bit-capable cores. The device's IMEI was incidentally
exposed by an unfiltered `getprop` dump during this check; it was not recorded anywhere, is not in this submission, and must never be.

## Feasibility gates

- **Bootloader: unknown.** LineageOS installs this device with fastboot, which suggests an unlockable bootloader. The vendor's unlock FAQ could not be read
  (HTTP 403); search summaries gave conflicting waiting times. The owned unit's bootloader is currently locked (its stock state); whether and how it can be
  unlocked through Xiaomi's official process was not attempted and is not assessed by reading the lock state alone.
- **Kernel: feasible, with limits.** A maintained downstream 4.19 tree (4.19.325 in its Makefile) serves the family and its board list names olivewood. No
  upstream SDM439 device tree was found. postmarketOS ships a mainline-based fork for the msm89x7 family; its coverage of olivewood was not checked.
- **Storage, network and power: unknown.** eMMC 5.1 storage (Wikipedia) and 802.11 b/g/n Wi-Fi (LineageOS data) are reported. Charging and battery behaviour were
  not sourced for olivewood, and a search summary of the postmarketOS page for the sibling olive reportedly lists the battery as not working; that page was not read.
  The owned unit's stock Android build is 32-bit only; this is a fact about the vendor ROM, not a constraint on a replacement 64-bit userspace.

Because two gates are unknown, the derived level is Unassessed.

## Claims and limitations

No claim above `not_tested`. No recipe, build, boot or physical test. Pages the author could not read, and the unconfirmed state of the upstream MSM8937 patches,
are marked as such in `device.toml`. All facts are paraphrased from the cited pages; none of their text, files or lists was copied.

## Provenance and licensing

No source, patch, binary, firmware, calibration or third-party material is imported; only links and paraphrased facts.
