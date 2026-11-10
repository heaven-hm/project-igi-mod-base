#!/usr/bin/env python3
"""Guard the frame callback-list ownership boundary.

The Windows hook implementation is not executable on the macOS test build,
so this source-level regression test protects the essential routing contract:
only the verified Display_RunCallbacks detour may process callback-list
requests or bind/drain the scheduler.
"""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/runtime/hook_manager.cpp"


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
    uninstall = re.search(
        r"bool HookManager::uninstall\(bool restoreHooks\) noexcept \{(?P<body>[\s\S]*?)\n}\n", source
    )

    assert uninstall, "missing HookManager::uninstall"
    assert "runDispatchActive.store(true" in frame_dispatch
    assert "runDispatchActive.store(false" in frame_dispatch
    body_text = uninstall.group("body")
    assert "runDispatchActive.load" in body_text
    assert "frame dispatch did not leave the detour" in body_text
    # The coexistence branch removes only our chained hook (the other mod's jump
    # is restored), so the retail prologue restore ordering applies to the normal
    # path after the retire decision.
    normal = body_text.split("if (!restoreHooks) {", 1)[-1]
    assert normal.index("runDispatchActive.load") < normal.index(
        "MH_DisableHook(reinterpret_cast<void *>(game::displayRunCallbacks))"
    )


def test_a_second_igi_menu_chains_instead_of_refusing():
    source = SOURCE.read_text(encoding="utf-8")
    assert "bool installCoexistenceHook(" in source
    assert "hasIgiModHook()" in source
    assert "already owned by another IGI mod" in source
    header = (ROOT / "src/runtime/hook_manager.hpp").read_text(encoding="utf-8")
    assert "usedExistingHooks_" in header
    uninstall = re.search(
        r"bool HookManager::uninstall\(bool restoreHooks\) noexcept \{(?P<body>[\s\S]*?)\n}\n",
        source,
    ).group("body")
    assert "if (usedExistingHooks_)" in uninstall
    assert "hooks are intact" in uninstall


def test_runtime_shutdown_retires_hooks_instead_of_patching_live_code():
    """Eject must not restore retail code under a running game: doing so patches
    bytes in a suspended process and froze the game on unload."""
    source = SOURCE.read_text(encoding="utf-8")
    match = re.search(
        r"bool HookManager::uninstall\(bool restoreHooks\) noexcept \{(?P<body>[\s\S]*?)\n}\n",
        source,
    )
    assert match, "missing HookManager::uninstall(bool restoreHooks)"
    uninstall = match.group("body")
    runtime = (ROOT / "src/runtime/runtime.cpp").read_text(encoding="utf-8")

    # The default runtime path retires the hooks and reports them resident.
    assert "if (!restoreHooks) {" in uninstall
    assert "retired_ = true" in uninstall
    assert "hooks retired in place" in uninstall
    # Restoring bytes stays available but is opt-in only.
    assert "MH_DisableHook" in uninstall
    assert "bool restoreHooks = false" in (
        ROOT / "src/runtime/hook_manager.hpp"
    ).read_text(encoding="utf-8")
    # A resident module keeps its native bindings alive for in-flight callbacks.
    assert "hooks_.resident()" in runtime
    assert "native bindings stay loaded" in runtime
    # Retiring must also make every detour a pass-through, otherwise the game
    # keeps seeing mod behaviour (for example edited menu strings) after eject.
    assert "hookBehaviorActive.store(false" in uninstall
    for detour in (
        "displayAddFrameCallbackDetour",
        "displayRunCallbacksDetour",
        "menuManagerGetStringDetour",
    ):
        text = body(detour)
        assert "hookBehaviorActive.load" in text, f"{detour} must pass through when retired"


if __name__ == "__main__":
    test_callback_list_work_is_owned_by_frame_dispatch()
    test_a_second_igi_menu_chains_instead_of_refusing()
    test_runtime_shutdown_retires_hooks_instead_of_patching_live_code()
    test_hook_restore_waits_for_an_active_frame_detour_to_return()
