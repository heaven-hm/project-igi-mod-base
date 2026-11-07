#!/usr/bin/env python3
"""Format or verify all first-party C++ sources under src/ and demos/."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOTS = (ROOT / "src", ROOT / "demos")
EXTENSIONS = {".c", ".cc", ".cpp", ".h", ".hh", ".hpp", ".inc"}


def source_files() -> list[Path]:
    return sorted(
        path
        for source_root in SOURCE_ROOTS
        for path in source_root.rglob("*")
        if path.suffix in EXTENSIONS
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if formatting is needed")
    arguments = parser.parse_args()

    clang_format = shutil.which("clang-format")
    if clang_format is None:
        print("error: clang-format is required; install LLVM clang-format first", file=sys.stderr)
        return 2

    paths = source_files()
    if not paths:
        print("No C++ files found under src/ or demos/.")
        return 0

    command = [clang_format]
    if arguments.check:
        command.extend(["--dry-run", "--Werror"])
    else:
        command.append("-i")
    command.extend(str(path) for path in paths)
    completed = subprocess.run(command, cwd=ROOT, check=False)
    if completed.returncode:
        return completed.returncode

    verb = "verified" if arguments.check else "formatted"
    print(f"{verb} {len(paths)} C++ file(s) under src/ and demos/")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
