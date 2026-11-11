#!/usr/bin/env python3
"""Keep every Heaven production-menu control connected to a real helper path."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
HEAVEN = (ROOT / "demos/heaven/heaven_menu.cpp").read_text(encoding="utf-8")
HELPERS = (ROOT / "demos/helpers/menu_helpers.cpp").read_text(encoding="utf-8")
IMGUI_HOST = (ROOT / "src/mod_base/imgui_menu_host.cpp").read_text(encoding="utf-8")
INTERNAL_HOST = (ROOT / "src/mod_base/menu_host.cpp").read_text(encoding="utf-8")
WINDOWS_DLL = (ROOT / "src/platform/windows_dll.cpp").read_text(encoding="utf-8")

# The normal Heaven target must remain the concise 50-control surface.  Advanced
# additions are compiled out of that target, so remove those guarded blocks before
# checking the common definition table.
NORMAL_HEAVEN = re.sub(
    r"#if defined\(IGI_HEAVEN_ADVANCED\).*?#endif\n", "", HEAVEN, flags=re.DOTALL
)

# Every normal control is declared once in the definition table:
#   {"id", igi::ModMenuItemKind::Kind, "Label", "Category", "Action", values},
defs = re.findall(
    r'\{"([a-z0-9_]+)",\s*igi::ModMenuItemKind::(\w+),\s*"([^"]+)",\s*"([^"]+)",\s*"([^"]*)"',
    NORMAL_HEAVEN,
)
multiline = re.findall(
    r'\{"([a-z0-9_]+)",\n\s*igi::ModMenuItemKind::(\w+),\n\s*"([^"]+)",\n\s*"([^"]+)",\n\s*"([^"]*)"',
    NORMAL_HEAVEN,
)
seen = {entry[0]: entry for entry in defs}
for entry in multiline:
    seen.setdefault(entry[0], entry)
defs = list(seen.values())
assert defs, "Heaven menu has no configured items"
ids = [entry[0] for entry in defs]
assert len(ids) == len(set(ids)), f"duplicate Heaven item IDs: {ids}"

MIN_OPTIONS = 30
NORMAL_OPTIONS = 69
assert MIN_OPTIONS <= len(ids) == NORMAL_OPTIONS, (
    f"normal Heaven must ship exactly {NORMAL_OPTIONS} working controls, found {len(ids)}"
)
ADVANCED_HEAVEN = HEAVEN.split("#if defined(IGI_HEAVEN_ADVANCED)", 1)[1]
assert "#if defined(IGI_HEAVEN_ADVANCED)" in HEAVEN and all(
    marker in ADVANCED_HEAVEN
    for marker in ('"player_ground_state"', '"npc_movement"', '"npc_ground_state"')
), ("Heaven Advanced must add its live NPC controls without changing normal Heaven")

categories = {"Weapons", "Player", "Human", "Camera", "World", "Vehicles", "AI / NPC", "System"}
found_categories = {entry[3] for entry in defs}
assert found_categories == categories, (
    f"Heaven categories must exactly match the production category set; got {found_categories}"
)

# Raw input lock stays out of the menu: a player can lock themselves out of the
# game with it. Look control is a camera control, so it must live in Camera.
kinds = {entry[0]: (entry[1], entry[3]) for entry in defs}
assert "input" not in ids, "Heaven must not expose the raw player input lock"
assert "weapon_flash" not in ids, "Heaven must not expose cosmetic flash clearing"
assert not {"music", "sfx", "audio_info", "audio_rate"} & set(ids), (
    "Heaven must keep its production controls focused on gameplay rather than audio"
)
assert kinds.get("look", (None, None))[1] == "Camera", (
    "look control is a camera control and must not reappear in Player"
)
assert kinds.get("third_person_crosshair", (None, None))[1] == "Camera", (
    "the third-person crosshair control must remain a camera control"
)

# Camera state must come from the retail camera-info handler, not a helper cache.
assert "const int mode = active ? 2 : 1;" in HELPERS, (
    "the retail camera path uses mode 1 for first person and mode 2 for third person"
)
assert "IGI::HUMAN::GET_CAMERA_INFO" in HELPERS, (
    "camera state must come from the retail camera-info handler, not a helper-owned cache"
)
assert "IGI::OTHER::DRAW_SPRITE_Z" in HELPERS and "IGI::OTHER::STATUS_SCREEN_GET" in HELPERS, (
    "the third-person crosshair must use the retail HUD sprite draw path"
)
assert '"void Human_SnapToGround(Human *h)"' in HELPERS, (
    "ground recovery must refuse an unresolved native instead of reporting a false success"
)
assert '"void Human_EnableCollision(Human *h)"' in HELPERS, (
    "collision controls must refuse an unresolved native instead of reporting a false success"
)
assert '"void Vehicle_ResetControls(Vehicle *v)"' in HELPERS, (
    "vehicle control resets must use the catalogued signature, not a widened pointer"
)

# Routing is checked inside executeMenuNativeAction only, so an unrelated match
# elsewhere in the helper file cannot hide a control that silently falls through
# to the "menu action has no native adapter" branch.
dispatcher_start = HELPERS.index("void executeMenuNativeAction(")
dispatcher_end = HELPERS.index("static bool executeMenuCommandFor(IGI::Task *manager", dispatcher_start)
DISPATCHER = HELPERS[dispatcher_start:dispatcher_end]
dispatched = set(re.findall(r'id == "([a-z0-9_]+)"', DISPATCHER))
assert 'id == "npc_movement"' in DISPATCHER, (
    "Heaven Advanced selected-NPC movement must have a dedicated game-thread route"
)
assert 'id == "npc_ground_state"' in DISPATCHER and "leaveHumanGround(npc)" in DISPATCHER, (
    "Heaven Advanced selected-NPC ground state must use the retail ground helpers"
)
assert 'id == "player_ground_state"' in DISPATCHER and "leaveHumanGround(player)" in DISPATCHER, (
    "Heaven Advanced player ground state must use the retail ground helpers"
)
assert 'id == "heli_reset"' in DISPATCHER and "VehicleHelpers(runtime).resetHeli" in DISPATCHER, (
    "selected-helicopter reset must have a dedicated game-thread route"
)
assert 'id == "heli_stop"' in DISPATCHER and 'id == "heli_control"' in DISPATCHER, (
    "helicopter stop/drive controls must have dedicated game-thread routes"
)
assert "VehicleHelpers(runtime).driveHeli" in DISPATCHER, (
    "helicopter controls must call the retail Heli_Control native"
)
assert 'id == "tank_drive"' in DISPATCHER and "VehicleHelpers(runtime).driveCar" in DISPATCHER, (
    "tank drive must reuse the retail Car_Control native"
)
assert all(native in HELPERS for native in ("IGI::AI::SET_WALK", "IGI::AI::SET_RUN", "IGI::AI::CLEAR_MOVE")), (
    "selected-NPC movement must use the catalogued HumanAI retail calls"
)

# The weapon controls are resolved by the Heaven adapter, because only the demo
# owns the retail weapon symbol table and its ammo counts.
adapter_id_equals = {"player_teleport_target"}
adapter_prefix = "player_teleport_"
adapter_full = {
    "weapon_pickup",
    "weapon_ammo",
    "npc_ensure_weapon",
    "weapon_all",
    "human_save",
    "human_reload",
}
adapter_human_params = {
    "human_move_scale",
    "human_jump_speed",
    "human_jump_height",
    "human_air_speed",
    "human_damage",
    "human_fence_damage",
}
for item_id in ids:
    if item_id in adapter_id_equals:
        assert f'id == "{item_id}"' in HEAVEN, f"{item_id} lost its adapter route"
        continue
    if item_id.startswith(adapter_prefix):
        assert 'id.starts_with("player_teleport_")' in HEAVEN, (
            "the coordinate boxes must share the typed-position adapter branch"
        )
        continue
    if item_id in adapter_full:
        assert f'config.items[item].id == "{item_id}"' in HEAVEN
        continue
    if item_id in adapter_human_params:
        assert "kHumanParamIds" in HEAVEN and f'"{item_id}"' in HEAVEN, (
            f"{item_id} lost its Human parameter adapter route"
        )
        continue
    assert item_id in dispatched, (
        f"unrouted Heaven control {item_id!r}: no dedicated branch in executeMenuNativeAction"
    )

required_native_paths = {
    "weapon_pickup": "WeaponHelpers(runtime).pickup",
    "weapon_drop": "WeaponHelpers(runtime).dropCurrentWeapon",
    "weapon_cycle": "WeaponHelpers(runtime).cycle(1)",
    "weapon_draw_anim": "WeaponHelpers(runtime).requestAnimation",
    "weapon_category": "WeaponHelpers(runtime).selectCategory",
    "vehicle_reset": "VehicleHelpers(runtime).resetCar",
    "vehicle_explode": "VehicleHelpers(runtime).explodeCar",
    "vehicle_drive": "VehicleHelpers(runtime).driveCar",
    "vehicle_info": "VehicleHelpers(runtime).reportCarInfo",
    "vehicle_controls_reset": "VehicleHelpers(runtime).resetVehicleControls",
    "heli_info": "VehicleHelpers(runtime).reportHeliInfo",
    "npc_execute": "NpcHelpers(runtime).executeSoldier",
    "npc_snap_ground": "NpcHelpers(runtime).snapToGround",
    "npc_collision": "NpcHelpers(runtime).setCollisionEnabled",
        "npc_params": "NpcHelpers(runtime).reportSoldierParams",
    "npc_stand_down": "NpcHelpers(runtime).standDownSoldier",
    "player_reset_damage": "PlayerHelpers(runtime).resetDamage",
    "player_snap_ground": "PlayerHelpers(runtime).snapToGround",
    "player_collision": "PlayerHelpers(runtime).setCollisionEnabled",
        "camera_info": "CameraHelpers(runtime).reportInfo",
    "heli_stop": "VehicleHelpers(runtime).driveHeli",
    "heli_control": "VehicleHelpers(runtime).driveHeli",
    "tank_drive": "VehicleHelpers(runtime).driveCar",
    "human_info": "readLiveHumanPlayerParams",
    "npc_reset_damage": "NpcHelpers(runtime).resetDamage",
    "restart_mission": "WorldHelpers(runtime).restartMission",
}
for item_id, native_path in required_native_paths.items():
    source = HEAVEN if item_id == "weapon_pickup" else HELPERS
    assert native_path in source, f"{item_id} lacks its verified helper path"

assert '"Live Car / Tank Target"' in HEAVEN and '"Waiting for live car list"' in HEAVEN, (
    "Heaven must render names from the active car list rather than ordinal slots"
)
assert '"Live NPC Target"' in HEAVEN and '"Waiting for live NPC list"' in HEAVEN, (
    "Heaven must render names from the active NPC list rather than ordinal slots"
)
assert '"Live Heli Target"' in HEAVEN and '"Waiting for live heli list"' in HEAVEN, (
    "Heaven must render names from the active helicopter list rather than ordinal slots"
)
assert "IGI::TASK::GET_TASK_NAME" in HELPERS and "IGI::TASK::GET_ID" in HELPERS, (
    "live targets must use the retail task name and stable task ID natives"
)
assert "liveTaskName(task)" in HELPERS, (
    "both live lists must resolve names through the shared retail name lookup"
)
assert "IGI::TASK::GET_NAME(type)" in HELPERS, (
    "a task without an instance name must fall back to its retail type name"
)
for ordinal in ("NPC " + "1", "Car " + "1", "NPC #", "Car #"):
    assert ordinal not in HEAVEN and ordinal not in HELPERS, (
        f"live target lists must never render ordinal labels such as {ordinal!r}"
    )
assert "findLiveTarget(runtime, g_selectedCarTaskId, isCarTask)" in HELPERS, (
    "vehicle actions must resolve the selected live car by task identity at action time"
)
assert "findLiveTarget(runtime, g_selectedNpcTaskId, isNpcTask)" in HELPERS, (
    "NPC actions must resolve the selected live NPC by task identity at action time"
)

# The old read-only diagnostics and the duplicate typed-XYZ teleport were
# removed from the production menu; Send Player Position is the only custom
# coordinate path.
assert not ({"world_level_tick", "world_flow", "world_events"} & set(ids)), (
    "Level Tick, Flow Counters and Game Events must stay out of the Heaven menu"
)
assert not (
    {"player_teleport_x", "player_teleport_y", "player_teleport_z", "player_teleport_apply"}
    & set(ids)
), "the duplicate Move Player To Position boxes must stay out; Send Player Position already provides the typed-XYZ path"

# humanplayer.qvm physics: every DefineHumanPlayerGeneral param up to the
# damage/health pair must be read from the live BSS block and written back, with
# a reload through HumanPlayer_LoadParameters. The lean/peek params stay out.
assert all(
    f'"{item_id}"' in HEAVEN
    for item_id in (
        "human_move_scale",
        "human_jump_speed",
        "human_jump_height",
        "human_air_speed",
        "human_damage",
        "human_fence_damage",
    )
), "the Human section must expose each qvm physics parameter separately"
# Match the parameter name as a word: a plain substring test also rejected
# innocent words such as "clean" in comments.
assert not re.search(r"\blean\b", HEAVEN, flags=re.IGNORECASE), (
    "the lean/peek params must not be exposed"
)
assert "saveLiveHumanPlayerParams" in HEAVEN and "reloadLiveHumanPlayerParams" in HEAVEN
STORAGE_HELPERS = (ROOT / "demos/helpers/menu_helpers.cpp").read_text(encoding="utf-8")
assert "read back" in STORAGE_HELPERS and "human physics live" in STORAGE_HELPERS, (
    "every human write must be verified with an immediate live read-back"
)
assert "moveScale <= 0.0F" in STORAGE_HELPERS, (
    "a write before the qvm ever ran must load the authored baseline first"
)
assert "readHumanPlayerQvmParams" in HELPERS and "writeHumanPlayerQvmParams" in HELPERS
assert "HUMAN_PLAYER_LOAD_PARAMETERS" in HELPERS and "reloadHumanPlayerParameters" in HELPERS
STORAGE = (ROOT / "src/runtime/qvm_param_storage.cpp").read_text(encoding="utf-8")
for address, name in (
    ("0x56E1E8", "gMoveScale"),
    ("0x56E1EC", "gJumpSpeed"),
    ("0x56E1F0", "gJumpUp"),
    ("0x56E1F4", "gHumanAirSpeed"),
    ("0x56E204", "gMaxDamage"),
    ("0x56E208", "gFenceDamage"),
):
    assert address in STORAGE, f"{name} must read the retail {address} global"
assert "0x56E1F8" not in STORAGE and "0x56E200" not in STORAGE, (
    "the lean/peek globals must not be read or written"
)

# A level/game restart can leave the previous session's IGI_SDK_* symbols and
# LOCAL:mod_base QVM resource registered. Both must be reclaimed, or the next
# open runs a stale script and shows a frame with no options.
BRIDGE = (ROOT / "src/ui/menu_script_bridge.cpp").read_text(encoding="utf-8")
RESOURCE = (ROOT / "src/ui/menu_resource.cpp").read_text(encoding="utf-8")
assert "isOwnScriptHandler" in BRIDGE and "kOwnScriptSymbols" in BRIDGE, (
    "stale IGI_SDK_* symbols from the previous menu session must be reclaimed"
)
assert "belongs to another module" in BRIDGE, (
    "a foreign mod's script symbol must never be removed"
)
assert "ownedName" in RESOURCE and "LOCAL:mod_base/" in RESOURCE, (
    "only our own LOCAL:mod_base QVM resource may be reclaimed after a restart"
)
assert "revertMenuState" in INTERNAL_HOST and "releaseScriptSession" in INTERNAL_HOST, (
    "a rejected close must abandon the stale session so the next open rebuilds it"
)

# Eject must never free the module from the worker thread; the game can still be
# inside a retired detour (the End-key/F12 eject crash).
# One F5 press must be enough: a press during a level load stages the open and
# the menu appears on the first active gameplay frame.
assert "autoOpenPending" in WINDOWS_DLL, (
    "the host must retry a staged start-key press while the level loads"
)
assert "menu staged to open on" in INTERNAL_HOST, (
    "the toggle must stage the attach when gameplay is not active yet"
)
assert "FreeLibraryAndExitThread" not in WINDOWS_DLL, (
    "eject must keep the DLL resident instead of freeing it from the worker thread"
)
assert "stays resident" in WINDOWS_DLL, "eject must report the resident safe-unload path"
assert "findLiveTarget(runtime, *selected, isHeliTask)" in HELPERS, (
    "heli actions must resolve the selected live helicopter by task identity at action time"
)
assert 'clearLiveTargetSelections("level change unloads the current level task list")' in HELPERS, (
    "a level change must drop stale NPC/car/heli selections instead of logging fake warnings"
)
assert 'clearLiveTargetSelections("mission restart rebuilds the level task list")' in HELPERS, (
    "a mission restart must drop stale NPC/car/heli selections too"
)
assert "reportMissingCarTarget(" in HELPERS and "reportMissingNpcTarget(" in HELPERS, (
    "target-dependent actions must distinguish a missing selection from a stale one"
)
assert "invoking Human_SnapToGround for task #" in HELPERS, (
    "ground recovery must log the native invocation so a crash inside it is traceable"
)
assert "invoking Car_Control reset for task #" in HELPERS, (
    "car resets must log the native invocation so a crash inside it is traceable"
)
assert "target.taskId == preserved" in HELPERS, (
    "live target lists must preserve the selected task identity, not its old index"
)
assert "preservedIsLive" in HELPERS, (
    "a selection that vanished from the task table must be dropped, not silently re-pointed"
)
assert "listSelections[index] = static_cast<int>(item.defaultSelection);" in IMGUI_HOST, (
    "the external menu must adopt the refreshed task-ID-preserving selection"
)
assert "onInternalDisplayEndFrame" in INTERNAL_HOST and "ensureEndFrameCallbackRegistered" in INTERNAL_HOST, (
    "the internal Heaven frontend must render the shared end-frame callback too"
)
assert "unregisterEndFrameCallback();" in INTERNAL_HOST, (
    "the internal end-frame callback must be removed before the DLL can unload"
)
assert "menuBridge->resync(menuConfig)" in INTERNAL_HOST, (
    "the internal menu must resync its script bridge after refreshing live targets"
)
# FLOW_GET_SCREEN returns the Flow's Screen task, not the MenuManager. Reading the
# current screen id from it made the host think its screen was gone (double open)
# and that the retail pause menu was absent (pause-menu interference).
assert "liveMenuManager()" in INTERNAL_HOST and "IGI::OTHER::FLOW_GET_SCREEN(" not in INTERNAL_HOST, (
    "the host must read the current screen id from the live MenuManager task"
)
assert "initialAttachPending.exchange(false" in INTERNAL_HOST, (
    "one F5 press must perform exactly one attach attempt"
)
assert "menuFrameWork();" not in INTERNAL_HOST.split("IGI_MenuManager_Internal_RunFrame")[1][:400], (
    "RunFrame must not run a second menuFrameWork in the same frame"
)
assert "Task_Broadcast" in INTERNAL_HOST and "live target lists refreshed" in INTERNAL_HOST, (
    "the internal menu must publish refreshed live targets to its attached retail screen"
)

assert 'WeaponEntry{"Binoculars", "WEAPON_ID_BINOCULARS", 0}' in HEAVEN, (
    "binocular pickup must use its retail symbol with no invented ammo count"
)
# Ammo is typed into a text box and applied, so the refill path must carry the
# user's amount instead of a hard-coded per-weapon constant.
assert 'config.items[item].id == "weapon_ammo"' in HEAVEN and '"weapon_ammo"' in HEAVEN, (
    "Heaven must expose an ammo amount input control"
)
assert "igi::ModMenuItemKind::Input" in HEAVEN, (
    "the ammo amount control must be a numeric text box, not a fixed action"
)
# The ammo text box's Apply button refills the selected weapon, so no separate
# Refill control exists in the menu at all.
assert "Refill Selected Weapon" not in HEAVEN and "Refill All Weapons" not in HEAVEN, (
    "the standalone Refill controls must stay removed; Apply on the text box refills"
)
assert '"RefillWithAmmo"' in HEAVEN, (
    "the ammo text box must apply the typed amount to the selected weapon"
)
assert "g_refillAmmo" in HEAVEN and "g_selectedWeapon" in HEAVEN, (
    "the weapon refill path must keep the typed ammo and selected weapon"
)
# The retail ListBox is a fixed-size widget: the command list must be paged in
# place instead of being filled with one huge list (which crashed the screen).
BRIDGE = (ROOT / "src/ui/menu_script_bridge.cpp").read_text(encoding="utf-8")
assert "kCommandsPerPage" in BRIDGE and "refreshScreen()" in BRIDGE, (
    "the retail command list must be paged and re-collected in place"
)
for stale in ("weapon_refill_glock", "weapon_refill_mp5sd", "weapon_refill_m16a2",
              "weapon_refill_ak47"):
    assert stale not in HEAVEN, f"per-weapon refill control {stale!r} must be removed"
assert "NpcHelpers(runtime).ensureWeapon(npc, weapon.id, ammo)" in HEAVEN, (
    "NPC equip must grant a real retail weapon task before claiming the slot"
)
assert "WeaponGunPickup" in HELPERS and "IGI::PLAYER::PICK_UP_WEAPON(npc, args)" in HELPERS, (
    "NPC equip must create the weapon task on the NPC through WeaponGunPickup"
)
assert "IGI::SOLDIER::ENSURE_WEAPON(npc)" in HELPERS, (
    "NPC equip must still claim retail weapon slot 0"
)
# The crosshair is a third-person replacement, not a second copy of the retail
# crosshair sprite: gating it on WeaponType.sightDisplay made the toggle appear
# dead on every weapon whose sight type is NONE.
assert "return camera.mode == 2;" in HELPERS, (
    "the third-person crosshair must be active whenever the camera is third person"
)
assert "scaleX = displayMode" not in HELPERS, (
    "TransContext_Project already returns display pixels; rescaling it misaligned the crosshair"
)
assert "IGI::OTHER::DRAW_SPRITE_Z" in HELPERS and "kStatusScreenCrossSpriteOffset = 0x74" in HELPERS, (
    "the internal menu host draws the retail STATUSSCREEN:cross sprite at 0x74"
)
assert "thirdPersonCrosshairActive" in HELPERS and "thirdPersonCrosshairActive" in HEAVEN, (
    "the demo must report when its crosshair is active"
)
assert "drawCrosshairOverlay" in IMGUI_HOST and "crosshairActive(runtime)" in IMGUI_HOST, (
    "the ImGui host must draw the crosshair through its validated D3D7 renderer"
)
# Free camera: retail camera-override path with WASD flight and a clean disable.
assert '"player_teleport_target"' in HEAVEN and '"landmark_target"' in HEAVEN, (
    "Heaven must offer the live-target and landmark teleport pickers"
)
assert not ({"player_send_x", "player_send_y", "player_send_z", "player_send_apply"} & set(ids)), (
    "the typed Custom Position boxes must stay out; Send Player To Target and the "
    "landmark picker are the send-player paths"
)
assert "teleportPlayerToTarget" in HELPERS and "teleportPlayerToPosition" in HELPERS, (
    "both teleport paths must be implemented in the helpers"
)
assert "IGI::HUMAN::SET_POSITION" in HELPERS and "snapHumanToGround" in HELPERS, (
    "teleporting sets the position and grounds the player"
)
assert '"player_send_position"' in HEAVEN and 'id == "player_send_position"' in DISPATCHER, (
    "the Send Player Position control must reach the retail SEND_POSITION path"
)
assert "IGI::PLAYER::SEND_POSITION" in HELPERS, (
    "teleporting must notify the player state machine after moving the body"
)
assert '"debug_keys"' in HEAVEN and '"Debug Keys / Fly Mode"' in HEAVEN, (
    "Heaven must expose the retail debug keys / fly mode toggle"
)
assert "LCtrl+LShift+" in HELPERS, (
    "the debug keys control must document the retail key chord"
)
assert '"freecam"' in HEAVEN and '"Free Camera (WASD)"' in HEAVEN, (
    "Heaven must expose the free camera in both frontends"
)
assert "setFreecamEnabled" in HELPERS and "updateFreecam" in HELPERS, (
    "the free camera needs an enable path and a per-frame update"
)
# The free camera uses the retail camera override (HumanView_SetCamera), which
# the renderer honours, instead of writing the player body state machine.
assert "IGI::CAMERA::SET_CAMERA" in HELPERS and "IGI::CAMERA::CLEAR_CAM_OVERRIDE" in HELPERS, (
    "the free camera must drive and clear the retail camera override"
)
assert "IGI::CAMERA::SET_EULER" in HELPERS and "GetAsyncKeyState" in HELPERS, (
    "the free camera builds its own pose from the WASD keys"
)
for key in ("down('W')", "down('A')", "down('S')", "down('D')", "down(VK_SPACE)",
            "down(VK_CONTROL)"):
    assert key in HELPERS, f"the free camera must poll {key}"
assert "BODY_FLY" not in HELPERS and "kBodyStateFly" not in HELPERS, (
    "the crashing BODY_FLY path must not come back"
)
assert 'id == "freecam"' in DISPATCHER, "the free camera control must have a dispatch branch"
assert "frameUpdate" in HEAVEN and "frameUpdate" in INTERNAL_HOST and "frameUpdate" in IMGUI_HOST, (
    "both hosts must run the per-frame mod update"
)
assert "IGI::WEAPON::DROP" in HELPERS and "WeaponHelpers::dropCurrentWeapon" in HELPERS, (
    "drop weapon must use the retail Weapon_Drop lifecycle"
)
# Dropping must also clear the carried slot and stop the draw animation, or the
# weapon can still be cycled back to.
assert "kHumanWeaponSlotsOffset = 0x4C4" in HELPERS, (
    "drop must clear Human.weapons[] at the recovered 0x4C4 offset"
)
assert "IGI::WEAPON::SLOT_SET(slots, slot, nullptr)" in HELPERS, (
    "drop must clear the carried weapon slot"
)
assert "IGI::HUMAN::STOP_WEAPON_ANIM(player, 0.0F)" in HELPERS, (
    "drop must stop the weapon animation"
)
# The retail death path deletes every weapon because the human is dying. Doing
# that to a live player left it unarmed and crashed the next gameplay frame.
assert "IGI::WEAPON::DELETE_ALL(player)" not in HELPERS, (
    "dropping a live player's weapon must not delete the rest of the inventory"
)
assert "findAnyOtherGun" in HELPERS and "WEAPON_ID_GLOCK" in HELPERS, (
    "drop must keep another carried weapon or equip the retail sidearm"
)
# Ammo must apply to the weapon in hand, not to a menu selection.
assert "WeaponHelpers::refillCurrentWeapon" in HELPERS, (
    "the ammo control must refill the held weapon"
)
assert "kGunIdOffset = 0x100" in HELPERS and "PICK_UP_WEAPON(player, message)" in HELPERS, (
    "the held weapon is refilled through its retail gun id"
)
assert 'id == "weapon_ammo"' in HELPERS, "the ammo control must have a dispatch branch"
# Car_Control input[1] is a target speed in retail speed units, not a 0..1
# throttle: the car AI cruises at 5 and up, and negative drives in reverse.
assert '{"Stop", "Reverse", "Walk", "Run", "Full", "Brake"}' in HEAVEN, (
    "car drive must expose the retail walk/run/reverse/stop control set"
)
for speed in ("targetSpeed = -6.0F", "targetSpeed = 5.0F", "targetSpeed = 15.0F",
              "targetSpeed = 30.0F"):
    assert speed in HELPERS, f"car drive must send a retail speed value ({speed})"
assert "Quarter" not in HELPERS and "0.25F" not in HELPERS.split("driveCar")[1][:1200], (
    "car drive must not send fractional throttles; retail expects a speed"
)
assert 'WeaponEntry{"Medipack", "WEAPON_ID_MEDIPACK", 50}' in HEAVEN, (
    "medipack pickup must carry the configured 50-charge bundle"
)

# The retail menu cannot draw more rows than its frame holds, so a larger control
# set must switch to the scrollable command list or options become unreachable.
# Large menus keep the rich row layout and are paged across MenuScreens, so no
# control collapses into a one-line selector.
SCRIPT = (ROOT / "src/ui/menu_script.cpp").read_text(encoding="utf-8")
assert "menuPageCount(config)" in SCRIPT and "kMenuRowsPerPage" in SCRIPT, (
    "the generator must page large control sets instead of drawing a single line"
)
assert "Next Page >" in SCRIPT and "IGI_SDK_Page(" in SCRIPT, (
    "page navigation must be a registered SDK symbol so the host can switch pages"
)
# MenuManager_AddItems stops at a selectable MenuWindow, so a button nested in a
# selectable frame is never clickable. Navigation buttons must be their own rows
# and the button bar must not be selectable.
assert "\"PageFrame\"" not in SCRIPT, (
    "page navigation must not be nested in a selectable frame"
)
assert "\"page_next\", \"Next Page >\"" in SCRIPT or "page_next" in SCRIPT
# Control rows must stack vertically: every row gets its own Y offset, or all
# rows draw at the frame top and the menu looks like a single line.
assert "const int y = rowY(index - firstRow);" in SCRIPT and "rowY(rowsOnPage)" in SCRIPT, (
    "control rows must be positioned per row, not all at the frame top"
)
assert "kBodyStateFly" not in HELPERS and "smGo" not in HELPERS, (
    "the free camera must not mutate the player body state machine"
)
assert "free camera enabled" in HELPERS and "free camera disabled" in HELPERS, (
    "the free camera must report both state changes"
)
assert "(void)setFreecamEnabled(runtime, false);" in HELPERS and (
    "g_thirdPersonCrosshairEnabled.store(false" in HELPERS
), "level change must exit fly mode and the crosshair before the Game task dies"
assert 'ControlsFrame' in SCRIPT and '20, 4, FALSE, "' in SCRIPT, (
    "the Restart/Okay/Cancel bar must not be selectable so its buttons are items"
)
assert "pageScreen(activePage)" in SCRIPT, (
    "only the active page may be emitted, so no page screen is ever orphaned"
)
assert "MenuManager_RequestScreen(\" << activeScreenId" in SCRIPT, (
    "the generated script must request the emitted page, not page 1"
)
# The host must recognise every page screen and pop the whole pushed range.
assert "igi::menuPageCount(menuConfig)" in INTERNAL_HOST, (
    "the retail host must know how many page screens belong to the menu"
)
assert "closeCommandScript" in INTERNAL_HOST and "MenuManager_PopScreen(FALSE);" in INTERNAL_HOST, (
    "page replacement still uses the retail screen command path"
)
assert "leaveCommandScript" in INTERNAL_HOST and "MenuManager_LeaveMenus(900, 9)" in INTERNAL_HOST, (
    "the final close must resume through the retail LeaveMenus path, not pop an empty stack"
)
# A fresh attach must delete the previous page screens before building the new
# one, or the base id migrates while old page scripts persist and the retail
# manager wedges on a screen id that no longer exists.
assert "menuScreenCreated = false;" in INTERNAL_HOST and (
    "releaseInjectedMenuScreen();" in INTERNAL_HOST
), "a completed close must delete the screen task and clear the created flag"
assert INTERNAL_HOST.count("releaseInjectedMenuScreen()") >= 4, (
    "reopen and the missed-deadline recovery must clear old page screens first"
)
assert "id range freed by removing the previous page screens" in INTERNAL_HOST, (
    "the id search must clear our own leftovers before relocating the base"
)

# Level, world-time and mission-time controls must use the validated paths.
for item_id in ("world_time_preset", "world_time_hour", "mission_time_seconds",
                "mission_time_preset", "mission_time_limit", "landmark_target",
                "player_send_target"):
    assert item_id in dispatched, f"{item_id} lost its helper dispatch branch"
assert "MISSION::FIND" in HELPERS and "requestLevelChange" in HELPERS, (
    "a level change must validate the id against the retail mission table first"
)
assert "processPendingLevelChange" in HEAVEN and "processPendingLevelChange" in HELPERS, (
    "the level change must run after the frame is flushed, never inside the draw pass"
)
assert "setWorldHour" in HELPERS and "setMissionElapsedSeconds" in HELPERS, (
    "world time and mission time are independent LevelFlow fields"
)
assert "broadcastPlayerPosition" in HELPERS and "IGI::HUMAN::SEND_POSITION(player)" in HELPERS, (
    "Send Player Position must publish both retail position broadcasts"
)
assert "worldObjectTargets" in HELPERS and "authoredLevelTasks" in HELPERS, (
    "the object picker must walk the authored level tree, not only the event task lists"
)
assert "refillWeapon" in HELPERS and "findOwnedGun" in HELPERS, (
    "ammo must be applied to the selected weapon task, including newly created weapons"
)
assert "requestModExit" in HELPERS and "IGI_SDK_RequestSafeUnload" in HELPERS, (
    "Exit Mod must request the host safe-unload path"
)
assert "exit_mod" in dispatched and "RequestExit" in HEAVEN, (
    "Exit Mod must be a routed action in the shared Heaven menu"
)
assert "Custom Position" not in HEAVEN and "Send Player To Custom XYZ" not in HEAVEN, (
    "the typed Custom Position boxes were removed; Send Player To Target and the "
    "landmark picker are the send-player paths"
)
assert "completeExternalShutdownOnGameThread" in IMGUI_HOST, (
    "the external host must finish an eject from the end-frame path even if the scheduler task is late"
)
assert "shutdownPending.load(std::memory_order_acquire)) {" in IMGUI_HOST, (
    "the external end-frame callback must drive the pending eject"
)
assert "unloadRequested.store(true" in WINDOWS_DLL, (
    "the configured end key must eject instead of only disabling the menu"
)
# Closing must not mistake the retail menu's fade-out-to-idle for an open pause
# menu; doing so left human input locked after the mod menu closed.
assert "nextState" in INTERNAL_HOST and "retailMenuShown" in INTERNAL_HOST, (
    "the host must distinguish an open retail menu from a fade-out to gameplay"
)
assert "kMenuManagerFadeOutState" in INTERNAL_HOST, (
    "the close path must recognise the retail menu resume transition"
)
# Eject must request cleanup once; re-requesting every poll queued the
# game-thread cleanup twice and double-freed the menu resources.
assert "shutdownCompleted.exchange" in INTERNAL_HOST, (
    "game-thread menu cleanup must be idempotent"
)
assert "shutdownPending.exchange(true" in INTERNAL_HOST, (
    "internal RequestShutdown must be idempotent"
)
assert "shutdownRequested" in WINDOWS_DLL, (
    "the host must not re-queue requestShutdown while waiting for canUnload"
)
# LevelRestart destroys the LevelFlow/MenuManager and invalidates the level QVM
# resources. The restart button must detach first, or the next F5 runs a stale
# resource (frames with no options) and eject cannot restore the retail menu.
assert "IGI_SDK_Restart" in SCRIPT, (
    "the Restart button must use the host restart action, not LeaveMenus(900, 12)"
)
assert "restartPending" in INTERNAL_HOST and "hostRestartPending" in INTERNAL_HOST, (
    "the restart must be deferred past the menu script and the draw pass"
)
assert "releaseScriptSession" in INTERNAL_HOST and "lastMenuManager" in INTERNAL_HOST, (
    "a close or level/menu-manager change must release the stale QVM session"
)
assert "requestMissionRestart" in HELPERS.split("WorldHelpers::restartMission", 1)[1][:600], (
    "the demo restart action must queue the restart instead of deleting its own screen"
)
assert "processMissionRestart" in HELPERS and "prepareMenuForLevelChange()" in HELPERS.split(
    "bool processMissionRestart", 1
)[1][:1600], (
    "the frame tick must detach the injected menu before GAME_RESTART"
)
assert "camera.mode == 2" in HELPERS and "IGI::CAMERA::ROTATE_X" in HELPERS, (
    "the free camera must build the retail camera axes and apply them at the viewport"
)
# Pause-menu restoration: the retail screen must be captured before the mod
# screen replaces it and survive page-switch resource rebuilds.
assert "returnScreenId" in INTERNAL_HOST and (
    "returnScreenId > 0" in INTERNAL_HOST
), "closing the mod menu must return to the retail screen that was open before it"
assert "returnScreenId = current;" in INTERNAL_HOST, (
    "the attach path must snapshot the pre-existing retail screen id"
)
# Start-key requests must serialize; two queued toggles used to open and close.
assert "toggleRequestPending" in INTERNAL_HOST and "toggleRequestPending" in IMGUI_HOST, (
    "both hosts must reject a duplicate queued start-key toggle"
)
# Free camera W/S follows the camera pitch, not only the yaw plane.
assert "sinPitch" in HELPERS and "moveZ += sinPitch" in HELPERS, (
    "free camera forward/back must follow the full aim direction"
)
# A failed ground probe must restore the player rather than leave it airborne.
assert "unchanged(grounded, probe)" in HELPERS and (
    "player left at " in HELPERS
), "a teleport with no floor must be rejected and the body restored"
assert "kLevelStartTargetId" in HELPERS and "captureLevelStartPosition" in HELPERS, (
    "every level must expose the cached Level Start teleport target"
)
assert "captureLevelStartPosition" in HEAVEN, (
    "the Heaven frame tick must capture the per-level start position"
)
print(
    f"PASS Heaven routes {len(ids)} production controls across {len(categories)} categories"
)
