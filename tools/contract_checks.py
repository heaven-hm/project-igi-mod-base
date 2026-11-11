#!/usr/bin/env python3
"""Static production-contract checks for the Unicorn verification suite.

These were the ctest contract/layout guards; they run directly from
tools/run-unicorn-tests.sh now (no ctest, no host test binaries). The behavioral
retail checks live in tools/igi_runtime_harness.py's --verify-* modes, which
execute the real igi.exe image in Unicorn.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def _read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def check_heaven_surface() -> int:
    heaven = _read("demos/heaven/heaven_menu.cpp")
    helpers = _read("demos/helpers/menu_helpers.cpp")
    imgui_host = _read("src/mod_base/imgui_menu_host.cpp")
    internal_host = _read("src/mod_base/menu_host.cpp")
    windows_dll = _read("src/platform/windows_dll.cpp")
    storage = _read("src/runtime/qvm_param_storage.cpp")

    normal = re.sub(r"#if defined\(IGI_HEAVEN_ADVANCED\).*?#endif\n", "", heaven, flags=re.DOTALL)
    defs = re.findall(
        r'\{"([a-z0-9_]+)",\s*igi::ModMenuItemKind::(\w+),\s*"([^"]+)",\s*"([^"]+)",\s*"([^"]*)"',
        normal,
    )
    multiline = re.findall(
        r'\{"([a-z0-9_]+)",\n\s*igi::ModMenuItemKind::(\w+),\n\s*"([^"]+)",\n\s*"([^"]+)",\n\s*"([^"]*)"',
        normal,
    )
    seen = {entry[0]: entry for entry in defs}
    for entry in multiline:
        seen.setdefault(entry[0], entry)
    ids = list(seen)
    if not ids:
        raise SystemExit("Heaven menu has no configured items")
    if len(ids) != len(set(ids)):
        raise SystemExit(f"duplicate Heaven item ids: {ids}")
    expected = 68
    if len(ids) != expected:
        raise SystemExit(f"normal Heaven must ship exactly {expected} controls, found {len(ids)}")

    categories = {"Weapons", "Player", "Camera", "World", "Vehicles", "AI / NPC", "System"}
    found = {entry[3] for entry in seen.values()}
    if found != categories:
        raise SystemExit(f"Heaven categories drifted: {found}")

    removed = {
        "world_level_tick", "world_flow", "world_events",
        "player_teleport_x", "player_teleport_y", "player_teleport_z", "player_teleport_apply",
        "player_send_x", "player_send_y", "player_send_z", "player_send_apply",
        "human_info",
    }
    leftovers = removed & set(ids)
    if leftovers:
        raise SystemExit(f"removed controls reappeared: {sorted(leftovers)}")
    if "Custom Position" in heaven:
        raise SystemExit("Custom Position must stay removed; Send Player To Target is the path")

    # Every control must be routed: adapter ids in heaven_menu.cpp, everything
    # else through a dedicated branch in executeMenuNativeAction.
    dispatcher_start = helpers.index("void executeMenuNativeAction(")
    dispatcher_end = helpers.index(
        "static bool executeMenuCommandFor(IGI::Task *manager", dispatcher_start
    )
    dispatcher = helpers[dispatcher_start:dispatcher_end]
    dispatched = set(re.findall(r'id == "([a-z0-9_]+)"', dispatcher))
    adapter_full = {
        "weapon_pickup", "weapon_ammo", "npc_ensure_weapon", "weapon_all",
        "human_save", "human_reload", "human_info",
        "player_teleport_target", "landmark_target", "player_send_target",
        "weapon_all",
    }
    human_params = {
        "human_move_scale", "human_jump_speed", "human_jump_height",
        "human_air_speed", "human_damage", "human_fence_damage",
    }
    # Controls whose branch lives in heaven_menu.cpp (the demo owns the weapon
    # table, the human parameter table and the typed/target teleport paths) or
    # in the helpers' dispatcher; everything must be routed in one of the two.
    adapter_ids = adapter_full | human_params
    for item_id in ids:
        in_adapter = item_id in adapter_ids and item_id in heaven
        in_dispatcher = item_id in dispatched
        if not (in_adapter or in_dispatcher):
            raise SystemExit(f"unrouted Heaven control {item_id!r}")

    required_paths = {
        "weapon_drop": "WeaponHelpers(runtime).dropCurrentWeapon",
        "vehicle_drive": "VehicleHelpers(runtime).driveCar",
        "heli_info": "VehicleHelpers(runtime).reportHeliInfo",
        "npc_execute": "NpcHelpers(runtime).executeSoldier",
        "restart_mission": "WorldHelpers(runtime).restartMission",
    }
    for item_id, path in required_paths.items():
        assert path in helpers, f"{item_id} lacks its verified helper path"

    # Human physics: qvm-driven defaults, immediate apply, live read-back.
    for item_id in human_params:
        assert f'"{item_id}"' in heaven, f"{item_id} missing from the Human section"
    assert "prefillHumanControls" in heaven and "captureHumanPlayerBaseline" in heaven, (
        "Human boxes must prefill from a HumanPlayer_LoadParameters baseline"
    )
    for source in (heaven, helpers):
        assert "readLiveHumanPlayerParams" not in source, (
            "reading the humanplayer.qvm globals must go through exactly one method "
            "(readHumanPlayerQvmParams)"
        )
    assert "applyHumanPhysics" in heaven, "human input changes must apply immediately"
    assert "inputDisplayDivisor" in heaven, "human values must display in authored units"
    assert "inputSampleValues" in heaven, "samples must derive from the qvm baseline"
    assert "read back" in helpers and "human physics live" in helpers, (
        "every human write must be verified with a live read-back"
    )
    assert "human physics baseline from HumanPlayer_LoadParameters" in helpers
    for address in ("0x56E1E8", "0x56E1EC", "0x56E1F0", "0x56E1F4", "0x56E204", "0x56E208"):
        assert address in storage, f"the human physics path lost retail address {address}"
    assert "0x56E1F8" not in storage and "0x56E200" not in storage, (
        "the lean/peek globals must not be read or written"
    )

    # The in-menu close button must unwind through the retail leave path:
    # RequestScreen replaced the screen without pushing, so a QVM-side
    # MenuManager_PopScreen popped an empty stack and corrupted the manager -
    # Escape never opened the retail pause menu again after that.
    script = _read("src/ui/menu_script.cpp")
    assert "IGI_SDK_Cancel() - MenuManager_PopScreen" not in script, (
        "the generated screen must not PopScreen; the Cancel callback runs the leave QVM"
    )
    assert "close button" in internal_host and "modmenu-leave.qvm" in internal_host, (
        "the Cancel callback must unwind through the retail leave path"
    )
    assert "were already retired" in _read("src/runtime/hook_manager.cpp"), (
        "teardown after a retire-in-place must be a no-op, not a 5 s tick-removal timeout"
    )
    # One F5 press is enough: staged press and staged attach.
    assert "autoOpenPending" in windows_dll
    assert "menu staged to open on" in internal_host
    # Eject never frees the module from the worker thread.
    assert "FreeLibraryAndExitThread" not in windows_dll
    assert "stays resident" in windows_dll
    # Stale script state must be reclaimed so a restart reopens a full menu.
    assert "isOwnScriptHandler" in _read("src/ui/menu_script_bridge.cpp")
    assert "ownedName" in _read("src/ui/menu_resource.cpp")
    assert "countMissingPageWidgets" in internal_host and "attachHealAttempts" in internal_host, (
        "an attach without its widget tasks must self-heal onto a fresh task-id range"
    )
    assert "produced no entries for item" in _read("src/ui/menu_script_bridge.cpp"), (
        "a zero-entry FillList must be logged loudly"
    )
    assert "revertMenuState" in internal_host and "releaseScriptSession" in internal_host

    # External ImGui parity: shared table, DirectX 7 renderer, shutdown exports.
    for pair in ("demos/internal", "demos/external"):
        basic = _read(f"{pair}/basic-mod/basic_menu.cpp")
        simple = _read(f"{pair}/simple-mod/simple_menu.cpp")
        advanced = _read(f"{pair}/advanced-mod/advanced_menu.cpp")
        assert "human_jump_speed" in advanced, f"{pair} advanced lost the Human section"
        assert basic and simple and advanced
    for token in ("ImGui_ImplDX7_Init", "ImGui_ImplWin32_Init", "IGI_MenuManager_External_Shutdown"):
        assert token in imgui_host, f"external host lost {token}"

    print(f"PASS Heaven surface: {len(ids)} controls, Human baseline units, eject and restart pins")
    return 0


def check_native_signatures() -> int:
    from contract_native_signatures import run

    return run()


def run() -> int:
    failures = 0
    for name, check in (("production contract", check_heaven_surface),
                        ("native signatures", check_native_signatures)):
        try:
            check()
        except SystemExit as error:
            print(f"FAIL {name}: {error}")
            failures += 1
    if failures:
        raise SystemExit(f"{failures} contract check(s) failed")
    print("contract checks: ALL PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(run())
