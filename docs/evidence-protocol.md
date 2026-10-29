# Evidence protocol v1

Status: draft. Python 3.11+ and the standard library are sufficient.
The executable schema is `tools/validate-submission` version 1.1.0 (1.1.0 adds the text report, the list of failed results and `recipe_valid_for`; the schema is that of 1.0.0); unknown fields,
duplicate identifiers and JSON keys are errors. Schema evolution requires a
versioned change and migration, not reinterpretation of old evidence.

## Two axes and the limits of verification

| Claim state | Meaning |
| --- | --- |
| `proposed` | Not implemented |
| `implemented` | Identifiable code exists; this alone says nothing about functionality |
| `not_tested` | Test evidence is absent |
| `blocked` | A prerequisite prevents progress; describe it in the report |

| Declared verification provenance | Meaning |
| --- | --- |
| `none` | No verification evidence asserted |
| `build_verified` | Contributor reports a clean build |
| `hardware_reported` | Contributor supplies evidence of physical execution |
| `reproduced` | Contributor declares an independent rebuild |
| `independently_hardware_verified` | Contributor declares independent physical execution |

The last two labels require an `independent` observer declaration, but **the
validator cannot authenticate identity, independence or physical origin**. They
remain declarations requiring maintainer review. No user-supplied label grants
Replicated, Maintained, or authenticated hardware verification.

Logs, digests and timestamps establish internal consistency only. They can all be
fabricated. A nonce would address freshness, not solve attestation; challenges
are deferred. The agent's judgment must not be substituted for a deterministic
check or the human decision.

## Device profile: `devices/<codename>/device.toml`

Required top-level keys: `schema_version = 1`, `device`, `sources`, `research`,
`safety`, `claims`. `build` is optional for research-only submissions. The template
is structurally checked but excluded from the device report.

- `device`: `codename` (matches folder), `manufacturer`, public `model`, `soc`,
  `variants` (unique public variant ids). Describe variant distinctions in
  `REPORT.md`; no per-unit identifiers. Unknown variants are not implicitly covered.
- `sources`: array of `{id, url, description}`. URLs are HTTPS without credentials,
  query strings or fragments; put context in `description`. Build source records
  also require `revision` (full Git object id), `sha256`, `license`.
  Describe exactly what the hash covers (such as a source archive) and how to
  obtain it in `REPORT.md`. Non-Git source pinning is a future schema extension.
- `research`: `bootloader`, `kernel`, `hardware_minimum`. Each has `outcome`
  (`unknown`, `feasible`, `blocked`), a `summary`, and nonempty `sources` id list.
- `safety`: `recovery` (relative document path), `proposed_writes`,
  `protected_partitions`, `partition_sources` (source ids). Lists are unique;
  writes and protected lists cannot overlap. Any partition assertion needs a
  source. Empty lists mean not assessed, never safe. The validator does not
  assess correctness or authorize a write.
- `claims`: array of `{capability, state, verification, evidence}`; one per
  capability. Capabilities: `build`, `boot`, `storage`, `network`, `power`,
  `stability`. With `verification = "none"`, `evidence = []`. Otherwise state
  must be `implemented`, with references `{run = "id", test = "known.id"}`.
  Use `REPORT.md` for capabilities outside this minimal vocabulary.
- Required text files: `REPORT.md` and the recovery document named by `safety`.

### Optional build section

`build` requires all these fields:

| Field | Format |
| --- | --- |
| `recipe` | `{path, sha256}` referring to a committed device-local file |
| `revision` | Full immutable commit id of the recipe that produced the artifact |
| `variants` | Nonempty subset of `device.variants` for which this recipe is submitted |
| `sources` | Nonempty ordered list of pinned source ids |
| `toolchain` | `{name, version}` with exact version; describe acquisition in the report |
| `config` | `{path, sha256}` for the committed build configuration |
| `patches` | Ordered array of `{path, sha256}`; `[]` if none |
| `inputs` | Array of `{id, name, sha256, license, redistributable}`; metadata only |
| `artifacts` | Nonempty array of `{id, name, sha256, size}`; positive integer byte size |

All SHA-256 values are lowercase 64-character hex strings; commit ids are full
40- or 64-character hex strings. Never hash or list private per-unit calibration
as an input. Name a public firmware release and hash its generic archive only
when appropriate; a hash does not establish redistribution rights.

List **every component of the tested system** as an artifact (boot, rootfs, etc.).
Artifact files are not committed. A recipe that changes the live system after
installation must describe those changes and their provenance; an old rootfs hash
does not describe an arbitrarily modified installation. Human review must reject
that mismatch even when fields are internally consistent.

The build identity is SHA-256 of UTF-8 JSON with sorted keys, ASCII escaping and
separators `(',', ':')` of:

```json
{"build": "the complete parsed build object", "sources": "the selected source records in build.sources order"}
```

