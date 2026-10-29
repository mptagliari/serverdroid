# saipan (Motorola moto g50 5G, XT2149-1): research gates, plus a complete build recipe

Investigated-level submission: all three research gates (bootloader, kernel, storage/network/power) are documented feasible with real external
sources, matching the owner's own hardware findings. `recipe/` additionally carries the actual kernel patches, build configuration, boot image
builder and root filesystem overlay the maintainer runs on the physical unit (`recipe/README.md`), stripped of the maintainer's own personal,
out-of-project services (Docker, NFS) per this project's scope. **No `build` or `boot` claim is made in `device.toml`**: that needs a real run
manifest and sanitized logs this submission does not include. See `REPORT.md` for exactly what that means.
