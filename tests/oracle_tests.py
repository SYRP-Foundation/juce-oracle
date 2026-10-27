#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Behaviour tests for juce-oracle, one function per ctest test (named b<N>_...).

usage: oracle_tests.py <test-name> <path-to-juce-oracle> <work-dir> [scenarios-dir]
Exit 0 pass, 1 fail, 77 skipped."""
import hashlib
import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIXTURES = ROOT / "tests" / "fixtures"
sys.path.insert(0, str(ROOT / "scripts"))
import scenario_set_hash  # noqa: E402
import verify_output  # noqa: E402

SKIP = 77


class Failure(Exception):
    pass


def check(condition, message):
    if not condition:
        raise Failure(message)


class Ctx:
    def __init__(self, exe: Path, work: Path, scenarios: Path | None):
        self.exe, self.work, self.scenarios = exe, work, scenarios

    def run(self, *args, expect_code=0):
        p = subprocess.run([str(self.exe), *map(str, args)], capture_output=True, text=True)
        check(p.returncode == expect_code,
              f"{args}: exit {p.returncode}, expected {expect_code}\nstdout:{p.stdout}\nstderr:{p.stderr}")
        return p

    def fresh(self, name) -> Path:
        d = self.work / name
        shutil.rmtree(d, ignore_errors=True)
        return d

    def run_fixtures(self, name, *extra, expect_code=0) -> tuple[Path, dict]:
        out = self.fresh(name)
        self.run("run-all", FIXTURES / "scenarios", "--out", out, *extra, expect_code=expect_code)
        return out, json.loads((out / "manifest.json").read_text(encoding="utf-8"))


def by_id(manifest):
    return {e["id"]: e for e in manifest["scenarios"]}


# --- B1: the runner builds against JUCE 9.0.4 ---------------------------------------------------

def b1_runner_reports_juce_9_0_4(c: Ctx):
    info = json.loads(c.run("version").stdout)
    check(info["juce"]["version"] == "JUCE v9.0.4", f"linked JUCE reports {info['juce']['version']}")
    check(info["juce"]["commit"] == "3fb5cbc0340758596dac39f7c8de47d562b4896e", "wrong pinned commit")


def b1_lists_supported_kinds(c: Ctx):
    kinds = {k["prefix"]: k for k in json.loads(c.run("kinds").stdout)}
    check("state/value_tree/listener" in kinds, f"kinds: {sorted(kinds)}")
    check(kinds["state/value_tree/listener"]["outputs"] == ["result.json"], "listener kind outputs")


def b1_cli_usage_errors_exit_2(c: Ctx):
    c.run(expect_code=2)
    c.run("bogus", expect_code=2)
    c.run("run-all", FIXTURES / "scenarios", expect_code=2)  # no --out
    c.run("run", c.work / "missing.json", "--out", c.work / "x", expect_code=2)


# --- B2: scenarios in, outputs and a hashed manifest out ----------------------------------------

def b2_value_tree_listener_fixtures_run(c: Ctx):
    out, manifest = c.run_fixtures("b2_listener")
    entries = by_id(manifest)
    ok = entries["state/value_tree/listener/fixture_ancestors_hear_property_changes"]
    check(ok["status"] == "pass", f"fixture should pass: {ok}")
    result = json.loads((out / ok["outputs"][0]["path"]).read_text(encoding="utf-8"))
    heard = {(o["listener"], o["callback"], o["tree"], o["property"]) for o in result["observed"]}
    check(("onTop", "valueTreePropertyChanged", "kid", "gain") in heard, f"observed {result['observed']}")
    check(result["check"]["status"] == "pass", "check block")

    bad = entries["state/value_tree/listener/fixture_expectation_mismatch"]
    check(bad["status"] == "fail", f"mismatch fixture should be fail: {bad}")
    result = json.loads((out / bad["outputs"][0]["path"]).read_text(encoding="utf-8"))
    check(result["observed"] == [] and result["check"]["unmatched_expected"] == [0], f"{result}")


def b2_selftest_writes_valid_wav(c: Ctx):
    out, manifest = c.run_fixtures("b2_wav")
    entry = by_id(manifest)["oracle/selftest/triangle_stereo"]
    check(entry["status"] == "pass", f"{entry}")
    wav = (out / "scenarios/oracle/selftest/triangle_stereo/signal.wav").read_bytes()
    check(wav[:4] == b"RIFF" and wav[8:12] == b"WAVE", "RIFF/WAVE header")
    check(struct.unpack_from("<I", wav, 4)[0] == len(wav) - 8, "RIFF size")
    fmt, channels, rate, _, align, bits = struct.unpack_from("<HHIIHH", wav, 20)
    check((fmt, channels, rate, align, bits) == (3, 2, 48000, 8, 32), f"fmt {fmt, channels, rate, align, bits}")
    data_at = wav.index(b"data")
    size = struct.unpack_from("<I", wav, data_at + 4)[0]
    samples = struct.unpack_from(f"<{size // 4}f", wav, data_at + 8)
    check(len(samples) == 128, f"{len(samples)} samples")
    left = samples[0::2]
    check(left[:5] == (-1.0, -0.75, -0.5, -0.25, 0.0), f"left starts {left[:5]}")
    check(max(samples) == 1.0 and min(samples) == -1.0, "triangle range")


def b2_manifest_hashes_match_outputs(c: Ctx):
    out, manifest = c.run_fixtures("b2_manifest")
    problems = verify_output.verify(out, FIXTURES / "scenarios")
    check(not problems, "; ".join(problems))
    check(manifest["scenario_set"]["count"] == 3, "schema files must not count as scenarios")
    check(manifest["summary"] == {"error": 0, "fail": 1, "pass": 2, "unsupported": 0}, f"{manifest['summary']}")


def b2_scenario_set_hash_matches_reference(c: Ctx):
    reported = json.loads(c.run("hash", FIXTURES / "scenarios").stdout)
    check(reported["hash"] == scenario_set_hash.set_hash(FIXTURES / "scenarios"), "C++ and Python hashes differ")

    # A CRLF checkout (Windows autocrlf) must hash the same as an LF one.
    crlf = c.fresh("b2_crlf")
    shutil.copytree(FIXTURES / "scenarios", crlf)
    for f in crlf.rglob("*.json"):
        f.write_bytes(f.read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))
    check(json.loads(c.run("hash", crlf).stdout)["hash"] == reported["hash"], "CRLF changes the set hash")

    # Any content change does change it.
    victim = next(crlf.rglob("fixture_expectation_mismatch.json"))
    victim.write_bytes(victim.read_bytes() + b" ")
    check(json.loads(c.run("hash", crlf).stdout)["hash"] != reported["hash"], "edit did not change the hash")


def b2_outputs_are_deterministic(c: Ctx):
    a, _ = c.run_fixtures("b2_det_a")
    b, _ = c.run_fixtures("b2_det_b")
    check((a / "SHA256SUMS").read_bytes() == (b / "SHA256SUMS").read_bytes(), "two runs differ")


def b2_unsupported_kind_is_reported(c: Ctx):
    out = c.fresh("b2_unsupported")
    c.run("run-all", FIXTURES / "unsupported", "--out", out)
    entry = json.loads((out / "manifest.json").read_text())["scenarios"][0]
    check(entry["status"] == "unsupported" and entry["outputs"] == [], f"{entry}")
    c.run("run-all", FIXTURES / "unsupported", "--out", out, "--replace", "--strict", expect_code=4)


def b2_malformed_scenario_is_an_error(c: Ctx):
    src = c.fresh("b2_malformed_src")
    (src / "state/value_tree/listener").mkdir(parents=True)
    (src / "broken.json").write_text("{ not json")
    (src / "state/value_tree/listener/bad_op.json").write_text(json.dumps({
        "id": "state/value_tree/listener/bad_op", "setup": [{"op": "explode"}], "steps": [],
        "expect": {"compare": "set", "calls": []}}))
    (src / "escape.json").write_text(json.dumps({"id": "../../escape"}))
    out = c.fresh("b2_malformed")
    c.run("run-all", src, "--out", out, expect_code=1)
    manifest = json.loads((out / "manifest.json").read_text())
    check([e["status"] for e in manifest["scenarios"]] == ["error"] * 3, f"{manifest['scenarios']}")
    check(not (c.work / "escape").exists(), "unsafe id escaped the output directory")


def b2_out_of_range_integers_are_errors(c: Ctx):
    src = c.fresh("b2_range_src")
    d = src / "state/value_tree/listener"
    d.mkdir(parents=True)
    wrap = 4294967297  # wraps to 1 if truncated to 32 bits, hiding a real change

    def scenario(name, setup, steps):
        (d / f"{name}.json").write_text(json.dumps({
            "id": f"state/value_tree/listener/{name}", "setup": setup, "steps": steps,
            "expect": {"compare": "set", "calls": []}}))

    base = [{"op": "create", "tree": "a", "type": "A"},
            {"op": "set_property", "tree": "a", "name": "x", "value": 1},
            {"op": "add_listener", "tree": "a", "listener": "l"}]
    scenario("big_value", base, [{"op": "set_property", "tree": "a", "name": "x", "value": wrap}])
    scenario("big_index", [{"op": "create", "tree": "a", "type": "A"}, {"op": "create", "tree": "b", "type": "B"}],
             [{"op": "add_child", "parent": "a", "child": "b", "index": wrap}])
    scenario("edge_ok", base, [{"op": "set_property", "tree": "a", "name": "x", "value": 2147483647},
                               {"op": "set_property", "tree": "a", "name": "x", "value": -2147483648}])
    out = c.fresh("b2_range")
    c.run("run-all", src, "--out", out, expect_code=1)
    entries = by_id(json.loads((out / "manifest.json").read_text()))
    check(entries["state/value_tree/listener/big_value"]["status"] == "error", f"{entries}")
    check(entries["state/value_tree/listener/big_index"]["status"] == "error", f"{entries}")
    check(entries["state/value_tree/listener/edge_ok"]["status"] in ("pass", "fail"), f"{entries}")


def b2_refuses_non_empty_output_dir(c: Ctx):
    out = c.fresh("b2_nonempty")
    out.mkdir()
    (out / "keep.txt").write_text("user data")
    c.run("run-all", FIXTURES / "scenarios", "--out", out, expect_code=3)
    c.run("run-all", FIXTURES / "scenarios", "--out", out, "--replace", expect_code=3)
    check((out / "keep.txt").exists(), "deleted a directory that was not an oracle output")


def b2_syrp_scenarios_run(c: Ctx):
    if c.scenarios is None:
        print("no SYRP scenarios directory configured (JUCE_ORACLE_SCENARIOS_DIR); skipping")
        sys.exit(SKIP)
    out = c.fresh("b2_syrp")
    c.run("run-all", c.scenarios, "--out", out)
    manifest = json.loads((out / "manifest.json").read_text())
    # New scenario families land in SYRP before the oracle has a kind for them: "unsupported" is
    # allowed (and listed so the gap stays visible); "error" is still a failure.
    bad = [e for e in manifest["scenarios"] if e["status"] not in ("pass", "fail", "unsupported")]
    check(not bad, f"scenarios that did not run: {bad}")
    unsupported = sorted(e["id"] for e in manifest["scenarios"] if e["status"] == "unsupported")
    print(f"{len(unsupported)} unsupported scenario(s):")
    for sid in unsupported:
        print(f"  unsupported: {sid}")
    problems = verify_output.verify(out, c.scenarios)
    check(not problems, "; ".join(problems))
    for e in manifest["scenarios"]:
        print(f"{e['status']:5} {e['id']}")


# --- B3: publishing ------------------------------------------------------------------------------

def b3_workflow_lint(c: Ctx):
    try:
        import yaml  # noqa: F401
    except ImportError:
        print("PyYAML not installed; skipping")
        sys.exit(SKIP)
    p = subprocess.run([sys.executable, str(ROOT / "scripts" / "lint_workflow.py")], capture_output=True, text=True)
    check(p.returncode == 0, p.stdout + p.stderr)


def b3_release_dry_run(c: Ctx):
    artifacts = c.fresh("b3_artifacts")
    for label in ("linux-x64", "macos-arm64"):
        c.run("run-all", FIXTURES / "scenarios", "--out", artifacts / label, "--label", label,
              "--oracle-commit", "0" * 40)
    staging = c.fresh("b3_staging")
    p = subprocess.run([sys.executable, str(ROOT / "scripts" / "publish_release.py"), "--artifacts", str(artifacts),
                        "--staging", str(staging), "--dry-run"], capture_output=True, text=True)
    check(p.returncode == 0, p.stdout + p.stderr)
    set_hash = scenario_set_hash.set_hash(FIXTURES / "scenarios")
    check(f"tag: scenarios-{set_hash}" in p.stdout, p.stdout)
    check("gh release upload" in p.stdout and "--clobber" in p.stdout, p.stdout)
    release = json.loads((staging / "release.json").read_text())
    check(release["assets"] == sorted(["cross-platform.json", "juce-oracle-linux-x64.tar.gz",
                                       "juce-oracle-macos-arm64.tar.gz", "manifest-linux-x64.json",
                                       "manifest-macos-arm64.json", "release.json"]), f"{release['assets']}")
    cross = json.loads((staging / "cross-platform.json").read_text())
    check(cross["identical"] and cross["outputs_compared"] == 4, f"{cross}")

    # An existing release: identical manifests skip the upload; differing ones fail unless --replace.
    existing = c.fresh("b3_existing")
    existing.mkdir()
    for label in ("linux-x64", "macos-arm64"):
        shutil.copy(artifacts / label / "manifest.json", existing / f"manifest-{label}.json")
    base = [sys.executable, str(ROOT / "scripts" / "publish_release.py"), "--artifacts", str(artifacts),
            "--staging", str(staging), "--dry-run", "--existing", str(existing)]
    p = subprocess.run(base, capture_output=True, text=True)
    check(p.returncode == 0 and "nothing to upload" in p.stdout and "dry-run: gh release upload" not in p.stdout,
          p.stdout + p.stderr)
    changed = json.loads((existing / "manifest-linux-x64.json").read_text())
    victim = next(e for e in changed["scenarios"] if e["outputs"])
    victim["outputs"][0]["sha256"] = "f" * 64
    (existing / "manifest-linux-x64.json").write_text(json.dumps(changed))
    p = subprocess.run(base, capture_output=True, text=True)
    check(p.returncode == 1 and "refusing to overwrite" in p.stderr and "differs: linux-x64" in p.stderr,
          p.stdout + p.stderr)
    p = subprocess.run(base + ["--replace"], capture_output=True, text=True)
    check(p.returncode == 0 and "gh release upload" in p.stdout and "--clobber" in p.stdout, p.stdout + p.stderr)

    # Different scenario sets must not be published under one tag.
    shutil.rmtree(artifacts / "macos-arm64")
    c.run("run-all", FIXTURES / "unsupported", "--out", artifacts / "macos-arm64", "--label", "macos-arm64")
    p = subprocess.run([sys.executable, str(ROOT / "scripts" / "publish_release.py"), "--artifacts", str(artifacts),
                        "--staging", str(staging), "--dry-run"], capture_output=True, text=True)
    check(p.returncode != 0 and "different scenario sets" in p.stderr, p.stdout + p.stderr)


def main() -> int:
    if len(sys.argv) < 4:
        print(__doc__, file=sys.stderr)
        return 2
    name, exe, work = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3])
    scenarios = Path(sys.argv[4]) if len(sys.argv) > 4 and sys.argv[4] else None
    test = globals().get(name)
    if not name.startswith("b") or test is None:
        print(f"unknown test {name}", file=sys.stderr)
        return 2
    work.mkdir(parents=True, exist_ok=True)
    try:
        test(Ctx(exe, work, scenarios))
    except Failure as f:
        print(f"FAIL {name}: {f}", file=sys.stderr)
        return 1
    print(f"PASS {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