The actual values are objects and an array, not the illustrative strings above.
Run the validator before collecting evidence: its device result prints `build_id`.
Add it to run manifests after the artifact metadata is final. The recipe commit
is a pre-existing build-source revision; it is not the commit that will later
contain the evidence. This avoids a self-referential commit/hash requirement.
Changing recipe revision, toolchain, sources, config, ordered patches, inputs,
variants or artifact metadata invalidates earlier runs. Keep historical evidence
in Git history and replace stale manifests in a new submission. Text-only report
edits do not invalidate a build. A suite change requires new matching runs.

## Run manifests and logs

`evidence/run-<id>.json` (device-relative) has exactly these fields:

```json
{
  "schema_version": 1,
  "id": "owner-build",
  "subject": {
    "variant": "public-variant-id",
    "recipe_revision": "full commit id",
    "build_id": "build identity described above",
    "artifacts": {"boot": "sha256", "rootfs": "sha256"}
  },
  "suite": {"id": "serverdroid-minimum", "version": "1"},
  "environment": {"kind": "build", "description": "OS, host architecture, tools, conditions"},
  "observer": {"handle": "public handle", "relationship": "owner"},
  "started_at": "2026-10-28T00:00:00Z",
  "ended_at": "2026-10-28T00:00:01Z",
  "results": [{
    "test_id": "build.clean",
    "command": "exact command that was actually executed, recorded only",
    "exit_code": 0,
    "started_at": "2026-10-28T00:00:00Z",
    "ended_at": "2026-10-28T00:00:01Z",
    "log": {"path": "evidence/log-build.json", "sha256": "sha256 of the sanitized log"}
  }],
  "limitations": ["What was not tested or could not be established"],
  "redactions": ["Categories removed, without the private values"]
}
```

The example uses placeholders and is not valid evidence. `subject.artifacts`
must exactly equal the entire profile artifact map. One run covers one exact
variant. `environment.kind` is `build` or `hardware`; an emulator is not hardware.
Timestamps are contextualized UTC RFC3339 ending in `Z`, with tests inside the
run interval. `observer.relationship` is `owner` or `independent`.

A referenced UTF-8 JSON log has `{test_id, measurements, transcript}`. `transcript`
is the sanitized command output, nonempty; no shell execution is performed by
the validator. Its measurement record is typed and checked against the known
criteria below. The reviewer must inspect whether the transcript supports those
measurements. Hash the **sanitized** log and describe redaction categories;
never commit an unsanitized digest that fingerprints a private identifier.

### Known tests: `serverdroid-minimum` version `1`

These are recording contracts, not installed test executables. Manual execution
must be documented and authorized where appropriate; the collector is deferred.

| Test / capability | Measurements and deterministic pass criterion |
| --- | --- |
| `build.clean` / build | `clean_workspace` is boolean true, `outputs` is exactly the full artifact hash map; environment is `build`. Transcript records a fresh workspace, build output and hashes. A repeat incremental build does not qualify. |
| `boot.userspace` / boot | `machine` equals `aarch64`, `kernel_release` and `pid1` are nonempty strings, `root_mounted` is boolean true; environment is `hardware`. Transcript records `uname`, PID 1 and mounts from the tested device. This proves neither Android absence nor ongoing server health by itself; the reviewer assesses native-userspace provenance. |
| `storage.roundtrip` / storage | `filesystem` is nonempty, `writable` is boolean true, `readback_sha256` equals SHA-256 of the bytes `ServerDroid storage probe v1` followed by one LF; environment is `hardware`. Owner-authorized test writes only a disposable file on the intended filesystem, reads it back and removes it. Transcript records commands, target filesystem and cleanup. No partition writes. |
| `network.ssh` / network | `protocol` equals `ssh`, integer `attempted >= 3`, integer `succeeded == attempted`; environment is `hardware`. Transcript records fresh successful SSH command round trips and failures. Remove network/device identifiers before submission. |

Every test must also have exit code zero to substantiate a claim. Zero alone is
insufficient. Failed runs are valid historical data but cannot support a verified
claim. Unknown tests and suites are rejected, not treated as passes. Schema-valid
negative measurements may be recorded without claiming success. Build tests can
support only `build_verified` or `reproduced`; hardware tests support only
`hardware_reported` or `independently_hardware_verified` declarations.

Power and stability currently accept unverified claim states only: no generic
safe thermal, charging or duration threshold has been established by a real
submission. Define device-specific, reviewed criteria before automating them.

## Derived maturity, per variant

| Output | Required supported declarations |
| --- | --- |
| Unassessed | One or more research gates remain unknown |
| Investigated | All three gate outcomes documented as feasible or blocked with source references |
| Buildable | Investigated plus an implemented build claim supported by `build.clean` for that variant |
| Boot verified (hardware-reported) | Buildable plus an implemented boot claim supported by `boot.userspace` for the same variant and build identity |

