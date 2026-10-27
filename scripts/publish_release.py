#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Packages per-platform oracle outputs and publishes them as a GitHub release, one release per
scenario-set hash (tag "scenarios-<hash>"), using the gh CLI.

Input: a directory holding one juce-oracle output directory per platform (each with manifest.json).
Assets uploaded (existing assets of the same name are replaced):
  juce-oracle-<label>.tar.gz   the whole output directory of one platform
  manifest-<label>.json        that platform's manifest, for downloading without unpacking
  cross-platform.json          which output files differ between platforms (ideally none)
  release.json                 tag, scenario-set hash, JUCE tag/commit, oracle commit, assets
"""
import argparse
import json
import subprocess
import sys
import tarfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import verify_output  # noqa: E402


def load_platforms(artifacts: Path):
    platforms = []
    for manifest_path in sorted(artifacts.glob("*/manifest.json")):
        out = manifest_path.parent
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        problems = verify_output.verify(out, None)
        if problems:
            raise SystemExit(f"{out}: " + "; ".join(problems))
        platforms.append((manifest["platform"]["label"], out, manifest))
    if not platforms:
        raise SystemExit(f"no */manifest.json under {artifacts}")
    labels = [p[0] for p in platforms]
    if len(set(labels)) != len(labels):
        raise SystemExit(f"duplicate platform labels {labels}")
    hashes = {p[2]["scenario_set"]["hash"] for p in platforms}
    if len(hashes) != 1:
        raise SystemExit(f"platforms ran different scenario sets: {sorted(hashes)}")
    return platforms


def compare(platforms):
    """For each output path, the hash on each platform; lists the paths whose hashes differ."""
    by_path = {}
    status = {}
    for label, _, manifest in platforms:
        for entry in manifest["scenarios"]:
            status.setdefault(entry["id"], {})[label] = entry["status"]
            for o in entry["outputs"]:
                by_path.setdefault(o["path"], {})[label] = o["sha256"]
    labels = [p[0] for p in platforms]
    differing = {
        path: hashes for path, hashes in sorted(by_path.items())
        if len(set(hashes.values())) != 1 or set(hashes) != set(labels)
    }
    status_diff = {sid: s for sid, s in sorted(status.items()) if len(set(s.values())) != 1}
    return {
        "platforms": labels,
        "outputs_compared": len(by_path),
        "identical": not differing and not status_diff,
        "differing_outputs": differing,
        "differing_status": status_diff,
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--artifacts", type=Path, required=True, help="directory of per-platform output dirs")
    ap.add_argument("--staging", type=Path, required=True, help="where release assets are written")
    ap.add_argument("--repo", help="owner/name for gh (default: current repository)")
    ap.add_argument("--target", help="commit the release tag points at")
    ap.add_argument("--dry-run", action="store_true", help="write assets and print gh commands, run nothing")
    args = ap.parse_args()

    platforms = load_platforms(args.artifacts)
    first = platforms[0][2]
    set_hash = first["scenario_set"]["hash"]
    tag = f"scenarios-{set_hash}"

    args.staging.mkdir(parents=True, exist_ok=True)
    assets = []
    for label, out, manifest in platforms:
        archive = args.staging / f"juce-oracle-{label}.tar.gz"
        with tarfile.open(archive, "w:gz") as tar:
            tar.add(out, arcname=label)
        manifest_copy = args.staging / f"manifest-{label}.json"
        manifest_copy.write_bytes((out / "manifest.json").read_bytes())
        assets += [archive, manifest_copy]

    cross = compare(platforms)
    (args.staging / "cross-platform.json").write_text(json.dumps(cross, indent=2) + "\n", encoding="utf-8")
    assets.append(args.staging / "cross-platform.json")

    summary = {
        "tag": tag,
        "scenario_set": first["scenario_set"],
        "juce": first["juce"],
        "oracle": first["oracle"],
        "platforms": {label: manifest["summary"] for label, _, manifest in platforms},
        "cross_platform_identical": cross["identical"],
        "assets": sorted(a.name for a in assets) + ["release.json"],
    }
    (args.staging / "release.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    assets.append(args.staging / "release.json")

    notes = (
        f"Reference outputs from JUCE {first['juce']['tag']} ({first['juce']['commit']}) for scenario set "
        f"`{set_hash}` ({first['scenario_set']['count']} scenarios), oracle commit "
        f"`{first['oracle'].get('commit') or 'unknown'}`.\n\n"
        + "\n".join(f"- {label}: {json.dumps(m['summary'], sort_keys=True)}" for label, _, m in platforms)
        + f"\n\nCross-platform outputs identical: {cross['identical']}.\n"
    )
    notes_file = args.staging / "notes.md"
    notes_file.write_text(notes, encoding="utf-8")

    repo = ["--repo", args.repo] if args.repo else []
    view = ["gh", "release", "view", tag, *repo]
    create = ["gh", "release", "create", tag, *repo, "--title", f"Oracle outputs for scenario set {set_hash[:12]}",
              "--notes-file", str(notes_file), *(["--target", args.target] if args.target else [])]
    edit = ["gh", "release", "edit", tag, *repo, "--notes-file", str(notes_file)]
    upload = ["gh", "release", "upload", tag, *repo, "--clobber", *[str(a) for a in assets]]

    print(f"tag: {tag}")
    if args.dry_run:
        for cmd in (view, create, edit, upload):
            print("dry-run:", " ".join(cmd))
        return 0

    exists = subprocess.run(view, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    subprocess.run(edit if exists else create, check=True)
    subprocess.run(upload, check=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
