# Investigation report

Desk research on 2026-10-29 from public pages. Nothing was run on a device. Maintainer's unit: not read for this report.

## Identity and variant boundaries

The Redmi 8A Dual belongs to the Xiaomi SDM439 family (Snapdragon 439, eight Cortex-A53). The shared LineageOS device tree accepts the board names pine, olive,
olivelite and olivewood, and the Wikipedia page gives the codename olivewood to the 8A Dual and 8A Pro.

**The sources disagree on the model number, and this is unresolved.** The LineageOS wiki data lists M2001C3K3I; Wikipedia lists M2003C3K3I (shared with the 8A Pro).
Both are declared as variants (`m2001c3k3i`, `m2003c3k3i`) only so that the conflict is explicit. Neither is claimed to be a real unit, and no variant is
established. The model number printed on a unit must be read before any later work.

## Feasibility gates

- **Bootloader: unknown.** LineageOS installs this device with fastboot, which suggests an unlockable bootloader. The vendor's unlock FAQ could not be read
  (HTTP 403); search summaries gave conflicting waiting times. Nothing else was checked.
- **Kernel: feasible, with limits.** A maintained downstream 4.19 tree (4.19.325 in its Makefile) serves the family and its board list names olivewood. No
  upstream SDM439 device tree was found. postmarketOS ships a mainline-based fork for the msm89x7 family; its coverage of olivewood was not checked.
- **Storage, network and power: unknown.** eMMC 5.1 storage (Wikipedia) and 802.11 b/g/n Wi-Fi (LineageOS data) are reported. Charging and battery behaviour were
  not sourced for olivewood, and a search summary of the postmarketOS page for the sibling olive reportedly lists the battery as not working; that page was not read.

Because two gates are unknown, the derived level is Unassessed.

## Claims and limitations

No claim above `not_tested`. No recipe, build, boot or physical test. Pages the author could not read, and the unconfirmed state of the upstream MSM8937 patches,
are marked as such in `device.toml`. All facts are paraphrased from the cited pages; none of their text, files or lists was copied.

## Provenance and licensing

No source, patch, binary, firmware, calibration or third-party material is imported; only links and paraphrased facts.
