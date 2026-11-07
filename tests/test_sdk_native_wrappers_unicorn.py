#!/usr/bin/env python3
"""Run SDK-generated native wrappers inside IGI-RE's x86 Unicorn emulator."""
import importlib.util
import os
import platform
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


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
DLL = os.environ.get("IGI_SDK_TEST_DLL", "") or (sys.argv[1] if len(sys.argv) > 1 else "")

if not EXE:
    print("SKIP: set IGI_EXE to the supported retail x86 IGI executable")
    raise SystemExit(77)
if sys.platform == "darwin" and platform.machine() == "arm64":
    print("SKIP: run this test with x86_64 Python under Rosetta")
    raise SystemExit(77)
if not DLL or not Path(DLL).is_file():
    raise SystemExit("IGI_SDK_TEST_DLL must point to the built x86 SDK ABI fixture DLL")

emu_path = RE_ROOT / "tests" / "emu.py"
if not emu_path.is_file():
    raise SystemExit(f"IGI-RE emulator not found: {emu_path}; set IGI_RE_ROOT")
spec = importlib.util.spec_from_file_location("igi_re_emu_sdk_wrapper_test", emu_path)
emu_module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = emu_module
spec.loader.exec_module(emu_module)

CASES = [
    ("FRAMES_SET", 0x00402820, "igi_sdk_probe_frames_set", [37], None),
    ("SET_RAINING", 0x00416110, "igi_sdk_probe_set_raining", [b"\0\x01"], None),
    ("SET_UNLIMITED_AMMO", 0x00410360, "igi_sdk_probe_unlimited_ammo", [], None),
    ("SET_DISPLAY_MODE", 0x00416010, "igi_sdk_probe_display_mode", [], None),
    ("MUSIC_SET_VOLUME", 0x00495E70, "igi_sdk_probe_music_volume", [0.5, 0.75], None),
    ("GET_EVENT0", 0x004167D0, "igi_sdk_probe_get_event0", [], 0xA5),
    ("GET_EVENT1", 0x004167E0, "igi_sdk_probe_get_event1", [], 0xA5),
    ("GET_EVENT2", 0x004167F0, "igi_sdk_probe_get_event2", [], 0xA5),
    ("GET_EVENT3", 0x00416810, "igi_sdk_probe_get_event3", [], 0xA5),
    ("GET_WORLD", 0x00416920, "igi_sdk_probe_get_world", [], 0x13572468),
    ("GET", 0x00416D40, "igi_sdk_probe_game_get", [], 0x24681357),
    ("RUN_SCRIPT", 0x004F0CF0, "igi_sdk_probe_run_script", [0x12345678, b"LOCAL:menu.qvm\0", 0], 0x10293847),
]


def arg_word(emu, value):
    if isinstance(value, bytes):
        pointer = emu.alloc(len(value))
        emu.write(pointer, value)
        return pointer
    if isinstance(value, float):
        return struct.unpack("<I", struct.pack("<f", value))[0]
    return value


def return_zero(emu):
    emu.ret(0)


def allocate(emu):
    emu.ret(emu.alloc(max(emu.arg(0), 1)))


def compare_memory(emu):
    left = emu.read(emu.arg(0), emu.arg(2))
    right = emu.read(emu.arg(1), emu.arg(2))
    emu.ret((left > right) - (left < right))


def bounded_strlen(emu):
    text = emu.read(emu.arg(0), emu.arg(1))
    emu.ret(text.find(b"\0") if b"\0" in text else len(text))


emu = emu_module.Emu(exe=EXE, dll=DLL)
# The SDK's trace/debug logging is disabled by fixture initialization. These
# narrow CRT/C++ stubs cover string/vector allocation and formatted log text.
for name in ("snprintf", "_snprintf"):
    emu.stub(name, return_zero)
emu.stub("memcmp", compare_memory)
emu.stub("strnlen", bounded_strlen)
errno_storage = emu.alloc(4)
emu.stub("_errno", lambda e: e.ret(errno_storage))
for name in ("_Znwj", "_Znaj", "_Znwy", "_Znay"):
    emu.stub(name, allocate)
for name in ("_ZdlPv", "_ZdaPv", "_ZdlPvj", "_ZdaPvj", "_ZdlPvy", "_ZdaPvy"):
    emu.stub(name, return_zero)
for name in ("_Unwind_SjLj_Register", "_Unwind_SjLj_Unregister"):
    emu.stub(name, return_zero)
for name in ("GetCurrentThreadId", "pthread_self"):
    emu.stub(name, lambda e: e.ret(1))
for name in ("EnterCriticalSection", "LeaveCriticalSection", "pthread_mutex_init", "pthread_mutex_destroy",
             "pthread_mutex_lock", "pthread_mutex_unlock"):
    pop = 4 if name in ("EnterCriticalSection", "LeaveCriticalSection") else 0
    emu.stub(name, lambda e, stack_bytes=pop: e.ret(0, stack_bytes))

emu.call("igi_sdk_probe_initialize", check_saved=False)
for api, native_address, wrapper, values, expected_return in CASES:
    words = [arg_word(emu, value) for value in values]
    emu.fake(native_address, ret=expected_return or 0x13572468, nargs=len(words))
    offset = len(emu.call_log())
    result = emu.call(wrapper, *words, check_saved=False)
    observed = emu.call_log()[offset:]
    if observed != [native_address, *words]:
        raise AssertionError(f"{api}: SDK wrapper reached {observed!r}, expected {[native_address, *words]!r}")
    if expected_return is not None and result[0] != expected_return:
        raise AssertionError(f"{api}: wrapper returned 0x{result[0]:x}, expected 0x{expected_return:x}")
    if emu.popped != 0:
        raise AssertionError(f"{api}: SDK wrapper violated cdecl stack balance ({emu.popped} bytes)")
    print(f"PASS SDK::{api}: resolved and invoked 0x{native_address:08x}")

print(f"SDK native wrapper probes passed: {len(CASES)}")
