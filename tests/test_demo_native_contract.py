#!/usr/bin/env python3
"""Run the demo-native generator as a focused contract test."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
result = subprocess.run(
    [sys.executable, str(ROOT / "tools/generate_demo_native_tests.py")],
    cwd=ROOT,
    text=True,
    capture_output=True,
)
if result.returncode:
    sys.stderr.write(result.stdout + result.stderr)
    raise SystemExit(result.returncode)
print(result.stdout, end="")
