#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Static checks for .github/workflows/oracle.yml: valid YAML, only GitHub-owned actions pinned to a
full commit SHA, the three-OS matrix, every scenario run, and a publish job that uses gh."""
import re
import sys
from pathlib import Path

import yaml

WORKFLOW = Path(__file__).resolve().parent.parent / ".github" / "workflows" / "oracle.yml"


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else WORKFLOW
    text = path.read_text(encoding="utf-8")
    wf = yaml.safe_load(text)
    problems = []

    jobs = wf.get("jobs", {})
    for name, job in jobs.items():
        for step in job.get("steps", []):
            uses = step.get("uses")
            if uses is None:
                continue
            if not re.fullmatch(r"actions/[A-Za-z0-9_.-]+@[0-9a-f]{40}", uses):
                problems.append(f"{name}: '{uses}' is not a GitHub-owned action pinned to a commit SHA")

    build = jobs.get("build", {})
    oses = build.get("strategy", {}).get("matrix", {}).get("include", [])
    runners = " ".join(str(o.get("os", "")) for o in oses)
    for family in ("ubuntu", "macos", "windows"):
        if family not in runners:
            problems.append(f"build matrix has no {family} runner")

    build_text = yaml.safe_dump(build)
    if "run-all" not in build_text:
        problems.append("build job does not run every scenario (run-all)")
    if "ctest" not in build_text:
        problems.append("build job does not run ctest")
    if "upload-artifact" not in build_text:
        problems.append("build job does not upload its outputs")

    publish = jobs.get("publish")
    if publish is None:
        problems.append("no publish job")
    else:
        publish_text = yaml.safe_dump(publish)
        if "publish_release.py" not in publish_text:
            problems.append("publish job does not call scripts/publish_release.py")
        if publish.get("permissions", {}).get("contents") != "write":
            problems.append("publish job needs contents: write")
        if "refs/heads/main" not in str(publish.get("if", "")):
            problems.append("publish job must run on main only")

    if wf.get("permissions", {}).get("contents") != "read":
        problems.append("workflow default permissions should be contents: read")

    for p in problems:
        print("lint_workflow:", p, file=sys.stderr)
    if not problems:
        print(f"lint_workflow: {path.name} OK")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
