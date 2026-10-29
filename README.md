# ServerDroid

**Experimental — not a release or a supported-device list. Nothing here has been reviewed by anyone but its author.**

ServerDroid develops a practical method for investigating and porting Android
devices to native Linux servers, assisted by agents from any provider. The
original proof of concept ran on a Motorola Moto G50 5G (`saipan`); its recipe and
hardware evidence have not been imported here. This repository does not yet
contain a working device recipe.

The goal is to scale an engineering method, not compatibility claims. A useful
contribution can conclude that a device is blocked. What an operator subsequently
runs on their server is outside this project's scope.

## Start here

1. Read [the agent contract](AGENTS.md) and [porting playbook](docs/porting-playbook.md).
2. Copy [the template](devices/TEMPLATE/README.md) to `devices/<codename>/`.
3. Follow [the evidence protocol](docs/evidence-protocol.md). Research-only
   submissions need no build or physical tests. Leave unknowns explicit.
4. Run `python3 -I tools/validate-submission --root . --text` with Python 3.11 or later: the report keeps three things apart (what you claim, what the tools demonstrate, what still needs physical validation). Without `--text` it prints JSON.
5. Open a PR. Tools check consistency, an independent agent advises, a human
   maintainer decides. Nothing merges or gains hardware attestation automatically.

The validator uses only Python's standard library. It reads candidate files as
data, never runs their commands, accesses hardware, downloads sources, or builds
recipes. `python3 -I -m unittest discover -s tests -v` runs synthetic positive and
negative tests. Test fixtures never represent real hardware results.

## Current boundaries

Implemented as drafts: contribution guidance, TOML/JSON contracts, static screening,
known-test criteria, per-variant maturity derivation, validator tests and a hosted
CI definition. CI bootstrap and approval boundaries are described in
[the protocol](docs/evidence-protocol.md#ci-and-review-trust).

Not implemented: evidence collection, automated rebuilds, hardware attestation,
challenge runs, assisted flashing, device imports or a public release. A clean
validator result is a consistency check, not a security or compatibility guarantee.

## Authorship

Idea and original work: **Matheus Tagliari** ([@mptagliari](https://github.com/mptagliari)). Please keep [the notice](NOTICE.md) and the
copyright lines in copies and derivative works.

## License

Original independent material in this tree is GPL-3.0-only; see [LICENSE](LICENSE).
No third-party device or kernel code is included. Future imports must preserve
their original licenses and document provenance; a top-level license must never
be applied indiscriminately to derived files. [LICENSING.md](LICENSING.md) states the rule, the decision
that is still open and the questions to answer before any import.
