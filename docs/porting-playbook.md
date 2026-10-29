# Porting playbook

## 1. Identify and investigate

Record public product identifiers and exact variants, not serials, IMEI, MACs or
per-unit calibration. The three feasibility gates are:

| Gate | Required investigation | Exit |
| --- | --- | --- |
| Bootloader | Lock state, boot/image constraints, documented recovery and owner-visible risks | Evidence-backed feasible path or named blocker |
| Kernel | Vendor sources, configuration, DT, toolchain, existing ports and licensing | Referenced starting point or named blocker |
| Minimum hardware | Storage, network, power and thermal dependencies for server use | Known dependencies, unknowns and blockers |

Use vendor sources, postmarketOS and reference ports before writing code. Shared
SoC does not establish compatible wiring, storage, PMIC, firmware or calibration.
Document all three gates in the profile and report; a blocked conclusion counts as
investigation, never as a successful port. Static checks confirm required fields,
not the truth or completeness of the research.

## 2. Prepare a device-specific recipe

Pin exact source revisions and content digests, toolchain version, config, ordered
patches and each required input. Distinguish upstream source hashes from generated
artifacts. Use non-redistributable inputs locally only; never fingerprint private
calibration. Record licenses and how the owner obtains lawful inputs.

Build a recipe specific to the first device. Do not introduce a universal build
framework or profiles for hypothetical devices. Record the complete artifact set
(for example boot image and root filesystem), not only one component of the system
that was tested. Use immutable recipe revisions, not moving branch names.

## 3. Review a flash plan, without executing it

The contributor agent prepares `RECOVERY.md` and a plan: exact verified variant,
identification commands, partition map provenance, proposed writes, protected
partitions, backup and restore prerequisites, power/cable requirements, and how a
human recovers when networking and display are unavailable. The protected list is
device-specific and must never be copied as a universal denylist.

Public v1 agents never perform writes. The owner reviews and separately authorizes
physical procedures outside this workflow; no CI job is attached to a phone. No
unlock, erase, repartition, charging or radio change is implicit in a submission.
If recovery cannot be justified, stop and report Blocked.

## 4. Build and test, with explicit limits

A clean build can establish Buildable for the listed variant. It does not establish
boot. The owner records the artifact set actually installed and the versioned
tests actually executed. Physical tests and any writes require the owner's
participation and authorization. Log unexpected failures even after recovery.

Use the [evidence protocol](evidence-protocol.md) to bind logs to recipe, variant,
artifact hashes and test version. Do not extrapolate from another variant. Keep
power, thermal, storage persistence, network absence, reboot and unattended
stability limitations visible. A register snapshot is not an initialization
sequence; label deductions accordingly.

## 5. Submit and review

Run the validator locally, inspect privacy and licenses manually, and open a PR.
Hosted CI checks data with a trusted validator; it does not execute recipes.
Reviewer agents inspect source provenance, patches, risks and support for claims,
then recommend accept, request changes or reject, citing evidence and limitations.
The maintainer decides. Independent rebuilds and physical replications require
separate evidence and human assessment; v1 does not grant those identities.

Future increments: a reviewed reference-device import; a read-only second-device
investigation; a collector shaped by real manual runs. Automated rebuilds and
challenge runs are separate later proposals, not prerequisites for a research PR.
