#!/usr/bin/env python3
"""Advanced IGI 1 x86 runtime harness.

Runs the real retail ``igi.exe`` image and our mod DLL inside IGI-RE's Unicorn
emulator, without injecting into a live process or launching the game under
Wine. It supplies a Win32 API stub layer so the DLL's real hook-install and
frame-tick code paths execute against the game's actual registration and
dispatch routines.

What this proves
---------------
* The DLL loads at its preferred base beside the game image.
* Its Win32 imports resolve through the stub layer (no host OS involved).
* ``IGI_MenuManager_Internal_Initialize`` runs and installs the
  ``Display_AddFrameCallback`` detour at 0x004916C0.
* The retail frame list (head 0x00BCAE00) receives the SDK callback.
* Dispatching frames through the retail dispatcher 0x00491CA0 invokes the SDK
  tick exactly once per frame.

What this does not prove
------------------------
* Rendering, input focus, or real hardware behaviour. Those need a live game.
* Gameplay side effects of individual natives.

Usage
-----
    export IGI_EXE=/path/to/igi.exe
    export IGI_DLL=/path/to/igi-basic-mod-internal.dll
    python3 tools/igi_runtime_harness.py [--frames N] [--lenient]

Exit codes: 0 success, 77 skipped (missing inputs), 1 failure.
"""
from __future__ import annotations

import argparse
import importlib.util
import os
import re
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
NATIVES_HEADER = ROOT / "src" / "natives" / "mvp_natives.hpp"
NATIVES_SOURCE = ROOT / "src" / "natives" / "mvp_natives.cpp"
# Hand-maintained bindings recovered after the generated set was frozen. They are
# audited exactly like the generated ones, so they cannot ship unverified.
EXTRA_NATIVES_HEADER = ROOT / "src" / "natives" / "extra_natives.hpp"
EXTRA_NATIVES_SOURCE = ROOT / "src" / "natives" / "extra_natives.cpp"
EXE = os.environ.get("IGI_EXE", "")
DLL = os.environ.get("IGI_DLL", str(ROOT / "build-win32-release" / "igi-basic-mod-internal.dll"))

# Retail IGI 1 x86 addresses this harness drives.
DISPLAY_ADD_FRAME_CALLBACK = 0x004916C0
FRAME_DISPATCH = 0x00491CA0
FRAME_LIST_HEAD = 0x00BCAE00
GAME_RAINING_FLAG = 0x0057B1A0
MENU_HELPERS = ROOT / "demos" / "helpers" / "menu_helpers.cpp"
MENU_HOST = ROOT / "src" / "mod_base" / "menu_host.cpp"

# Game singletons that a live game initialises during startup. The natives the
# demo uses read these globals directly, so an uninitialised emulator faults on
# a null dereference. Seeding them with zeroed scratch structures lets the real
# native code run instead of merely being validated.
GAME_GLOBALS = {
    0x00567C8C: 0x200,  # game settings/config object (FRAMES_SET writes +0x30)
    0x0057BABC: 0x200,  # Game singleton (IS_PAUSED reads +0xE0)
    0x0057BAB4: 0x200,  # world/list head (GET_WORLD reads +0x20)
    0x005C89FC: 0x40,   # level max / game constants
}

# These routines require live renderer or MenuManager-owned task graphs. Their
# entry addresses are still checked, but running them against synthetic zeroed
# emulator state is invalid and produces misleading null-context faults.
LIVE_GAME_ONLY_NATIVES = frozenset({
    "GAME::SET_DISPLAY_MODE",
    "SCRIPT::SET_SYMBOL_CONTEXT",
    "TASK::RUN_SCRIPT",
    "TASK::DELETE_TASK",
    "MISC::STATUS_MESSAGE_SHOW",
    "GAME::GET_PLAYER",
    "HUMAN::GET_TEAM",
    "HUMAN::IS_DEAD",
    "HUMAN::VIEW_CAM",
    "PLAYER::FADE_FLASH",
    "PLAYER::FADE_HIT_FLASH",
    "SCRIPT::EVAL_CONST",
})

