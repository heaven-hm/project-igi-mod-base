#!/usr/bin/env python3
"""Guard the frame callback-list ownership boundary.

The Windows hook implementation is not executable on the macOS test build,
so this source-level regression test protects the essential routing contract:
only the verified Display_RunCallbacks detour may process callback-list
requests or bind/drain the scheduler.
"""

from pathlib import Path
import re


SOURCE = Path(__file__).resolve().parents[1] / "src/runtime/hook_manager.cpp"


def body(name: str) -> str:
    source = SOURCE.read_text(encoding="utf-8")
    match = re.search(rf'extern "C" [^{{]+\b{name}\([^)]*\)\s*\{{', source)
    assert match, f"missing {name}"
    start = match.end()
    depth = 1
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index]
    raise AssertionError(f"unterminated {name}")


def test_callback_list_work_is_owned_by_frame_dispatch():
    add_callback = body("displayAddFrameCallbackDetour")
    frame_dispatch = body("displayRunCallbacksDetour")

    assert "tickRegistrationRequested.store" in add_callback
    assert "registerFrameTickOnFrameThread" not in add_callback
    assert "runPendingFromGameHook" not in add_callback
    assert "processFrameTickRequests" in frame_dispatch
    assert "sdkFrameTick" in frame_dispatch


def test_hook_restore_waits_for_an_active_frame_detour_to_return():
    source = SOURCE.read_text(encoding="utf-8")
    frame_dispatch = body("displayRunCallbacksDetour")
    uninstall = re.search(r"bool HookManager::uninstall\(\) noexcept \{(?P<body>[\s\S]*?)\n}\n", source)

    assert uninstall, "missing HookManager::uninstall"
    assert "runDispatchActive.store(true" in frame_dispatch
    assert "runDispatchActive.store(false" in frame_dispatch
    assert "runDispatchActive.load" in uninstall.group("body")
    assert "frame dispatch did not leave the detour" in uninstall.group("body")
    assert uninstall.group("body").index("runDispatchActive.load") < uninstall.group("body").index(
        "MH_DisableHook(reinterpret_cast<void *>(game::displayRunCallbacks))"
    )


if __name__ == "__main__":
    test_callback_list_work_is_owned_by_frame_dispatch()
    test_hook_restore_waits_for_an_active_frame_detour_to_return()
