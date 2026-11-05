#!/usr/bin/env python3
"""Create signature metadata from IGI-RE without copying its implementation.

The generated file contains only address/name/provenance/signature metadata.
It never copies function bodies, comments, or game assets.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path


HOOK = re.compile(r"\bHOOK\s*\(\s*0x([0-9A-Fa-f]+)\s*,\s*([A-Za-z_]\w*)\s*\)")
ORIG = re.compile(r"\bORIG\s*\(\s*0x([0-9A-Fa-f]+)\s*,\s*([^,]+),\s*([A-Za-z_]\w*)\s*,\s*(\([^;]+?\))\s*\)")


def source_files(root: Path) -> list[Path]:
    return sorted(path for suffix in ("*.c", "*.h") for path in root.rglob(suffix))


def extract_definition(text: str, name: str, before: int) -> str | None:
    """Return a normalized declaration for a named C function definition."""
    matches = list(re.finditer(rf"\b{re.escape(name)}\s*\(", text[:before]))
    for match in reversed(matches):
        open_paren = text.find("(", match.start())
        depth = 0
        end = open_paren
        while end < len(text):
            if text[end] == "(":
                depth += 1
            elif text[end] == ")":
                depth -= 1
                if depth == 0:
                    break
            end += 1
        if depth != 0:
            continue
        tail = text[end + 1 :]
        brace = tail.find("{")
        semicolon = tail.find(";")
        if brace < 0 or (semicolon >= 0 and semicolon < brace):
            continue
        start = max(text.rfind(";", 0, match.start()), text.rfind("}", 0, match.start()), text.rfind("\n\n", 0, match.start()) + 1) + 1
        declaration = text[start : end + 1]
        if "*/" in declaration:
            declaration = declaration.rsplit("*/", 1)[1]
        declaration = re.sub(r"/\*.*?\*/", "", declaration, flags=re.DOTALL)
        declaration = re.sub(r"//[^\n]*", "", declaration)
        declaration = re.sub(r"__attribute__\s*\(\(.*\)\)", "", declaration)
        declaration = re.sub(r"\s+", " ", declaration).strip()
        if declaration:
            return declaration
    return None


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--names", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    source_text = {path: path.read_text(encoding="utf-8", errors="replace") for path in source_files(args.source)}
    hooked: dict[str, str] = {}
    definitions: dict[str, str] = {}
    orig_signatures: dict[str, str] = {}
    for text in source_text.values():
        for original in ORIG.finditer(text):
            address, return_type, name, arguments = original.groups()
            orig_signatures.setdefault(f"{int(address, 16):08x}", f"{return_type.strip()} {name}{arguments}")
        for hook in HOOK.finditer(text):
            address, name = hook.groups()
            hooked[f"{int(address, 16):08x}"] = name
            signature = extract_definition(text, name, hook.start())
            if signature:
                definitions[name] = signature

    output = ["# address\tname\tsource\tkind\tsignature"]
    for raw in args.names.read_text(encoding="utf-8").splitlines():
        address, name, provenance, kind = raw.split("\t", 3)
        signature = ""
        if kind == "entry":
            signature = definitions.get(hooked.get(address.lower(), name), orig_signatures.get(address.lower(), ""))
        output.append("\t".join((address, name, provenance, kind, signature)))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(output) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