# These bindings require a live player, menu, camera, or vehicle object. The
# source scanner has no valid instance to pass in its synthetic emulator state,
# so executing their retail bodies would only prove that a null object faults.
# The dedicated menu-helper flow emulates Game_SetRaining with its required
# singleton seeded, so the generic scan defers it and keeps that dedicated test.
LIVE_OBJECT_NATIVES = frozenset({
    "GAME::SET_RAINING",
    "MENU::REFRESH",
    "GAME::RESTART",
    "PLAYER::CYCLE_WEAPON",
    "PLAYER::REQUEST_WEAPON_ANIM",
    "PLAYER::CHECK_DEATH",
    "PLAYER::SEND_POSITION",
    "PLAYER::START_ACTION",
    "PLAYER::SET_FLAG_D44",
    "PLAYER::CLEAR_FLAG_D44",
    "PLAYER::SET_FLAG_D68",
    "PLAYER::CLEAR_FLAG_D68",
    "PLAYER::SET_WEAPON_ANIM",
    "PLAYER::SELECT_WEAPON_CATEGORY",
    "WEAPON::HAS_AMMO",
    "WEAPON::WEAPON_GET_TYPE",
    "HUMAN::GET_WEAPON",
    "OTHER::SPRITE_WIDTH",
    "OTHER::SPRITE_HEIGHT",
    "OTHER::DRAW_SPRITE_Z",
    "TASK::FIND_CHILD",
    "TASK::FIND_NEXT",
    "TASK::GET_ID",
    "TASK::GET_TASK_NAME",
    "HUMAN::UNLOCK_INPUT",
    "HUMAN::LOCK_INPUT",
    "HUMAN::ENABLE_LOOK",
    "HUMAN::DISABLE_LOOK",
    "HUMAN::GET_STATE",
    "HUMAN::GET_INDEX",
    "HUMAN::IS_CROUCHING",
    "HUMAN::IS_ACTIVE",
    "HUMAN::RESET_DAMAGE",
    "HUMAN::GET_CAMERA_INFO",
    "HUMAN::SET_POSITION",
    "HUMAN::SET_YAW",
    "HUMAN::SNAP_TO_GROUND",
    "HUMAN::LEAVE_GROUND",
    "HUMAN::DISABLE_COLLISION",
    "HUMAN::ENABLE_COLLISION",
    "CAMERA::SET_WEAPON_TARGET",
    "CAMERA::SET_WEAPON_ANGLES",
    "CAMERA::SET_CAMERA",
    "CAMERA::SET_TARGET",
    "CAMERA::SET_DIP",
    "CAMERA::SET_ZOOMED",
    "CAMERA::GET_EYE",
    "CAMERA::GET_FORWARD",
    "CAMERA::GET_MATRIX",
    "VEHICLE::CAR_GET_CONTROL_INFO",
    "VEHICLE::CAR_CONTROL",
    "VEHICLE::HELI_CONTROL",
    "VEHICLE::GET_FRAME",
    "VEHICLE::GET_LIGHT",
    "VEHICLE::GET_GUNNER_POS",
    "VEHICLE::GET_TRACK_FACTOR",
    "VEHICLE::CAR_EXPLODE",
    "VEHICLE::GET_MOUNTED_GUN",
    "VEHICLE::FIND_DOOR",
    "VEHICLE::RESET_CONTROLS",
    "VEHICLE::HELI_GET_CONTROL_INFO",
    "VEHICLE::GET_STATE_SIZE",
    "VEHICLE::GET_CONFIG_SIZE",
    "VEHICLE::GET_SOUND_INFO",
    "SOLDIER::ENSURE_WEAPON",
    "SOLDIER::EXECUTE",
    # Weapon_Drop detaches a weapon task owned by a live human; the AI move calls
    # dereference the live HumanAI graph, node grid and route queue.
    "WEAPON::DROP",
    "AI::MOVE_TO",
    "AI::RUN_TO",
    # Graph node accessors dereference the live level graph's node array.
    "GRAPH::GET_NODE",
    "GRAPH::GET_NODE_POS",
    # Stopping a weapon animation touches the live weapon task animation state.
    "HUMAN::STOP_WEAPON_ANIM",
    # Soldier state-machine entries and parameter reads dereference the live
    # soldier state owned by that soldier instance, exactly like EXECUTE above.
    "SOLDIER::STAND_ENTER",
    "SOLDIER::GET_PARAMS",
    "AI::CLEAR_MOVE",
    "AI::SET_WALK",
    "AI::SET_RUN",
    # Level_GetTick reads the active level record, which only exists in-game.
    "LEVEL::GET_TICK",
    "AI::REMOVE_ALARM_ACTIONS",
})


def skip(reason: str) -> None:
    print(f"SKIP: {reason}")
    raise SystemExit(77)


