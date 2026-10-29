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
Original material, templates, and two research-only device submissions (`devices/olivewood/`, `devices/saipan/`): research-gate narratives and
external source references only. **No third-party and no kernel-derived file is included**, so the compatibility questions below do not arise from
what is committed yet; they are answered below as *policy*, ahead of the first submission that would actually commit a kernel patch, build
configuration or generated device tree.

## Open decision
- **`GPL-3.0-only` or `GPL-3.0-or-later`** for original files. The files here use `GPL-3.0-only`; the `LICENSE` text does not decide it, so this is the
  maintainer's choice to confirm or change (a change means editing the SPDX line of every original file).

## Questions to answer before importing any device material

Answered as policy below, ahead of a submission that would actually commit the files in question; `devices/saipan/REPORT.md` records that its own
kernel recipe (patches, build configuration) is deliberately not yet committed, pending exactly this review, file by file.

1. **Kernel patches under GPL-2.0-only next to GPL-3.0 files: answered.** A patch file is never merged into or relicensed as part of a GPL-3.0
   original file. If and when kernel patches are committed, they are committed as their own files, under GPL-2.0(-only) or the licence of the file
   they touch, with their origin and revision recorded in the device profile's `sources`. No single file combines both licences.
2. **Patches and sources from other projects (reference ports, vendor drivers): answered as a default, case reviewed per source.** The default is to
   *reference* a source by URL, pinned revision and licence in the device profile, not to vendor its files, unless the recipe needs a locally
   committed, device-specific patch — which is then reviewed individually (redistribution rights, licence compatibility, content) before commit.
3. **Device trees derived from a vendor's stock tree: answered.** They are produced locally by the recipe, from firmware the operator supplies
   themselves, and are never committed here, matching `build/README.md`'s existing practice on the reference recipe.
4. **Fonts, artwork, icons:** no change; still open if any are ever added (none exist today).
5. **Binary releases:** no change; still open, and not raised by this submission, which distributes no binary.
6. **Documentation licence: answered.** Documentation is original material under the same `GPL-3.0-only` as code (`LICENSE` already covers the whole
   tree's original material; no separate licence is introduced for docs). **Sign-off (e.g. a DCO) is still open**, unresolved by this submission.
7. **A contribution that includes third-party code:** no change; still the rule for any future contribution, enforced at review time.
