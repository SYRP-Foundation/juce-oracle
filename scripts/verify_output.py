#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Checks a juce-oracle output directory: manifest shape, every listed hash and size, SHA256SUMS,
and (with --scenarios) the scenario-set hash and per-file hashes against the scenario sources."""
import argparse
import hashlib
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import scenario_set_hash  # noqa: E402

STATUSES = {"pass", "fail", "error", "unsupported"}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(out: Path, scenarios: Path | None) -> list[str]:
    problems = []
    manifest_path = out / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    if manifest.get("format") != "juce-oracle-manifest/1":
        problems.append("unknown manifest format")
    if manifest.get("juce", {}).get("tag") != "9.0.4":
        problems.append("manifest juce.tag is not 9.0.4")

    listed = {}
    for entry in manifest.get("scenarios", []):
        if entry.get("status") not in STATUSES:
            problems.append(f"{entry.get('id')}: bad status {entry.get('status')}")
        for o in entry.get("outputs", []):
            path = out / o["path"]
            if not path.is_file():
                problems.append(f"missing output {o['path']}")
                continue
            if sha256(path) != o["sha256"]:
                problems.append(f"hash mismatch {o['path']}")
            if path.stat().st_size != o["bytes"]:
                problems.append(f"size mismatch {o['path']}")
            listed[o["path"]] = o["sha256"]

    counts = {s: 0 for s in STATUSES}
    for entry in manifest.get("scenarios", []):
        counts[entry.get("status")] = counts.get(entry.get("status"), 0) + 1
    if manifest.get("summary") != counts:
        problems.append(f"summary {manifest.get('summary')} does not match entries {counts}")

    sums = {}
    for line in (out / "SHA256SUMS").read_text(encoding="utf-8").splitlines():
        digest, path = line.split("  ", 1)
        sums[path] = digest
    listed["manifest.json"] = sha256(manifest_path)
    if sums != listed:
        problems.append("SHA256SUMS does not list exactly the manifest's outputs plus manifest.json")
    for path, digest in sums.items():
        if (out / path).is_file() and sha256(out / path) != digest:
            problems.append(f"SHA256SUMS hash mismatch {path}")

    if scenarios is not None:
        expected = scenario_set_hash.set_hash(scenarios)
        if manifest["scenario_set"]["hash"] != expected:
            problems.append(f"scenario_set.hash {manifest['scenario_set']['hash']} != {expected}")
        files = dict(scenario_set_hash.scenario_files(scenarios))
        for entry in manifest["scenarios"]:
            if files.get(entry["file"]) != entry["sha256"]:
                problems.append(f"scenario hash mismatch {entry['file']}")
        if manifest["scenario_set"]["count"] != len(files):
            problems.append("scenario_set.count does not match the scenario files")

    return problems


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("out", type=Path)
    ap.add_argument("--scenarios", type=Path)
    args = ap.parse_args()
    problems = verify(args.out, args.scenarios)
    for p in problems:
        print("verify_output:", p, file=sys.stderr)
    if not problems:
        print(f"verify_output: {args.out} OK")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
