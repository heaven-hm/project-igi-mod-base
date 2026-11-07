#!/usr/bin/env python3
"""Convert the IGI-RE symbol table into HASH namespace constants.

Reads data/igi-natives.csv (address, name, evidence, kind, signature) from the
IGI-RE project and writes include/igi_sdk/natives/igi_re/hashes.hpp:

    namespace HASH {
    inline constexpr NativeHash FRAMES_SET = 0x00402820;
    ...
    }

Every symbol row becomes one constant named exactly as in IGI-RE.
Duplicate names are skipped. Run from the repository root:

    python3 tools/generate_igi_hash.py
"""

import sys
import csv
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TSV = ROOT / "data/igi-natives.csv"
OUT = ROOT / "include/igi_sdk/natives/igi_re/hashes.hpp"


def main() -> int:
    if not TSV.exists():
        print(f"error: missing {TSV}", file=sys.stderr)
        return 1

    constants = []
    seen = set()
    skipped = 0
    with TSV.open(encoding="utf-8") as table:
        for row in csv.reader(table):
            if len(row) < 2:
                continue
            address, name = row[0].strip(), row[1].strip()
            if not address or not name:
                continue
            if not all(c in "0123456789abcdefABCDEF" for c in address):
                continue
            if not name.replace("_", "").isalnum():
                continue
            if name in seen:
                skipped += 1
                continue
            seen.add(name)
            constants.append((name, f"0x{address}"))

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("w", encoding="utf-8") as out:
        out.write("// GENERATED from the IGI-RE symbol table (neonix-igi-re-names.tsv) by\n")
        out.write("// tools/generate_igi_hash.py. Do not edit.\n")
        out.write("#pragma once\n\n")
        out.write("#include <cstdint>\n\n")
        out.write("namespace igi::re {\n\n")
        out.write("using NativeHash = std::uint32_t;\n\n")
        out.write("namespace HASH {\n")
        for name, address in constants:
            out.write(f"inline constexpr NativeHash {name} = {address};\n")
        out.write("} // namespace HASH\n\n")
        out.write("} // namespace igi::re\n")

    print(f"wrote {OUT}: {len(constants)} symbols, {skipped} duplicates skipped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