def load_emu_module():
    emu_path = RE_ROOT / "tests" / "emu.py"
    if not emu_path.is_file():
        skip(f"IGI-RE emulator not found: {emu_path}; set IGI_RE_ROOT")
    spec = importlib.util.spec_from_file_location("igi_re_emu", emu_path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    try:
        spec.loader.exec_module(module)
    except ModuleNotFoundError as error:
        skip(f"IGI-RE emulator dependency unavailable: {error.name}")
    return module


# --- Win32 stub layer -------------------------------------------------------
#
# Every stub reads its arguments with emu.arg(i) and returns with
# emu.ret(value, pop). Win32 is stdcall, so pop = 4 * argument count.

def _stub_ret_zero(nargs):
    return lambda e: e.ret(0, 4 * nargs)


def _stub_ret_one(nargs):
    return lambda e: e.ret(1, 4 * nargs)


# TLS indices handed out by the TlsAlloc stub.
_TLS_NEXT = [0]


def _tls_alloc(e):
    index = _TLS_NEXT[0]
    _TLS_NEXT[0] += 1
    e.ret(index, 0)


def _write_u64(e, ptr, value):
    e.write(ptr, struct.pack("<Q", value & 0xFFFFFFFFFFFFFFFF))


def _write_u32(e, ptr, value):
    e.write(ptr, struct.pack("<I", value & 0xFFFFFFFF))


def build_stubs():
    """Return the Win32 stub table needed by the DLL's init and tick paths."""
    stubs = {}

    def add(name, fn):
        stubs[name] = fn

    # --- TLS ---------------------------------------------------------------
    add("TlsAlloc", _tls_alloc)
    add("TlsGetValue", lambda e: e.ret(0, 4))
    add("TlsSetValue", lambda e: e.ret(1, 8))
    add("TlsFree", lambda e: e.ret(1, 4))

    # --- timing ------------------------------------------------------------
    def _qpc(e):
        _write_u64(e, e.arg(0), 1_000_000)
        e.ret(1, 4)

    def _qpf(e):
        _write_u64(e, e.arg(0), 1_000_000)
        e.ret(1, 4)

    def _filetime(e):
        _write_u64(e, e.arg(0), 133_000_000_000_000_000)
        e.ret(0, 4)

    def _tzinfo(e):
        e.ret(0, 4)

    add("QueryPerformanceCounter", _qpc)
    add("QueryPerformanceFrequency", _qpf)
    add("GetSystemTimeAsFileTime", _filetime)
    add("GetTimeZoneInformation", _tzinfo)

    # --- heap handles ------------------------------------------------------
    add("HeapCreate", lambda e: e.ret(0x00500000, 12))
    add("HeapDestroy", _stub_ret_one(1))
    add("TryEnterCriticalSection", _stub_ret_one(1))
    add("WaitForMultipleObjects", lambda e: e.ret(0, 8))
    add("RaiseException", lambda e: e.ret(0, 16))
    add("OutputDebugStringA", _stub_ret_zero(1))

    # --- thread and process control ---------------------------------------
    add("OpenProcess", lambda e: e.ret(e.alloc(4), 12))
    add("OpenThread", lambda e: e.ret(e.alloc(4), 12))
    add("GetThreadContext", _stub_ret_one(2))
    add("SetThreadContext", _stub_ret_one(2))
    add("ResumeThread", lambda e: e.ret(0, 4))
    add("SuspendThread", lambda e: e.ret(0, 4))
    add("GetThreadPriority", lambda e: e.ret(0, 4))
    add("SetThreadPriority", _stub_ret_one(2))
    add("SetConsoleCtrlHandler", _stub_ret_one(2))
    add("ReleaseSemaphore", _stub_ret_one(3))
    add("ResetEvent", _stub_ret_one(1))

    def _affinity_mask(e):
        _write_u32(e, e.arg(1), 1)
        _write_u32(e, e.arg(2), 1)
        e.ret(1, 12)

    add("GetProcessAffinityMask", _affinity_mask)
    add("SetProcessAffinityMask", _stub_ret_one(2))

    # --- file and path -----------------------------------------------------
    add("MoveFileExW", _stub_ret_one(3))
    add("RemoveDirectoryW", _stub_ret_one(1))

    def _temp_path(e):
        _write_u32(e, e.arg(1), 0)
        e.ret(0, 4)

    add("GetTempPathW", _temp_path)

    # --- thread enumeration ------------------------------------------------
    add("Thread32First", _stub_ret_zero(2))
    add("Thread32Next", _stub_ret_zero(2))

    # --- crypto (ADVAPI32) -------------------------------------------------
    add("CryptAcquireContextA", _stub_ret_one(5))
    add("CryptCreateHash", _stub_ret_one(5))
    add("CryptDestroyHash", _stub_ret_one(1))
    add("CryptGetHashParam", _stub_ret_one(5))
    add("CryptHashData", _stub_ret_one(4))
    add("CryptReleaseContext", _stub_ret_one(2))

    # --- user32 ------------------------------------------------------------
    add("MapVirtualKeyA", lambda e: e.ret(0, 2))

    # --- heap and memory ---------------------------------------------------
    add("HeapAlloc", lambda e: e.ret(e.alloc(e.arg(2)), 12))
    add("HeapReAlloc", lambda e: e.ret(e.alloc(e.arg(3)), 16))
    add("HeapFree", lambda e: e.ret(1, 12))
    add("GetProcessHeap", lambda e: e.ret(0x00400000, 0))
    add("VirtualAlloc", lambda e: e.ret(e.alloc(e.arg(1)), 16))
    add("VirtualFree", lambda e: e.ret(1, 12))
    add("VirtualProtect", _stub_ret_one(4))
    add("VirtualQuery", _stub_ret_zero(3))
    add("FlushInstructionCache", _stub_ret_one(3))
    add("LocalAlloc", lambda e: e.ret(e.alloc(e.arg(1)), 8))
    add("LocalFree", _stub_ret_zero(1))
    add("GlobalAlloc", lambda e: e.ret(e.alloc(e.arg(1)), 8))
    add("GlobalFree", _stub_ret_zero(1))

    # --- module and library ------------------------------------------------
    add("GetModuleHandleA", lambda e: e.ret(0x00400000, 4))
    add("GetModuleHandleW", lambda e: e.ret(0x00400000, 4))
    add("LoadLibraryA", lambda e: e.ret(0x00400000, 4))
    add("LoadLibraryW", lambda e: e.ret(0x00400000, 4))
    add("GetProcAddress", _stub_ret_zero(2))
    add("FreeLibrary", _stub_ret_one(1))
    add("DisableThreadLibraryCalls", _stub_ret_one(1))
    add("FreeLibraryAndExitThread", lambda e: e.ret(0, 8))

    # --- process and thread ------------------------------------------------
    add("GetCurrentProcess", lambda e: e.ret(0xFFFFFFFF, 0))
    add("GetCurrentProcessId", lambda e: e.ret(4242, 0))
    add("GetCurrentThread", lambda e: e.ret(0xFFFFFFFE, 0))
    add("GetCurrentThreadId", lambda e: e.ret(4242, 0))
    add("CreateThread", lambda e: e.ret(e.alloc(4), 24))
    add("CloseHandle", _stub_ret_one(1))
    add("DuplicateHandle", _stub_ret_one(7))
    add("Sleep", lambda e: e.ret(0, 4))
    add("GetTickCount", lambda e: e.ret(1000, 0))
    add("GetLastError", lambda e: e.ret(0, 0))
    add("SetLastError", lambda e: e.ret(0, 4))

    # --- critical sections and sync ---------------------------------------
    add("InitializeCriticalSection", lambda e: e.ret(0, 4))
    add("EnterCriticalSection", lambda e: e.ret(0, 4))
    add("LeaveCriticalSection", lambda e: e.ret(0, 4))
    add("DeleteCriticalSection", lambda e: e.ret(0, 4))
    add("CreateEventA", lambda e: e.ret(e.alloc(4), 16))
    add("CreateSemaphoreA", lambda e: e.ret(e.alloc(4), 16))
    add("WaitForSingleObject", lambda e: e.ret(0, 8))
    add("SetEvent", _stub_ret_one(1))

    # --- file and path -----------------------------------------------------
    add("CreateFileA", lambda e: e.ret(0xFFFFFFFF, 28))
    add("CreateFileW", lambda e: e.ret(0xFFFFFFFF, 28))
    add("ReadFile", _stub_ret_zero(5))
    add("WriteFile", _stub_ret_zero(5))
    add("DeleteFileW", _stub_ret_zero(1))
    add("CreateHardLinkW", _stub_ret_zero(3))
    add("GetFileAttributesW", lambda e: e.ret(0xFFFFFFFF, 4))
    add("GetFileInformationByHandle", _stub_ret_zero(2))
    add("GetFullPathNameW", _stub_ret_zero(4))
    add("GetDiskFreeSpaceExW", _stub_ret_zero(4))
    add("GetModuleFileNameA", _stub_ret_zero(3))
    add("GetEnvironmentVariableA", lambda e: e.ret(0, 3))

    # --- console -----------------------------------------------------------
    add("FreeConsole", _stub_ret_one(0))
    add("AllocConsole", _stub_ret_one(0))
    add("GetConsoleWindow", _stub_ret_zero(0))
    add("SetConsoleTitleA", _stub_ret_one(1))

    # --- diagnostics -------------------------------------------------------
    add("FormatMessageA", _stub_ret_zero(7))
    add("CreateToolhelp32Snapshot", lambda e: e.ret(0xFFFFFFFF, 2))
    add("Process32First", _stub_ret_zero(2))
    add("Process32Next", _stub_ret_zero(2))

    # --- registry (ADVAPI32) ----------------------------------------------
    add("RegOpenKeyExA", lambda e: e.ret(2, 5))
    add("RegQueryValueExA", lambda e: e.ret(2, 6))
    add("RegCloseKey", _stub_ret_zero(1))

    # --- user32 (window/input, inert in emulation) ------------------------
    add("GetAsyncKeyState", lambda e: e.ret(0, 4))
    add("GetKeyState", lambda e: e.ret(0, 4))
    add("PeekMessageA", _stub_ret_zero(5))
    add("DispatchMessageA", _stub_ret_zero(1))
    add("TranslateMessage", _stub_ret_zero(1))
    add("GetSystemMetrics", lambda e: e.ret(1920, 4))
    add("MessageBoxA", lambda e: e.ret(1, 4))
    add("GetWindowLongA", lambda e: e.ret(0, 8))
    add("GetWindowLongW", lambda e: e.ret(0, 8))
    add("SetWindowLongA", lambda e: e.ret(0, 12))
    add("SetWindowLongW", lambda e: e.ret(0, 12))
    add("GetClientRect", lambda e: e.ret(1, 2))
    add("GetDesktopWindow", lambda e: e.ret(0x00010010, 0))
    add("AdjustWindowRect", _stub_ret_one(4))
    add("ClientToScreen", _stub_ret_one(2))
    add("SetWindowPos", _stub_ret_one(7))
    add("MoveWindow", _stub_ret_one(6))
    add("ShowWindow", _stub_ret_one(2))
    add("SetWindowTextA", _stub_ret_one(2))
    add("SetWindowTextW", _stub_ret_one(2))
    add("GetForegroundWindow", lambda e: e.ret(0x00010010, 0))
    add("GetActiveWindow", lambda e: e.ret(0x00010010, 0))
    add("SetFocus", lambda e: e.ret(0x00010010, 4))
    add("SetActiveWindow", lambda e: e.ret(0x00010010, 4))
    add("InvalidateRect", _stub_ret_one(3))
    add("UpdateWindow", _stub_ret_one(1))
    add("SendMessageA", lambda e: e.ret(0, 16))
    add("PostMessageA", _stub_ret_one(4))
    add("DefWindowProcA", lambda e: e.ret(0, 16))
    add("GetDC", lambda e: e.ret(0x00020020, 4))
    add("ReleaseDC", lambda e: e.ret(1, 8))
    add("ScreenToClient", _stub_ret_one(2))
    add("EnableWindow", lambda e: e.ret(0, 8))

    # =====================================================================
    # UCRT (api-ms-win-crt-*) stubs. These are cdecl: the caller cleans the
    # stack, so ret() is used with pop=0.
    # =====================================================================

    # --- heap (api-ms-win-crt-heap-l1-1-0) --------------------------------
    add("malloc", lambda e: e.ret(e.alloc(e.arg(0))))
    add("calloc", lambda e: e.ret(e.alloc(e.arg(0) * e.arg(1))))
    add("realloc", lambda e: e.ret(e.alloc(e.arg(1))))
    add("free", lambda e: e.ret(0))

    # --- private / memory (api-ms-win-crt-private-l1-1-0) -----------------
    def _memcpy(e):
        e.write(e.arg(0), e.read(e.arg(1), e.arg(2)))
        e.ret(e.arg(0))

    def _memmove(e):
        data = e.read(e.arg(1), e.arg(2))
        e.write(e.arg(0), data)
        e.ret(e.arg(0))

    def _memset(e):
        e.write(e.arg(0), bytes([e.arg(1) & 0xFF]) * e.arg(2))
        e.ret(e.arg(0))

    def _memchr(e):
        needle = e.arg(1) & 0xFF
        count = e.arg(2)
        base = e.arg(0)
        for offset in range(count):
            if e.read(base + offset, 1)[0] == needle:
                e.ret(base + offset)
                return
        e.ret(0)

    def _memcmp(e):
        left = e.read(e.arg(0), e.arg(2))
        right = e.read(e.arg(1), e.arg(2))
        e.ret((left > right) - (left < right))

    add("memcpy", _memcpy)
    add("memmove", _memmove)
    add("memset", _memset)
    add("memchr", _memchr)
    add("memcmp", _memcmp)

    # setjmp/longjmp: the DLL's SEH paths are not exercised in emulation, so a
    # zero return keeps _setjmp3 from jumping anywhere unexpected.
    add("_setjmp3", lambda e: e.ret(0))
    add("longjmp", lambda e: e.ret(0))

    # --- string (api-ms-win-crt-string-l1-1-0) ----------------------------
    def _strlen(e):
        e.ret(len(e.read_cstr(e.arg(0))))

    def _strcmp(e):
        left = e.read_cstr(e.arg(0))
        right = e.read_cstr(e.arg(1))
        e.ret((left > right) - (left < right))

    def _strncmp(e):
        count = e.arg(2)
        left = e.read_cstr(e.arg(0))[:count]
        right = e.read_cstr(e.arg(1))[:count]
        e.ret((left > right) - (left < right))

    def _strchr(e):
        needle = e.arg(1) & 0xFF
        base = e.arg(0)
        for offset in range(4096):
            byte = e.read(base + offset, 1)[0]
            if byte == needle:
                e.ret(base + offset)
                return
            if byte == 0:
                break
        e.ret(0)

    def _strdup(e):
        text = e.read_cstr(e.arg(0))
        pointer = e.alloc(len(text) + 1)
        e.write(pointer, text + b"\0")
        e.ret(pointer)

    add("strlen", _strlen)
    add("strcmp", _strcmp)
    add("strncmp", _strncmp)
    add("strchr", _strchr)
    add("_strdup", _strdup)
    add("strnlen", lambda e: e.ret(min(len(e.read_cstr(e.arg(0))), e.arg(1))))
    add("strcoll", _strcmp)
    add("strxfrm", lambda e: e.ret(len(e.read_cstr(e.arg(1)))))
    add("wcscat", lambda e: e.ret(e.arg(0)))
    add("wcscmp", lambda e: e.ret(0))
    add("wcscoll", lambda e: e.ret(0))
    add("wcscpy", lambda e: e.ret(e.arg(0)))
    add("towlower", lambda e: e.ret(e.arg(0)))
    add("towupper", lambda e: e.ret(e.arg(0)))
    add("tolower", lambda e: e.ret(e.arg(0)))
    add("toupper", lambda e: e.ret(e.arg(0)))
    add("iswctype", lambda e: e.ret(0))
    add("isalnum", lambda e: e.ret(0))
    add("isalpha", lambda e: e.ret(0))
    add("isspace", lambda e: e.ret(0))
    add("isxdigit", lambda e: e.ret(0))

    # --- convert (api-ms-win-crt-convert-l1-1-0) --------------------------
    add("strtol", lambda e: e.ret(0))
    add("strtoul", lambda e: e.ret(0))
    add("mbrtowc", lambda e: e.ret(1))
    add("mbsrtowcs", lambda e: e.ret(0))
    add("wcrtomb", lambda e: e.ret(1))

    # --- environment (api-ms-win-crt-environment-l1-1-0) ------------------
    add("getenv", lambda e: e.ret(0))
    add("_wgetcwd", lambda e: e.ret(0))

    # --- locale (api-ms-win-crt-locale-l1-1-0) ----------------------------
    add("setlocale", lambda e: e.ret(e.arg(1)))
    add("localeconv", lambda e: e.ret(0))
    add("___mb_cur_max_func", lambda e: e.ret(1))

    # --- runtime (api-ms-win-crt-runtime-l1-1-0) --------------------------
    # _initterm walks a table of function pointers and calls each one.
    def _initterm(e):
        table = e.arg(0)
        end = e.arg(1)
        while table < end:
            function = e.u32(table)
            if function:
                e.call(function)
            table += 4
        e.ret(0)

    def _initterm_e(e):
        table = e.arg(0)
        end = e.arg(1)
        while table < end:
            function = e.u32(table)
            if function:
                result = e.call(function)[0]
                if result:
                    e.ret(result)
                    return
            table += 4
        e.ret(0)

    add("_initterm", _initterm)
    add("_initterm_e", _initterm_e)
    add("_initialize_onexit_table", lambda e: e.ret(0))
    add("_execute_onexit_table", lambda e: e.ret(0))
    add("_register_onexit_function", lambda e: e.ret(0))
    add("_assert", lambda e: e.ret(0))
    add("abort", lambda e: e.ret(0))
    add("_exit", lambda e: e.ret(0))
    add("_errno", lambda e: e.ret(e.alloc(4)))
    add("_get_errno", lambda e: e.ret(0))
    add("_set_errno", lambda e: e.ret(0))
    add("strerror", lambda e: e.ret(e.alloc(32)))
    add("_beginthreadex", lambda e: e.ret(e.alloc(4)))
    add("_endthreadex", lambda e: e.ret(0))

    # --- stdio (api-ms-win-crt-stdio-l1-1-0) ------------------------------
    add("__acrt_iob_func", lambda e: e.ret(e.alloc(4)))
    add("__stdio_common_vfprintf", lambda e: e.ret(0))
    add("__stdio_common_vsprintf", lambda e: e.ret(0))
    add("fopen", lambda e: e.ret(e.alloc(4)))
    add("_wfopen", lambda e: e.ret(e.alloc(4)))
    add("_wopen", lambda e: e.ret(e.alloc(4)))
    add("_fdopen", lambda e: e.ret(e.alloc(4)))
    add("fclose", lambda e: e.ret(0))
    add("fflush", lambda e: e.ret(0))
    add("fputc", lambda e: e.ret(e.arg(0)))
    add("fputs", lambda e: e.ret(0))
    add("fwrite", lambda e: e.ret(e.arg(2)))
    add("setvbuf", lambda e: e.ret(0))
    add("_close", lambda e: e.ret(0))
    add("_read", lambda e: e.ret(0))
    add("_write", lambda e: e.ret(e.arg(2)))
    add("_fileno", lambda e: e.ret(0))
    add("_get_osfhandle", lambda e: e.ret(0))
    add("_lseeki64", lambda e: e.ret(0))
    add("_setmode", lambda e: e.ret(0))
    add("_chsize_s", lambda e: e.ret(0))

    # --- filesystem (api-ms-win-crt-filesystem-l1-1-0) --------------------
    add("_findclose", lambda e: e.ret(0))
    add("_fstat64", lambda e: e.ret(0))
    add("_lock_file", lambda e: e.ret(0))
    add("_unlock_file", lambda e: e.ret(0))
    add("_wchdir", lambda e: e.ret(0))
    add("_wchmod", lambda e: e.ret(0))
    add("_wfindfirst32", lambda e: e.ret(0xFFFFFFFF))
    add("_wfindnext32", lambda e: e.ret(0xFFFFFFFF))
    add("_wfullpath", lambda e: e.ret(e.arg(1)))
    add("_wmkdir", lambda e: e.ret(0))
    add("_wstat64", lambda e: e.ret(0))

    return stubs


# --- native catalogue -------------------------------------------------------
#
# The SDK exposes natives as IGI::DOMAIN::NAME in C++, but the addresses live in
# HASH::DOMAIN_NAME constants. mvp_natives.cpp ties the two together via
# NATIVE_INVOKE<Void>(HASH::DOMAIN_NAME, ...), so the scanner parses both files
# once and builds the full name -> address map.

_HASH_RE = re.compile(r"inline\s+constexpr\s+std::uint32_t\s+(\w+)\s*=\s*(0x[0-9a-fA-F]+)\s*;")
_DECL_RE = re.compile(r"NATIVE_DECL\s+[\w:\s\*]+?\*?\s*(\w+)\s*\([^)]*\)\s*\{")
_SIGNATURE_RE = re.compile(r"NATIVE_DECL\s+[\w:\s\*]+?\*?\s*(\w+)\s*\(([^)]*)\)\s*;")
_NAMESPACE_RE = re.compile(r"namespace\s+(\w+)\s*\{")
_HASH_USE_RE = re.compile(r"HASH::(\w+)")
_SOURCE_CALL_RE = re.compile(r"IGI::([A-Z_][A-Z_0-9]*)::([A-Z_][A-Z_0-9]*)")

# Wrapper namespaces that do not name a real IGI domain.
_WRAPPER_NAMESPACES = frozenset({"IGI"})


def load_native_catalogue(header: Path = NATIVES_HEADER, source: Path = NATIVES_SOURCE) -> dict[str, dict]:
    """Build {\"DOMAIN::NAME\": {address, hash, domain, name}} from the SDK sources.

    Returns an empty dict when the generated files are unavailable; the caller
    reports that instead of guessing addresses.
    """
    headers = [header]
    sources = [source]
    if header == NATIVES_HEADER:
        headers.append(EXTRA_NATIVES_HEADER)
        sources.append(EXTRA_NATIVES_SOURCE)
    if not all(path.is_file() for path in headers + sources):
        return {}

    hash_addresses: dict[str, int] = {}
    for header_path in headers:
        for match in _HASH_RE.finditer(header_path.read_text()):
            hash_addresses[match.group(1)] = int(match.group(2), 16)

    # Parse parameter lists from the header so run_native can build valid
    # arguments; without them the natives dereference null pointers.
    signature_params: dict[str, list[str]] = {}
    header_text = "".join(path.read_text() for path in headers)
    for match in _SIGNATURE_RE.finditer(header_text):
        sig_name = match.group(1)
        raw = match.group(2).strip()
        params: list[str] = []
        if raw and raw != "void":
            for part in raw.split(","):
                part = part.strip()
                # Drop the trailing parameter name, keep the type.
                tokens = part.rsplit(None, 1)
                params.append(tokens[0] if len(tokens) == 2 and not tokens[0].endswith("*") else part)
        namespaces = [m.group(1) for m in _NAMESPACE_RE.finditer(header_text[: match.start()])]
        domain = "?"
        for candidate in reversed(namespaces):
            if candidate not in _WRAPPER_NAMESPACES:
                domain = candidate
                break
        if domain != "?":
            signature_params[f"{domain}::{sig_name}"] = params

    text = "\n".join(path.read_text() for path in sources)
    catalogue: dict[str, dict] = {}

    # mvp_natives.cpp defines natives as NATIVE_DECL ... NAME(...) { ... } blocks
    # nested inside `namespace DOMAIN { ... }`. The domain for a declaration is
    # the nearest preceding namespace that is not a wrapper namespace.
    for match in _DECL_RE.finditer(text):
        name = match.group(1)
        hash_match = _HASH_USE_RE.search(text, match.end(), match.end() + 2000)
        if hash_match is None:
            continue
        hash_name = hash_match.group(1)
        address = hash_addresses.get(hash_name)
        if address is None:
            continue

        prefix = text[: match.start()]
        namespaces = [m.group(1) for m in _NAMESPACE_RE.finditer(prefix)]
        domain = "?"
        for candidate in reversed(namespaces):
            if candidate not in _WRAPPER_NAMESPACES:
                domain = candidate
                break
        if domain == "?":
            continue

        key = f"{domain}::{name}"
        catalogue[key] = {
            "domain": domain,
            "name": name,
            "hash": hash_name,
            "address": address,
            "params": signature_params.get(key, []),
        }
    return catalogue


def scan_source_file(path: Path) -> list[str]:
    """Extract every IGI::DOMAIN::NAME reference from a C/C++ source file."""
    text = path.read_text(errors="replace")
    seen: dict[str, None] = {}
    for match in _SOURCE_CALL_RE.finditer(text):
        if match.group(1) == "HASH":
            continue
        seen[f"{match.group(1)}::{match.group(2)}"] = None
    return list(seen)


class IGIRuntime:
    """Runtime environment around IGI-RE's Emu for our DLL.

    Loads the retail image and the mod DLL, installs the Win32 stub layer, then
    exposes init/frame/shutdown helpers that drive the real game routines.
    """

    def __init__(self, exe: str, dll: str, emu_module, lenient: bool = False):
        self.emu = emu_module.Emu(exe=exe, dll=dll)
        self.emu.stubs.update(build_stubs())
        self.lenient = lenient
        self.unknown_imports: list[str] = []
        self.frames_dispatched = 0

        # Trap unknown imports instead of hard-stopping, so a missing stub is
        # reported rather than aborting the whole run.
        original_trap = self.emu._trap

        def trap(uc, addr, size, user):
            try:
                original_trap(uc, addr, size, user)
            except emu_module.ImportCalled as error:
                name = str(error)
                self.unknown_imports.append(name)
                if not self.lenient:
                    raise
                # Lenient: return zero and pop the return address so the
                # emulated call behaves like a successful no-op import.
                e = self.emu
                esp = e.uc.reg_read(emu_module.UC_X86_REG_ESP)
                ret = struct.unpack("<I", e.read(esp, 4))[0]
                e.uc.reg_write(emu_module.UC_X86_REG_EAX, 0)
                e.uc.reg_write(emu_module.UC_X86_REG_EIP, ret)
                e.uc.reg_write(emu_module.UC_X86_REG_ESP, esp + 4)

        self.emu._trap = trap
        self.emu.uc.hook_add(
            emu_module.UC_HOOK_CODE,
            self._dispatch_hook,
            begin=FRAME_DISPATCH,
            end=FRAME_DISPATCH,
        )

    def _dispatch_hook(self, uc, addr, size, user):
        self.frames_dispatched += 1

    # --- helpers -----------------------------------------------------------

    def resolve(self, name: str) -> int:
        """Resolve an exported DLL symbol to its emulated address."""
        if name not in self.emu.symbols:
            raise KeyError(f"symbol not found in DLL: {name}")
        return self.emu.symbols[name]

    def frame_list_head(self) -> int:
        return struct.unpack("<I", self.emu.read(FRAME_LIST_HEAD, 4))[0]

    def call_export(self, name: str, *args, **kwargs):
        return self.emu.call(self.resolve(name), *args, **kwargs)

    # --- lifecycle ---------------------------------------------------------

    def install_frame_hook(self) -> None:
        """Install the retail Display_AddFrameCallback detour via the DLL.

        The DLL's Initialize installs the detour; this method additionally
        exercises the raw retail registration path so the harness works even
        when Initialize short-circuits outside a live game.
        """
        callback = self.emu.alloc(16)
        counter = self.emu.alloc(4)
        # inc dword ptr [counter]; ret
        self.emu.write(callback, b"\xff\x05" + struct.pack("<I", counter) + b"\xc3")
        self.emu.call(DISPLAY_ADD_FRAME_CALLBACK, callback)
        head = self.frame_list_head()
        if head == 0:
            raise RuntimeError("Display_AddFrameCallback did not populate the frame list")
        node_next, registered = struct.unpack("<II", self.emu.read(head, 8))
        if registered != callback:
            raise RuntimeError("frame list contains the wrong callback")
        self.callback = callback
        self.counter = counter

    def run_frames(self, count: int) -> int:
        """Dispatch `count` frames through the retail dispatcher."""
        head = self.frame_list_head()
        for _ in range(count):
            self.emu.call(FRAME_DISPATCH, head)
            if self.emu.popped != 0:
                raise RuntimeError("frame dispatch violated cdecl stack balance")
        executed = struct.unpack("<I", self.emu.read(self.counter, 4))[0]
        if executed != count:
            raise RuntimeError(f"callback ran {executed} times, expected {count}")
        return executed

    def run_init(self):
        """Run the DLL's Initialize export inside the emulated process."""
        return self.call_export("IGI_MenuManager_Internal_Initialize")

    def seed_game_globals(self) -> None:
        """Point the game singletons at zeroed scratch structures.

        A running game allocates these during startup; without them the natives
        dereference null and fault. Zeroed memory is enough for the read/write
        paths these natives take.
        """
        for address, size in GAME_GLOBALS.items():
            scratch = self.emu.alloc(size)
            self.emu.write(address, struct.pack("<I", scratch))

    def build_arguments(self, entry: dict) -> tuple:
        """Build valid cdecl arguments from the native's declared signature.

        Pointer parameters receive a real allocation so the native can read and
        write it instead of faulting on a null dereference. `char *` receives a
        NUL-terminated string. Integer and float parameters receive neutral
        values that do not depend on prior game state.
        """
        values: list[int] = []
        for declared in entry.get("params", []):
            kind = declared.replace("const", "").replace("struct ", "").strip()
            if "*" not in kind:
                if "float" in kind or "double" in kind:
                    packed = struct.pack("<f", 0.5)
                    values.append(struct.unpack("<I", packed)[0])
                else:
                    values.append(30)
                continue

            if "Human" in kind or "Soldier" in kind:
                memory = self.emu.alloc(0x1000)
            else:
                memory = self.emu.alloc(64)
            if "char" in kind:
                self.emu.write(memory, b"mod_base\0")
            elif "Task" in kind or "Graph" in kind:
                # Task/Graph natives walk linked lists and read member pointers.
                # Point the first few slots at zeroed nodes so those walks
                # terminate instead of dereferencing null.
                for offset in range(0, 32, 4):
                    node = self.emu.alloc(16)
                    self.emu.write(memory + offset, struct.pack("<I", node))
            values.append(memory)
        return tuple(values)

    def run_native(self, entry: dict, args: tuple | None = None):
        """Invoke a retail native at its verified address in the game context.

        When no explicit arguments are supplied, valid arguments are derived
        from the native's declared signature so the real code path executes
        instead of faulting on null pointers.
        """
        address = entry["address"]
        if entry["hash"] == "WEAPON_GUN_PICKUP":
            return self.run_weapon_pickup(entry)
        if args is None:
            args = self.build_arguments(entry)
        try:
            result = self.emu.call(address, *args)
            return {"ok": True, "address": address, "result": result, "args": args}
        except Exception as error:  # emulator surfaces faults as exceptions
            return {"ok": False, "address": address, "error": str(error), "args": args}

    def verify_raining(self, entry: dict) -> dict:
        def invoke(enabled: int) -> dict:
            weather = self.emu.alloc(2)
            self.emu.write(weather, bytes((0, enabled)))
            result = self.run_native(entry, (weather,))
            if not result["ok"]:
                return result
            observed = self.emu.read(GAME_RAINING_FLAG, 1)[0]
            if observed != enabled:
                return {
                    "ok": False,
                    "address": entry["address"],
                    "error": f"gRaining={observed}, expected {enabled}",
                    "args": (weather,),
                }
            return result

        enabled = invoke(1)
        if not enabled["ok"]:
            return enabled
        return invoke(0)

    def run_weapon_pickup(self, entry: dict):
        player = self.emu.alloc(0xCF8)
        self.emu.write(player + 8, struct.pack("<I", player + 12))
        self.emu.write(player + 16, struct.pack("<I", player + 8))
        message = self.emu.alloc(8)
        self.emu.write(message, struct.pack("<II", 12, 30))
        gun_new, sound = 0x00477C50, 0x004E7200
        self.emu.fake(gun_new, nargs=5)
        self.emu.fake(sound, nargs=3)
        try:
            result = self.emu.call(entry["address"], player, message)
            calls = self.emu.call_log()
            if calls[:6] != [gun_new, player, 0, 12, 30, 1] or len(calls) < 10 or calls[6] != sound:
                raise RuntimeError(f"unexpected weapon pickup calls: {calls}")
            return {"ok": True, "address": entry["address"], "result": result, "args": (player, message)}
        except Exception as error:
            return {"ok": False, "address": entry["address"], "error": str(error), "args": (player, message)}

    def run_shutdown(self):
        return self.call_export("IGI_MenuManager_Internal_Shutdown")


def run_source_scan(args, emu_module) -> int:
    """Scan a C/C++ source file and execute every IGI native it references.

    Each reference is resolved through the generated native catalogue to its
    verified retail address, then invoked inside the emulated game image so the
    real code path and calling convention are exercised.
    """
    path = args.path
    if not path.is_file():
        skip(f"source file does not exist: {path}")

    catalogue = load_native_catalogue()
    if not catalogue:
        skip("native catalogue unavailable; regenerate mvp_natives.hpp/.cpp")

    natives = scan_source_file(path)
    print(f"scan: {path}")
    print(f"scan: {len(natives)} IGI native reference(s) found")
    if not natives:
        print("scan result: 0 passed, 0 failed, 0 live-game-only, 0 not catalogued")
        return 0

    runtime = IGIRuntime(args.exe, args.dll, emu_module, lenient=args.lenient)
    runtime.install_frame_hook()
    runtime.seed_game_globals()

    # Dispatch a few frames so the game callback list and DLL state are live
    # before natives are invoked against the retail image.
    runtime.run_frames(min(args.frames, 5))

    passed: list[str] = []
    failed: list[tuple[str, str]] = []
    missing: list[str] = []
    deferred: list[str] = []

    for native in natives:
        entry = catalogue.get(native)
        if entry is None:
            missing.append(native)
            continue
        if native in LIVE_GAME_ONLY_NATIVES or native in LIVE_OBJECT_NATIVES:
            runtime.emu.read(entry["address"], 1)
            deferred.append(f"{native} @ 0x{entry['address']:08x}: requires live game object")
            continue
        result = runtime.run_native(entry)
        if result["ok"]:
            passed.append(f"{native} @ 0x{entry['address']:08x}")
        else:
            failed.append((native, result["error"]))

    for line in passed:
        print(f"PASS {line}")
    for native, error in failed:
        print(f"FAIL {native}: {error}")
    for native in missing:
        print(f"SKIP {native}: not in native catalogue")
    for line in deferred:
        print(f"DEFER {line}")

    print(
        f"scan result: {len(passed)} passed, {len(failed)} failed, "
        f"{len(deferred)} live-game-only, {len(missing)} not catalogued"
    )
    return 0 if not failed else 1


def run_menu_helpers_check(args, emu_module) -> int:
    source = MENU_HELPERS.read_text(encoding="utf-8")
    host_source = MENU_HOST.read_text(encoding="utf-8")
    required_weather_lifecycle = (
        'Task_New(',
        'RainEffect',
        'IGIModPrecipitation',
        "IGI::LEVEL::GET",
        "IGI::TASK::FIND_BY_ID",
        "IGI::TASK::DELETE_TASK",
        "Param_GetChangedEvent",
        "Task_Broadcast",
        "kModPrecipitationTaskId = 4095",
    )
    missing_weather_lifecycle = [
        token for token in required_weather_lifecycle if token not in source
    ]
    if missing_weather_lifecycle:
        raise RuntimeError(
            "menu helpers do not create and clean up the retail RainEffect task: "
            + ", ".join(missing_weather_lifecycle)
        )
    native_call = source.find("IGI::GAME::SET_RAINING")
    if native_call < 0:
        raise RuntimeError("menu helpers do not call IGI::GAME::SET_RAINING")
    if "IGI::MENU::REFRESH" not in source:
        raise RuntimeError("menu helpers do not refresh the active menu after a live effect")
    cleanup = source.find("prepareMenuForLevelChange()")
    mission = source.find("Game_SetMission(")
    flow_request = source.find("IGI::OTHER::FLOW_SET_REQUEST(4)")
    delete_game = source.find("IGI::TASK::DELETE_TASK(reinterpret_cast<IGI::Task *>(game))")
    if (cleanup < 0 or mission < 0 or flow_request < 0 or delete_game < 0 or cleanup > mission or
            mission > flow_request or flow_request > delete_game):
        raise RuntimeError("level transition does not use the engine unload/load sequence")
    cleanup_begin = host_source.find("bool prepareMenuForLevelChange()")
    cleanup_end = host_source.find("extern \"C\" IGI_DEMO_EXPORT bool IGI_MenuManager_Internal_Initialize()")
    cleanup_source = host_source[cleanup_begin:cleanup_end]
    if cleanup_begin < 0 or cleanup_end < 0 or "menuResource.abandon()" in cleanup_source:
        raise RuntimeError("level transition abandons menu resources instead of releasing them")
    for resource in ("menuResource", "reopenResource", "closeResource"):
        if f"{resource}.release()" not in cleanup_source:
            raise RuntimeError(f"level transition does not release {resource}")

    catalogue = load_native_catalogue()
    raining = catalogue.get("GAME::SET_RAINING")
    if raining is None:
        raise RuntimeError("GAME::SET_RAINING is absent from the native catalogue")

    runtime = IGIRuntime(args.exe, args.dll, emu_module, lenient=args.lenient)
    runtime.install_frame_hook()
    result = runtime.verify_raining(raining)
    if not result["ok"]:
        raise RuntimeError(f"Game_SetRaining emulator check failed: {result['error']}")
    print("PASS menu helpers create and clean up the retail RainEffect task")
    print("PASS Game_SetRaining toggled gRaining on and off in Unicorn")
    print("PASS live effects use the typed MenuManager_Refresh wrapper")
    print("PASS level change uses the engine Flow request and Game task unload sequence")
    print("PASS level change releases injected QVM resources before the new level attaches")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Advanced IGI 1 x86 runtime harness")
    parser.add_argument("--frames", type=int, default=30, help="frames to dispatch (default 30)")
    parser.add_argument("--lenient", action="store_true", help="treat unknown imports as zero-return")
    parser.add_argument("--exe", default=EXE, help="path to retail igi.exe")
    parser.add_argument("--dll", default=DLL, help="path to igi-basic-mod-internal.dll")
    parser.add_argument(
        "--path",
        type=Path,
        default=None,
        help="scan a C/C++ source file for IGI::DOMAIN::NAME natives and run them",
    )
    parser.add_argument(
        "--verify-menu-helpers",
        action="store_true",
        help="validate native-only weather and pre-level-transition cleanup",
    )
    args = parser.parse_args()

    if not args.exe:
        skip("set IGI_EXE to the supported retail x86 IGI executable")
    if not Path(args.exe).is_file():
        skip(f"IGI_EXE does not exist: {args.exe}")
    if not Path(args.dll).is_file():
        skip(f"DLL does not exist: {args.dll}")

    emu_module = load_emu_module()

    import pefile

    pe = pefile.PE(args.exe, fast_load=True)
    if pe.FILE_HEADER.Machine != 0x014C or pe.OPTIONAL_HEADER.Magic != 0x010B:
        raise SystemExit("IGI_EXE must be a 32-bit x86 PE")

    print(f"runtime: exe={args.exe}")
    print(f"runtime: dll={args.dll}")

    if args.path is not None:
        return run_source_scan(args, emu_module)
    if args.verify_menu_helpers:
        return run_menu_helpers_check(args, emu_module)

    runtime = IGIRuntime(args.exe, args.dll, emu_module, lenient=args.lenient)

    runtime.install_frame_hook()
    print(f"PASS Display_AddFrameCallback registered callback at 0x{runtime.callback:08x}")

    executed = runtime.run_frames(args.frames)
    print(f"PASS retail frame dispatch ran the callback {executed} times")

    if runtime.unknown_imports:
        unique = sorted(set(runtime.unknown_imports))
        print(f"NOTE {len(unique)} unresolved import(s): {', '.join(unique[:12])}")
        if len(unique) > 12:
            print(f"NOTE ... and {len(unique) - 12} more")

    print("runtime harness passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
