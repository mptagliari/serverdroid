# Licensing and provenance

## Rule
**No single licence is applied to everything.**
- The **original, independent code and documentation** of ServerDroid are under the GNU General Public License v3 (`LICENSE`); the files carry
  `SPDX-License-Identifier: GPL-3.0-only`.
- **Files derived from the Linux kernel, or from any third party, keep their own licence and their provenance**: the licence text or SPDX line, the
  origin (URL and exact revision) and the authorship stay with the file. They are never relicensed and their notices are never stripped.
- No vendor firmware, binary blob, calibration data or image is distributed here. Sources are recorded in a device profile with a revision, a
  digest and a licence; inputs the owner must supply are recorded by name and digest only.

Attribution: the copyright lines in the files and [NOTICE.md](NOTICE.md) name the author of the idea and of the original work; the licence requires that
they travel with every copy.

## What is in this tree today
Only original material and templates. **No third-party and no kernel-derived file is included**, so the compatibility questions below do not arise yet:
they must be answered before the first import of a device recipe, not before this tree is published.

## Open decision
- **`GPL-3.0-only` or `GPL-3.0-or-later`** for original files. The files here use `GPL-3.0-only`; the `LICENSE` text does not decide it, so this is the
  maintainer's choice to confirm or change (a change means editing the SPDX line of every original file).

## Questions to answer before importing any device material
1. **Kernel patches under GPL-2.0-only next to GPL-3.0 files.** Patch files derived from Linux must stay GPL-2.0-only (or the licence of the file they
   touch). Are they separate works from the GPL-3.0 original files, or does any file combine both? GPL-2.0-only and GPL-3.0 are not compatible for a
   combination.
2. **Patches and sources from other projects** (reference ports, vendor drivers): the licence of each, and whether it can be redistributed here, as
   patches, next to GPL-3.0 files. Record licence and revision in the device profile, per source.
3. **Device trees** built or generated from a vendor's stock tree are derivatives of it: know the licence before committing any generated file; prefer
   producing them locally in the recipe.
4. **Fonts, artwork, icons, if any are added:** their own licences; a font under another licence cannot simply be placed under the GPL.
5. **Binary releases** carry GPL-2.0 kernel code and third-party packages, so each needs a source offer and a notice of the components and their
   licences. They are reviewed separately from sources.
6. **Documentation licence,** and whether contributions need a sign-off (for example a Developer Certificate of Origin).
7. **A contribution that includes third-party code** must say where it comes from and under which licence; the maintainer decides whether it is accepted.
