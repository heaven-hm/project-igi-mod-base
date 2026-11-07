#!/usr/bin/env python3
"""Exercise the ten demo native call ABIs with IGI-RE's x86 Unicorn harness.

IGI-RE's Emu loads the retail PE and supplies x86 stack, memory, and execution
semantics. Each native entry is replaced with Emu.fake, so this validates the
SDK's address/signature manifest and the game's cdecl call boundary without
running gameplay code or injecting into a live process. It does not prove a
native's in-game side effects.

Set IGI_EXE to the supported retail x86 ``igi.exe`` and optionally IGI_RE_ROOT
to the IGI-RE checkout before running this test.
"""
import csv
import hashlib
import importlib.util
import os
import platform
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if not (ROOT / "data/igi-natives.csv").is_file():
    print("SKIP: local IGI native catalogue is unavailable")
    raise SystemExit(77)


def discover_re_root() -> Path:
    configured = os.environ.get("IGI_RE_ROOT")
    candidates = [
        Path(configured).expanduser() if configured else ROOT.parent / "IGI-RE",
        ROOT.parent / "IGI-RE",
        Path.home() / "Documents" / "Code" / "IGI-RE",
    ]
    for candidate in candidates:
        if (candidate / "tests" / "emu.py").is_file():
            return candidate
    return candidates[0]


RE_ROOT = discover_re_root()
EXE = os.environ.get("IGI_EXE", "")

if not EXE:
    print("SKIP: set IGI_EXE to the supported retail x86 IGI executable")
    raise SystemExit(77)
if not Path(EXE).is_file():
    raise SystemExit(f"IGI_EXE does not exist: {EXE}")
if sys.platform == "darwin" and platform.machine() == "arm64":
    print("SKIP: run this test with x86_64 Python under Rosetta on arm64 macOS")
    raise SystemExit(77)
try:
    import pefile
except ModuleNotFoundError:
    print("SKIP: install IGI-RE emulator dependencies pefile and unicorn")
    raise SystemExit(77)

emu_path = RE_ROOT / "tests" / "emu.py"
if not emu_path.is_file():
    raise SystemExit(f"IGI-RE emulator not found: {emu_path}; set IGI_RE_ROOT")
spec = importlib.util.spec_from_file_location("igi_re_emu", emu_path)
emu_module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = emu_module
try:
    spec.loader.exec_module(emu_module)
except ModuleNotFoundError as error:
    print(f"SKIP: IGI-RE emulator dependency is unavailable: {error.name}")
    raise SystemExit(77)

pe = pefile.PE(EXE, fast_load=True)
if pe.FILE_HEADER.Machine != 0x014C or pe.OPTIONAL_HEADER.Magic != 0x010B:
    raise SystemExit("IGI_EXE must be a 32-bit x86 PE")
digest = hashlib.sha256(Path(EXE).read_bytes()).hexdigest()
expected_digest = "dea34892d417715873ea61d7b2ba5556655f1ad65385104348146f9679915a01"
if digest != expected_digest:
    raise SystemExit(
        f"IGI_EXE hash mismatch: expected supported IGI-RE profile {expected_digest}, got {digest}"
    )


def read_rows(path):
    with path.open(encoding="utf-8", newline="") as source:
        return [row for row in csv.reader(
            (line for line in source if not line.startswith("#")), delimiter="\t"
        ) if row]


cases = read_rows(ROOT / "tests/demo-natives.tsv")
with (ROOT / "data/igi-natives.csv").open(encoding="utf-8", newline="") as source:
    catalogue = {row["address"].lower(): row for row in csv.DictReader(source)}
if len(cases) != 10:
    raise SystemExit(f"expected exactly 10 demo natives, found {len(cases)}")


def parse_parameters(signature):
    params = signature[signature.index("(") + 1:-1].strip()
    return [] if params == "void" else [part.strip() for part in params.split(",")]


def make_argument(emu, type_name, index):
    if "*" in type_name:
        address = emu.alloc(32)
        if "char" in type_name:
            payload = b"rain\0"
        else:
            payload = bytes((0x40 + index, 0, 0, 0))
        emu.write(address, payload)
        return address, payload
    if "float" in type_name:
        value = 0.25 + index
        return struct.unpack("<I", struct.pack("<f", value))[0], None
    return index + 1, None


# One retail image and one call log for all ten address probes.
emu = emu_module.Emu(exe=EXE, dll="")
for api, domain, name, address, signature in cases:
    address_i = int(address, 16)
    row = catalogue.get(address.lower())
    if row is None or (row["name"], row["signature"]) != (name, signature):
        raise AssertionError(f"{name}: demo contract differs from generated IGI-RE catalogue")

    params = parse_parameters(signature)
    arguments, pointed_payloads = [], []
    for index, param in enumerate(params):
        word, payload = make_argument(emu, param, index)
        arguments.append(word)
        pointed_payloads.append((word, payload))

    prefix = signature[:signature.index("(")].strip()
    function_match = re.search(r"[A-Za-z_]\w*$", prefix)
    if not function_match:
        raise AssertionError(f"unsupported native signature: {signature}")
    result_type = prefix[:function_match.start()].strip()
    sentinel = 0xA5 if result_type == "uint8_t" else 0x13572468
    emu.fake(address_i, ret=sentinel, nargs=len(arguments))
    log_start = len(emu.call_log())
    emu.call(address_i, *arguments)

    call_words = emu.call_log()[log_start:]
    expected_words = [address_i, *arguments]
    if call_words != expected_words:
        raise AssertionError(
            f"{domain}::{api}: emulator recorded {call_words!r}, expected {expected_words!r}"
        )
    for pointer, payload in pointed_payloads:
        if payload is not None and emu.read(pointer, len(payload)) != payload:
            raise AssertionError(f"{domain}::{api}: pointer argument payload was not readable")
    if result_type != "void":
        width_mask = 0xFF if result_type == "uint8_t" else 0xFFFFFFFF
        actual = emu.uc.reg_read(emu_module.UC_X86_REG_EAX) & width_mask
        if actual != sentinel & width_mask:
            raise AssertionError(
                f"{domain}::{api}: return register 0x{actual:x}, expected 0x{sentinel & width_mask:x}"
            )
    if emu.popped != 0:
        raise AssertionError(f"{domain}::{api}: cdecl callee popped {emu.popped} bytes")
    print(f"PASS {domain}::{api} @ 0x{address_i:08x}: {len(arguments)} args, cdecl")

print(f"IGI-RE Unicorn native ABI probes passed: {len(cases)}")

# Exercise the real retail registration and list-dispatch code, not a fake
# native entry.  A callback registration alone is not a recurring frame tick.
frame_emu = emu_module.Emu(exe=EXE, dll="")
callback = frame_emu.alloc(16)
counter = frame_emu.alloc(4)
frame_emu.write(callback, b"\xff\x05" + struct.pack("<I", counter) + b"\xc3")
frame_emu.call(0x004916C0, callback)
head = struct.unpack("<I", frame_emu.read(0x00BCAE00, 4))[0]
assert head != 0, "Display_AddFrameCallback did not populate the frame list"
next_node, registered = struct.unpack("<II", frame_emu.read(head, 8))
assert registered == callback, "frame list contains the wrong callback"
for frame in range(3):
    frame_emu.call(0x00491CA0, head)
    assert frame_emu.popped == 0, "frame dispatch violated cdecl stack balance"
    assert struct.unpack("<I", frame_emu.read(counter, 4))[0] == frame + 1, "callback did not execute exactly once"
print("PASS retail Display_AddFrameCallback registration and three frame dispatches")
