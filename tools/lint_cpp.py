#!/usr/bin/env python3
"""Run clang-tidy on src/ and demos/ files present in a build database."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOTS = (ROOT / "src", ROOT / "demos")


def is_first_party_source(path: Path) -> bool:
    return path.suffix in {".c", ".cc", ".cpp"} and any(
        path.is_relative_to(source_root) for source_root in SOURCE_ROOTS
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="build-macos-verify", help="CMake build directory")
    arguments = parser.parse_args()

    clang_tidy = os.environ.get("CLANG_TIDY") or shutil.which("clang-tidy")
    if clang_tidy is None:
        print("error: clang-tidy is required; install LLVM clang-tidy first", file=sys.stderr)
        return 2

    build_dir = (ROOT / arguments.build_dir).resolve()
    database = build_dir / "compile_commands.json"
    if not database.is_file():
        print(
            f"error: {database} is missing; configure CMake with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            file=sys.stderr,
        )
        return 2

    entries = json.loads(database.read_text(encoding="utf-8"))
    paths = sorted({Path(entry["file"]).resolve() for entry in entries if is_first_party_source(Path(entry["file"]).resolve())})
    if not paths:
        print("error: no src/ or demos/ translation units found in compilation database", file=sys.stderr)
        return 2

    command = [clang_tidy, "-p", str(build_dir), "-quiet", *(str(path) for path in paths)]
    completed = subprocess.run(command, cwd=ROOT, check=False)
    if completed.returncode:
        return completed.returncode

    print(f"linted {len(paths)} C++ translation unit(s) from {build_dir.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
