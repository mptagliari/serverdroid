# Device submission template

This is a structural example, **not a device or evidence of a port**. The validator
checks it as data but excludes it from the device maturity report. Copy this folder
to `devices/<codename>/`, change the codename to match the folder, and replace the
placeholder model, variant, research and source references with real research.

Start without a `[build]` section or evidence directory. A research-only PR is
valid. The gate outcomes are `unknown`, `feasible` or `blocked`. All three must be
`feasible` or `blocked`, with references and a report, to derive Investigated.

When a recipe exists, follow the build and run contracts in
`docs/evidence-protocol.md`. Never fill evidence fields with dummy success values.
The synthetic fixtures under `tests/` show machine formats, not hardware claims.
