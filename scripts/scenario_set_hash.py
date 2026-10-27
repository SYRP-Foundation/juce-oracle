#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Reference implementation of the scenario-set hash, independent of the C++ runner.

Files: every *.json under the root at any depth, except *.schema.json.
File hash: SHA-256 of the file's bytes with every CR LF replaced by LF.
Set hash: SHA-256 of the UTF-8 text made of "<file hash>  <relative path>\\n" for each file,
sorted by relative path ('/'-separated) in byte order.
"""
import hashlib
import sys
from pathlib import Path


def scenario_files(root: Path):
    rows = []
    for path in root.rglob("*.json"):
        if not path.is_file() or path.name.lower().endswith(".schema.json"):
            continue
        rel = path.relative_to(root).as_posix()
        digest = hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()
        rows.append((rel, digest))
    rows.sort(key=lambda r: r[0].encode("utf-8"))
    return rows


def set_hash(root: Path) -> str:
    text = "".join(f"{digest}  {rel}\n" for rel, digest in scenario_files(root))
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: scenario_set_hash.py <scenarios-dir>", file=sys.stderr)
        sys.exit(2)
    print(set_hash(Path(sys.argv[1])))
