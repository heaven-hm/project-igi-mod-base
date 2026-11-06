#!/usr/bin/env python3
"""Check generated IGI bindings against the imported IGI-RE catalogue."""
import csv
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def rows(path):
    with path.open(encoding="utf-8", newline="") as source:
        return list(csv.reader((line for line in source if not line.startswith("#")), delimiter="\t"))


class GeneratedNativesTest(unittest.TestCase):
    def test_manifest_matches_source_and_implementations(self):
        source = {row[0].lower(): row for row in rows(ROOT / "data/IGI1/neonix-igi-re-signatures.tsv")}
        manifest = rows(ROOT / "data/IGI1/mvp-300.tsv")
        implementation = (ROOT / "src/igi_sdk/natives/mvp_natives.cpp").read_text(encoding="utf-8")
        # Build this index once: repeatedly scanning the generated source for
        # thousands of functions makes the catalogue check unnecessarily slow.
        implemented = set()
        domain = None
        for line in implementation.splitlines():
            namespace = re.fullmatch(r"namespace ([A-Z][A-Z0-9_]*) \{", line)
            if namespace:
                domain = namespace.group(1)
            declaration = re.search(r"\bNATIVE_DECL\s+[^\n(]+?\b([A-Z][A-Z0-9_]*)\s*\(", line)
            if declaration and domain:
                implemented.add((domain, declaration.group(1)))
        self.assertEqual(len(manifest), 300)
        self.assertEqual(len({row[2].lower() for row in manifest}), len(manifest))
        for domain, api, address, name, signature, verified in manifest:
            with self.subTest(native=name):
                original = source[address.lower()]
                self.assertEqual((name, signature), (original[1], original[4]))
                self.assertIn(verified, {"source_only", "unicorn_abi"})
                self.assertNotRegex(name, r"^(?:Mem_|Memory_|crt_)")
                self.assertIn((domain, api), implemented)

    def test_demo_native_set_is_included_and_abi_marked(self):
        manifest = {row[2].lower(): row for row in rows(ROOT / "data/IGI1/mvp-300.tsv")}
        demo = rows(ROOT / "tests/demo-natives.tsv")
        runtime = rows(ROOT / "tests/menu-runtime-natives.tsv")
        self.assertEqual(len(demo), 10)
        self.assertEqual({row[2] for row in runtime},
                         {"Game_Get", "Game_IsPaused", "Task_RunScript", "Task_FindById"})
        for api, domain, name, address, signature in demo + runtime:
            with self.subTest(native=name):
                row = manifest[address.lower()]
                self.assertEqual(row[:5], [domain, api, address, name, signature])
                self.assertEqual(row[5], "unicorn_abi")


if __name__ == "__main__":
    unittest.main()
