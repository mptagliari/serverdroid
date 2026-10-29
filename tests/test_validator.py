# SPDX-License-Identifier: GPL-3.0-only
# SPDX-FileCopyrightText: 2026 Matheus Tagliari
"""Synthetic data only. No fixture is evidence of a working device."""

import hashlib
import json
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import tempfile
import tomllib
import unittest

REPO = Path(__file__).resolve().parents[1]
V = runpy.run_path(str(REPO / "tools/validate-submission"))
Invalid = V["Invalid"]
STAMP = "2026-10-28T00:00:00Z"
LATER = "2026-10-28T00:00:01Z"


def sha(data):
    return hashlib.sha256(data).hexdigest()


class ValidatorTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        for name in ("README.md", "AGENTS.md", "LICENSE", "LICENSING.md", "NOTICE.md"):
            shutil.copy(REPO / name, self.root / name)
        shutil.copytree(REPO / "docs", self.root / "docs")
        shutil.copytree(REPO / "devices/TEMPLATE", self.root / "devices/TEMPLATE")
        self.dev = self.root / "devices/synthetic"
        shutil.copytree(REPO / "devices/TEMPLATE", self.dev)
        self.profile = self.dev / "device.toml"
        content = self.profile.read_text().replace('codename = "template"', 'codename = "synthetic"')
        content = content.replace('["unidentified"]', '["variant-a", "variant-b"]')
        content = content.replace('outcome = "unknown"', 'outcome = "blocked"')
        self.profile.write_text(content)

    def validate(self):
        return V["validate"](self.root)

    def reject(self, message=None):
        with self.assertRaises(Invalid) as raised:
            self.validate()
        if message:
            self.assertIn(message, str(raised.exception))

    def replace(self, old, new):
        self.profile.write_text(self.profile.read_text().replace(old, new))

    def add_build(self):
        recipe = b"#!/bin/sh\n# SPDX-License-Identifier: GPL-3.0-only\nexit 0\n"
        (self.dev / "build.sh").write_bytes(recipe)
        config = b"CONFIG_SYNTHETIC=y\n"
        (self.dev / "kernel.config").write_bytes(config)
        content = self.profile.read_text().split("[[claims]]")[0]
        content = content.replace('[research.bootloader]',
                                  'revision = "' + "1" * 40 + '"\nsha256 = "' + "2" * 64
                                  + '"\nlicense = "GPL-2.0-only"\n\n[research.bootloader]', 1)
        content += f'''
[build]
revision = "{'3' * 40}"
variants = ["variant-a", "variant-b"]
sources = ["research-reference"]
patches = []
inputs = []

[build.recipe]
path = "build.sh"
sha256 = "{sha(recipe)}"

[build.toolchain]
name = "synthetic-compiler"
version = "1.0"

[build.config]
path = "kernel.config"
sha256 = "{sha(config)}"

[[build.artifacts]]
id = "boot"
name = "boot-image"
sha256 = "{'4' * 64}"
size = 4096

[[build.artifacts]]
id = "rootfs"
name = "root-filesystem"
sha256 = "{'5' * 64}"
size = 8192
'''
        self.profile.write_text(content)

    def add_run(self, rid="owner", tid="build.clean", variant="variant-a", measurements=None,
                kind=None, observer="owner"):
        p = tomllib.loads(self.profile.read_text())
        artifacts = {a["id"]: a["sha256"] for a in p["build"]["artifacts"]}
        defaults = {
            "build.clean": {"clean_workspace": True, "outputs": artifacts},
            "boot.userspace": {"machine": "aarch64", "kernel_release": "synthetic",
                               "pid1": "init", "root_mounted": True},
            "storage.roundtrip": {"filesystem": "ext4", "writable": True,
                                  "readback_sha256": V["PROBE_SHA"]},
            "network.ssh": {"protocol": "ssh", "attempted": 3, "succeeded": 3},
        }
        log = {"test_id": tid, "measurements": measurements or defaults[tid],
               "transcript": "Synthetic fixture transcript; no real execution."}
        directory = self.dev / "evidence"
        directory.mkdir(exist_ok=True)
        log_path = f"evidence/log-{rid}.json"
        raw = json.dumps(log).encode()
        (self.dev / log_path).write_bytes(raw)
        bid = V["build_id"]({"build": p["build"], "sources": p["sources"]})
        run = {
            "schema_version": 1, "id": rid,
            "subject": {"variant": variant, "recipe_revision": p["build"]["revision"],
                        "build_id": bid, "artifacts": artifacts},
            "suite": {"id": "serverdroid-minimum", "version": "1"},
            "environment": {"kind": kind or ("build" if tid == "build.clean" else "hardware"),
                            "description": "Synthetic test; no device."},
            "observer": {"handle": "synthetic", "relationship": observer},
            "started_at": STAMP, "ended_at": LATER,
            "results": [{"test_id": tid, "command": "recorded only; never execute",
                         "exit_code": 0, "started_at": STAMP, "ended_at": LATER,
                         "log": {"path": log_path, "sha256": sha(raw)}}],
            "limitations": ["Synthetic, not physical evidence."], "redactions": [],
        }
        self.write_run(rid, run)
        return run

    def write_run(self, rid, value):
        (self.dev / f"evidence/run-{rid}.json").write_text(json.dumps(value))

    def claim(self, cap="build", rid="owner", tid="build.clean", verification="build_verified"):
        self.profile.write_text(self.profile.read_text() + f'''
[[claims]]
capability = "{cap}"
state = "implemented"
verification = "{verification}"
evidence = [{{run = "{rid}", test = "{tid}"}}]
''')

    def build_claim(self):
        self.add_build()
        self.add_run()
        self.claim()

    def levels(self):
        return self.validate()["devices"][0]["maturity_by_variant"]

    def test_template_excluded(self):
        result = self.validate()
        self.assertEqual(len(result["devices"]), 1)
        self.assertEqual(result["template"], "structurally valid; excluded from maturity")

    def test_blocked_research_is_investigated(self):
        self.assertEqual(self.levels()["variant-a"], "Investigated")

    def test_unknown_gate_prevents_promotion(self):
        self.replace('outcome = "blocked"', 'outcome = "unknown"')
        self.assertEqual(self.levels()["variant-a"], "Unassessed")

    def test_buildable_is_variant_specific(self):
        self.build_claim()
        self.assertEqual(self.levels(), {"variant-a": "Buildable", "variant-b": "Investigated"})

    def test_boot_requires_build_and_known_hardware_test(self):
        self.build_claim()
        self.add_run("boot", "boot.userspace")
        self.claim("boot", "boot", "boot.userspace", "hardware_reported")
        self.assertEqual(self.levels()["variant-a"], "Boot verified (hardware-reported)")

    def test_boot_without_build_claim_does_not_promote(self):
        self.add_build()
        self.add_run("boot", "boot.userspace")
        self.claim("boot", "boot", "boot.userspace", "hardware_reported")
        self.assertEqual(self.levels()["variant-a"], "Investigated")

    def test_storage_and_network_have_known_criteria(self):
        self.build_claim()
        for rid, tid, cap in (("storage", "storage.roundtrip", "storage"),
                              ("network", "network.ssh", "network")):
            self.add_run(rid, tid)
            self.claim(cap, rid, tid, "hardware_reported")
        self.assertEqual(self.levels()["variant-a"], "Buildable")

    def test_exit_zero_is_not_enough(self):
        self.add_build()
        self.add_run(measurements={"clean_workspace": False, "outputs": {}})
        self.claim()
        self.reject("criteria")

    def test_wrong_machine_does_not_prove_boot(self):
        self.build_claim()
        self.add_run("boot", "boot.userspace", measurements={"machine": "x86_64",
                     "kernel_release": "synthetic", "pid1": "init", "root_mounted": True})
        self.claim("boot", "boot", "boot.userspace", "hardware_reported")
        self.reject("criteria")

    def test_failed_result_can_be_retained_without_claim(self):
        self.add_build()
        run = self.add_run()
        run["results"][0]["exit_code"] = 1
        self.write_run("owner", run)
        self.profile.write_text("claims = []\n" + self.profile.read_text())
        self.assertEqual(self.levels()["variant-a"], "Investigated")

    def test_failed_claim_rejected(self):
        self.build_claim()
        run = json.loads((self.dev / "evidence/run-owner.json").read_text())
        run["results"][0]["exit_code"] = 1
        self.write_run("owner", run)
        self.reject("criteria")

    def test_wrong_capability_test(self):
        self.add_build()
        self.add_run()
        self.claim("boot")
        self.reject("capability")

    def test_unknown_test(self):
        self.build_claim()
        run = json.loads((self.dev / "evidence/run-owner.json").read_text())
        run["results"][0]["test_id"] = "custom.success"
        self.write_run("owner", run)
        self.reject("unknown test")

    def test_suite_change_rejected(self):
        self.build_claim()
        run = json.loads((self.dev / "evidence/run-owner.json").read_text())
        run["suite"]["version"] = "2"
        self.write_run("owner", run)
        self.reject("suite")

    def test_stale_artifact_rejected(self):
        self.build_claim()
        self.replace("4" * 64, "6" * 64)
        self.reject("build identity")

    def test_missing_component_rejected(self):
        self.build_claim()
        run = json.loads((self.dev / "evidence/run-owner.json").read_text())
        del run["subject"]["artifacts"]["rootfs"]
        self.write_run("owner", run)
        self.reject("artifact set")

    def test_source_provenance_change_invalidates_run(self):
        self.build_claim()
        self.replace("2" * 64, "6" * 64)
        self.reject("build identity")

    def test_config_tampering(self):
        self.build_claim()
        (self.dev / "kernel.config").write_text("changed\n")
        self.reject("hash mismatch")

    def test_log_tampering(self):
        self.build_claim()
        (self.dev / "evidence/log-owner.json").write_text("{}")
        self.reject("hash mismatch")

    def test_storage_wrong_readback_rejected(self):
        self.add_build()
        self.add_run("storage", "storage.roundtrip", measurements={"filesystem": "ext4",
                     "writable": True, "readback_sha256": "0" * 64})
        self.claim("storage", "storage", "storage.roundtrip", "hardware_reported")
        self.reject("criteria")

    def test_network_partial_success_rejected(self):
        self.add_build()
        self.add_run("network", "network.ssh", measurements={"protocol": "ssh",
                     "attempted": 3, "succeeded": 2})
        self.claim("network", "network", "network.ssh", "hardware_reported")
        self.reject("criteria")

    def test_build_in_hardware_run_does_not_promote(self):
        self.add_build()
        self.add_run(kind="hardware")
        self.claim()
        self.reject("criteria")

    def test_patch_order_changes_build_identity(self):
        self.build_claim()
        for name in ("first", "second"):
            (self.dev / f"{name}.patch").write_text(name)
        patch_records = ', '.join('{path = "' + name + '.patch", sha256 = "'
                                  + sha(name.encode()) + '"}' for name in ("first", "second"))
        self.replace('patches = []', 'patches = [' + patch_records + ']')
        # The old evidence must no longer support this amended recipe.
        self.reject("build identity")

    def test_patch_order_is_not_sorted_away(self):
        a = {"patches": [{"path": "first"}, {"path": "second"}]}
        b = {"patches": list(reversed(a["patches"]))}
        self.assertNotEqual(V["build_id"](a), V["build_id"](b))

    def test_duplicate_result_rejected(self):
        self.build_claim()
        run = json.loads((self.dev / "evidence/run-owner.json").read_text())
        run["results"].append(run["results"][0])
        self.write_run("owner", run)
        self.reject("duplicate test")

    def test_empty_recovery_rejected(self):
        (self.dev / "RECOVERY.md").write_text("")
        self.reject("nonempty")

    def test_broken_local_link(self):
        (self.dev / "REPORT.md").write_text("See [missing](not-present.md).")
        self.reject("Markdown target")

    def test_link_escaping_root(self):
        (self.root / "README.md").write_text("See [escape](../outside.md).")
        self.reject("escapes tree")

    def test_recipe_revision_mismatch(self):
        self.build_claim()
        run = json.loads((self.dev / "evidence/run-owner.json").read_text())
        run["subject"]["recipe_revision"] = "7" * 40
        self.write_run("owner", run)
        self.reject("recipe revision")

    def test_unlisted_variant(self):
        self.build_claim()
        run = json.loads((self.dev / "evidence/run-owner.json").read_text())
        run["subject"]["variant"] = "variant-c"
        self.write_run("owner", run)
        self.reject("variant")

    def test_independence_is_declared_not_authenticated(self):
        self.add_build()
        self.add_run(observer="independent")
        self.claim(verification="reproduced")
        result = self.validate()["devices"][0]
        self.assertEqual(result["maturity_by_variant"]["variant-a"], "Buildable")
        self.assertIn("contributor-declared", " ".join(result["limits"]))

    def test_independent_label_requires_matching_declaration(self):
        self.add_build()
        self.add_run()
        self.claim(verification="reproduced")
        self.reject("independent observer")

    def test_time_reversal(self):
        self.build_claim()
        run = json.loads((self.dev / "evidence/run-owner.json").read_text())
        run["started_at"], run["ended_at"] = LATER, STAMP
        self.write_run("owner", run)
        self.reject("time order")

    def test_naive_timestamp(self):
        self.build_claim()
        run = json.loads((self.dev / "evidence/run-owner.json").read_text())
        run["started_at"] = STAMP[:-1]
        self.write_run("owner", run)
        self.reject("timestamp")

    def test_missing_claim_evidence(self):
        self.replace('verification = "none"', 'verification = "hardware_reported"')
        self.replace('state = "not_tested"', 'state = "implemented"')
        self.reject("evidence")

    def test_unknown_profile_field(self):
        self.profile.write_text('maturity = "Operational"\n' + self.profile.read_text())
        self.reject("unknown fields")

    def test_partition_overlap(self):
        self.replace("proposed_writes = []", 'proposed_writes = ["boot"]')
        self.replace("protected_partitions = []", 'protected_partitions = ["boot"]')
        self.reject("overlap")

    def test_partition_assertion_needs_source(self):
        self.replace("proposed_writes = []", 'proposed_writes = ["boot"]')
        self.reject("sources")

    def test_path_traversal(self):
        self.replace('recovery = "RECOVERY.md"', 'recovery = "../../README.md"')
        self.reject("unsafe reference")

    def test_symlink(self):
        (self.dev / "escape.md").symlink_to(self.root / "README.md")
        self.reject("symlink")

    def test_binary_disguised_as_text(self):
        (self.dev / "log.txt").write_bytes(b"\x7fELF\0payload")
        self.reject("binary")

    def test_blob_extension(self):
        (self.dev / "firmware.bin").write_text("text camouflage")
        self.reject("file type")

    def test_oversized_file(self):
        (self.dev / "large.txt").write_text("x" * (V["MAX_FILE"] + 1))
        self.reject("size limit")

    def test_imei_screening(self):
        prefix = "49" + "0154" + "20323751"
        candidate = next(prefix + str(i) for i in range(10) if V["luhn"](prefix + str(i)))
        (self.dev / "leak.txt").write_text(candidate)
        self.reject("IMEI")

    def test_mac_screening(self):
        (self.dev / "leak.txt").write_text(":".join(["aa", "bb", "cc", "dd", "ee", "ff"]))
        self.reject("private identifier")

    def test_token_screening(self):
        (self.dev / "leak.txt").write_text("gh" + "p_" + "a" * 36)
        self.reject("credential")

    def test_missing_spdx(self):
        (self.dev / "script.sh").write_text("#!/bin/sh\nexit 0\n")
        self.reject("SPDX")

    def test_c_source_with_spdx_accepted(self):
        (self.dev / "probe.c").write_text(
            "// SPDX-License-Identifier: GPL-3.0-only\nint main(void) { return 0; }\n")
        (self.dev / "probe.h").write_text(
            "// SPDX-License-Identifier: GPL-3.0-only\n#ifndef PROBE_H\n#define PROBE_H\n#endif\n")
        self.validate()  # does not raise

    def test_c_source_missing_spdx(self):
        (self.dev / "probe.c").write_text("int main(void) { return 0; }\n")
        self.reject("SPDX")

    def test_h_header_missing_spdx(self):
        (self.dev / "probe.h").write_text("#ifndef PROBE_H\n#define PROBE_H\n#endif\n")
        self.reject("SPDX")

    def test_duplicate_json_key(self):
        self.build_claim()
        path = self.dev / "evidence/run-owner.json"
        path.write_text(path.read_text().replace('"schema_version": 1',
                                                '"schema_version": 1, "schema_version": 1'))
        self.reject("duplicate JSON")

    def test_boolean_not_integer(self):
        self.build_claim()
        run = json.loads((self.dev / "evidence/run-owner.json").read_text())
        run["results"][0]["exit_code"] = False
        self.write_run("owner", run)
        self.reject("integer")

    def test_recipe_and_recorded_command_never_run(self):
        self.build_claim()
        marker = self.root / "executed.txt"
        recipe_marker = self.root / "recipe-executed.txt"
        recipe = f"#!/bin/sh\n# SPDX-License-Identifier: GPL-3.0-only\ntouch {recipe_marker}\n"
        old_digest = sha((self.dev / "build.sh").read_bytes())
        (self.dev / "build.sh").write_text(recipe)
        self.replace(old_digest, sha(recipe.encode()))
        # Update the run binding to this recipe, without executing it.
        self.add_run()
        run = json.loads((self.dev / "evidence/run-owner.json").read_text())
        run["results"][0]["command"] = f"touch {marker}"
        self.write_run("owner", run)
        self.validate()
        self.assertFalse(marker.exists())
        self.assertFalse(recipe_marker.exists())

    def test_cli_malformed_input_no_traceback_or_content_leak(self):
        self.profile.write_text('private_value = "unterminated')
        result = subprocess.run([sys.executable, "-I", str(REPO / "tools/validate-submission"),
                                 "--root", str(self.root)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertFalse(json.loads(result.stdout)["valid"])
        self.assertNotIn("unterminated", result.stdout)
        self.assertNotIn("Traceback", result.stderr)

    # Merged increment: the three-section report, failed results that stay visible, and CODEOWNERS.
    def cli(self, *args):
        return subprocess.run([sys.executable, "-I", "-B", str(REPO / "tools/validate-submission"),
                               "--root", str(self.root), *args], capture_output=True, text=True)

    def test_text_report_has_three_separate_sections(self):
        self.build_claim()
        done = self.cli("--text")
        self.assertEqual(done.returncode, 0, done.stdout)
        positions = [done.stdout.index(h) for h in ("== (a)", "== (b)", "== (c)")]
        self.assertEqual(positions, sorted(positions))
        for needle in ("variant-a: Buildable", "partial barrier", "not attestation", "never derived here"):
            self.assertIn(needle, done.stdout)
        self.assertNotIn("clean", done.stdout.lower().replace("clean_workspace", "").replace("clean build", ""))

    def test_text_report_of_a_rejected_submission_has_no_content_and_still_three_sections(self):
        (self.dev / "notes.md").write_text("see /home/" + "alice" + "/x\n")
        done = self.cli("--text")
        self.assertEqual(done.returncode, 1)
        for head in ("== (a)", "== (b)", "== (c)"):
            self.assertIn(head, done.stdout)
        self.assertNotIn("alice", done.stdout)

    def test_json_stays_the_default_output(self):
        done = self.cli()
        self.assertEqual(done.returncode, 0)
        self.assertTrue(json.loads(done.stdout)["valid"])

    def test_recipe_valid_for_lists_only_variants_that_reached_a_level(self):
        self.build_claim()
        device = self.validate()["devices"][0]
        self.assertEqual(device["recipe_valid_for"], ["variant-a"])
        self.assertEqual(device["not_tested"], ["variant-b"])
        self.assertIn("recipe valid for: ['variant-a']; Not tested: ['variant-b']", V["render_text"](self.validate()))

    def test_research_only_recipe_is_valid_for_no_variant(self):
        device = self.validate()["devices"][0]
        self.assertEqual(device["recipe_valid_for"], [])
        self.assertEqual(device["not_tested"], ["variant-a", "variant-b"])

    def test_a_failed_result_is_listed_even_when_a_claim_cites_another_run(self):
        self.build_claim()
        bad = self.add_run("failed", "build.clean", measurements={"clean_workspace": False, "outputs": {}})
        device = self.validate()["devices"][0]
        self.assertEqual(device["maturity_by_variant"]["variant-a"], "Buildable")
        self.assertEqual(device["failed_results"], [{"run": "failed", "test": "build.clean", "variant": "variant-a"}])
        self.assertIn("NOTE failed results exist for this build: failed/build.clean", V["render_text"](self.validate()))

    def test_no_failed_results_means_an_empty_list(self):
        self.build_claim()
        self.assertEqual(self.validate()["devices"][0]["failed_results"], [])

    def test_codeowners_is_an_accepted_file(self):
        (self.root / "CODEOWNERS").write_text("# placeholder\n")
        self.validate()



    # Added after a mutation round (the validator was broken on purpose; these were the changes no test noticed).
    def test_unknown_gate_source_rejected(self):
        self.replace('sources = ["research-reference"]', 'sources = ["no-such-source"]')
        self.reject("unknown gate source")

    def test_result_outside_the_run_interval_rejected(self):
        self.add_build()
        run = self.add_run()
        run["results"][0]["started_at"] = "2026-10-27T23:59:59Z"
        self.write_run("owner", run)
        self.claim()
        self.reject("outside")

    def test_hard_link_rejected(self):
        os.link(self.dev / "REPORT.md", self.dev / "copy.md")
        self.reject("hard link")

    def test_every_binary_signature_is_rejected(self):
        for signature in (b"\x7fELF", b"ANDROID!", b"VNDRBOOT", b"MZ", b"PK\x03\x04", b"\x1f\x8b"):
            with self.subTest(signature=signature):
                blob = self.dev / "blob.txt"
                blob.write_bytes(signature + b" looks like text")
                self.reject("binary signature")
                blob.unlink()

    def test_network_test_needs_three_attempts(self):
        self.build_claim()
        self.add_run("network", "network.ssh", measurements={"protocol": "ssh", "attempted": 2, "succeeded": 2})
        self.claim("network", "network", "network.ssh", "hardware_reported")
        self.reject("criteria")

    def test_run_filename_must_match_its_id(self):
        self.add_build()
        self.add_run()
        (self.dev / "evidence/run-owner.json").rename(self.dev / "evidence/run-other.json")
        self.claim()
        self.reject("filename")


if __name__ == "__main__":
    unittest.main()
