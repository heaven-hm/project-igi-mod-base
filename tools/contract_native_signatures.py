#!/usr/bin/env python3
"""Fail when a typed native wrapper's signature drifts from the registry.

NativeCaller::resolveByAddress compares the wrapper signature string with the
registered IGI-RE signature byte for byte. A mismatch is silently refused at
runtime, which is how the viewport free-camera callback and the crosshair
projection stayed dead while every build and layout test passed.

Runs as part of the Unicorn verification suite (tools/run-unicorn-tests.sh);
no ctest and no host test binaries.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def check() -> int:
    header = (ROOT / "src/natives/mvp_natives.hpp").read_text(encoding="utf-8")
    source = (ROOT / "src/natives/mvp_natives.cpp").read_text(encoding="utf-8")
    registry = (ROOT / "src/natives/mvp_registry.inc").read_text(encoding="utf-8")

    hashes = {
        name: int(address, 16)
        for name, address in re.findall(
            r"inline constexpr std::uint32_t (\w+) = 0x([0-9a-fA-F]+);", header
        )
    }
    registered = {}
    for match in re.finditer(
        r"\{\s*0x([0-9a-fA-F]+),\s*\"([^\"]+)\",\s*\"IGI-RE\",\s*\"entry\",\s*"
        r"((?:\"(?:\\.|[^\"])*\"\s*)+),",
        registry,
        re.S,
    ):
        signature = "".join(re.findall(r"\"((?:\\.|[^\"])*)\"", match.group(3)))
        registered[int(match.group(1), 16)] = (match.group(2), signature)

    problems = []
    checked = 0
    for match in re.finditer(r"HASH::(\w+),\s*((?:\"(?:\\.|[^\"])*\"\s*)+)", source, re.S):
        hash_name = match.group(1)
        signature = "".join(re.findall(r"\"((?:\\.|[^\"])*)\"", match.group(2)))
        address = hashes.get(hash_name)
        if address is None:
            problems.append(f"{hash_name}: no HASH address")
            continue
        record = registered.get(address)
        if record is None:
            problems.append(f"{hash_name} @ 0x{address:08x}: not in the native registry")
            continue
        checked += 1
        if record[1] != signature:
            problems.append(
                f"{hash_name} ({record[0]}): wrapper '{signature}' != registry '{record[1]}'"
            )

    if problems:
        raise SystemExit("native signature drift:\n  " + "\n  ".join(problems))
    if checked < 250:
        raise SystemExit(f"only {checked} native wrappers were checked; the parser is stale")
    print(f"PASS {checked} typed native wrapper signatures match the registry")
    return 0


def run() -> int:
    return check()


if __name__ == "__main__":
    raise SystemExit(run())
