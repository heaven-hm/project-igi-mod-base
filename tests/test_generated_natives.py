#!/usr/bin/env python3
"""Compare generated native bindings with the local IGI-RE catalogue."""
import csv
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class GeneratedNativesTest(unittest.TestCase):
    def test_registry_matches_catalogue(self):
        with (ROOT / "data/igi-natives.csv").open(encoding="utf-8", newline="") as source:
            rows = {row["address"].lower(): row for row in csv.DictReader(source)}
        registry = (ROOT / "src/natives/mvp_registry.inc").read_text(encoding="utf-8")
        records = re.findall(
            r'catalogue\.add\(\{\s*0x([0-9a-fA-F]+),\s*"([^"]+)",\s*"IGI-RE",\s*"entry",\s*'
            r'((?:"(?:\\.|[^"])*"\s*)+),\s*NativeVerification',
            registry,
        )
        bindings = [
            (address, name, "".join(re.findall(r'"((?:\\.|[^"])*)"', signature)))
            for address, name, signature in records
        ]
        self.assertGreaterEqual(len(bindings), 250)
        self.assertEqual(len(bindings), len({address.lower() for address, _, _ in bindings}))
        for address, name, signature in bindings:
            with self.subTest(native=name):
                self.assertEqual((rows[address.lower()]["name"], rows[address.lower()]["signature"]), (name, signature))


if __name__ == "__main__":
    unittest.main()