These are consistency-derived draft labels, not independent endorsements.
Unsupported claims fail validation instead of silently elevating maturity.
Operational requires storage, network, power and stability acceptance criteria
that v1 does not yet define completely. Replicated additionally needs a human
review of independent reproduction; Maintained needs an active maintainer and
regression policy. All three are deferred and never automatically output in v1.
An author cannot declare a maturity field. Partial PRs remain useful and welcome.

## Static screening and local checks

`tools/validate-submission --root . --text` prints the result as three sections that are never merged: what the contributor claims, what the tools demonstrate (consistency, the levels derived per variant, `recipe valid for: [...]; Not tested: [...]`, and every **failed result** that exists for the build, so that a passing run cannot hide a failing one), and what still depends on physical or independent validation. Without `--text` it prints JSON.

The validator requires the project contracts, checks every device profile and
referenced file, verifies source/config/recipe/ordered-patch/log identities, and
checks known-test criteria and variant boundaries. It performs no network I/O.
URL syntax is checked; reachability, upstream license validity, SPDX correctness
and source authenticity need review. Relative references must resolve inside the
same device directory.

It rejects symlinks, hard links, special files, unsafe filenames, path traversal,
unknown file extensions, common binary signatures, NUL/control bytes and non-UTF-8.
No binary exceptions exist in v1. Bounds: 1 MiB/file, 10 MiB/tree, 1,000 files,
2,000 entries, 12 directory levels; the root `.git` directory alone is skipped.
Scripts need an SPDX header in the first five lines; presence is not license
validation. Candidate trees should be immutable snapshots while scanned.

Screening flags some private keys, common token forms, public SSH keys, MACs,
serial labels, personal paths and contiguous 15-digit Luhn-valid identifiers.
It misses other encodings, spaced identifiers, arbitrary credentials, embedded
personal data and intentionally concealed binaries. It can flag harmless numbers.
Do not add broad exclusions to silence a finding: sanitize or explain it during
review. This barrier is **partial, not a guarantee**. The owner must inspect every
file and full diff before export. No raw matching value or parser excerpt is
printed in errors.

## CI and review trust

`validate-device.yml` uses GitHub-hosted Ubuntu only, read-only repository token,
no supplied secrets, no caches or retained artifacts, pinned checkout action,
no persisted Git credentials, submodules or LFS. It uses `pull_request`, never
`pull_request_target`. It does not use private/self-hosted runners or hardware.

For a PR, checkout the base commit into `trusted/` and the proposed head into
`candidate/`. Run the **base validator and base tests** against candidate data;
never run device scripts, recipes or PR-supplied tests. Tools/protocol changes
must receive separate code review and local sandboxed tests; their new behavior
is not approved by existing CI. The workflow definition itself can be changed by
a PR, so the maintainer must inspect workflow diffs and require the trusted check;
this YAML alone cannot enforce branch protection or stop malicious PR workflows.
Set Actions to read-only defaults, require approval for untrusted contributions,
avoid organization secrets available to arbitrary workflows, and grant no OIDC
or write permission. Those account settings are not changed by this draft.

**Observed in real CI runs (2026-10-28, on a private test repository with a base-branch validator).** A pull request that only changed
`tools/` or the known tests was judged by the base branch's rules and was rejected where it should be. A pull request that also edited the workflow to
use its own validator passed through the weakened rules and the review flag disappeared: the limit stated above is real, not theoretical. On
GitHub's free plan, branch protection and rulesets are **not available for private repositories** (the API answers "Upgrade to GitHub Pro or make this
repository public"), and "require approval for workflow runs from outside collaborators" only exists for public ones. So the protections that actually
stop this (a required review on `.github/`, `tools/` and `tests/` through CODEOWNERS, and approval of outside workflow runs) can be switched on only when
the repository is public, and they must be switched on **before** an outside pull request can arrive.

**Bootstrap:** the initial base branch has no trusted validator. The PR job fails
closed with a bootstrap message; it does not execute the proposed validator to
approve itself. Review and test this first increment locally. After human review
and merge, later PRs use that trusted base. Push checks on reviewed branches run
self-tests and validation; manual dispatch runs only on `main`. A green push check
on an unreviewed branch is not maintainer approval. Automated rebuilds are deferred:
they would execute untrusted recipes and require a separately reviewed sandbox,
source availability, licensing, resource limits and network policy.

Submission flow: contributor PR -> deterministic check -> advisory independent
agent report with evidence references, risk findings and accept/request-changes/
reject recommendation -> human decision. No automatic agent invocation, posting,
approval, merge or runner registration is included. Independent evidence may be
submitted in a follow-up PR; human review must assess its independence.

## Later reference import

Import a reviewed file snapshot, never experimental history. Check privacy,
licenses and provenance per file, remove private infrastructure assumptions and
document omitted material. Historical results remain owner-reported and must be
bound to the artifact actually tested; never relabel a clean rebuilt image as
hardware-tested because an older image booted. Source availability or boot
reproduction can remain Blocked. No device import is authorized by this increment.
