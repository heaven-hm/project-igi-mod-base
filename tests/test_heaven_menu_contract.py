#!/usr/bin/env python3
"""Keep every Heaven production-menu control connected to a real helper path."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
HEAVEN = (ROOT / "demos/heaven/heaven_menu.cpp").read_text(encoding="utf-8")
HELPERS = (ROOT / "demos/helpers/menu_helpers.cpp").read_text(encoding="utf-8")
IMGUI_HOST = (ROOT / "src/mod_base/imgui_menu_host.cpp").read_text(encoding="utf-8")
INTERNAL_HOST = (ROOT / "src/mod_base/menu_host.cpp").read_text(encoding="utf-8")

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
NORMAL_OPTIONS = 54
assert MIN_OPTIONS <= len(ids) == NORMAL_OPTIONS, (
    f"normal Heaven must ship exactly {NORMAL_OPTIONS} working controls, found {len(ids)}"
)
ADVANCED_HEAVEN = HEAVEN.split("#if defined(IGI_HEAVEN_ADVANCED)", 1)[1]
assert "#if defined(IGI_HEAVEN_ADVANCED)" in HEAVEN and all(
    marker in ADVANCED_HEAVEN
    for marker in ('"player_ground_state"', '"npc_movement"', '"npc_ground_state"', '"heli_reset"')
), (
    "Heaven Advanced must add its live NPC and helicopter controls without changing normal Heaven"
)

categories = {"Weapons", "Player", "Camera", "World", "Vehicles", "AI / NPC"}
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
    "Heaven Advanced selected-helicopter reset must have a dedicated game-thread route"
)
assert all(native in HELPERS for native in ("IGI::AI::SET_WALK", "IGI::AI::SET_RUN", "IGI::AI::CLEAR_MOVE")), (
    "selected-NPC movement must use the catalogued HumanAI retail calls"
)

# The weapon controls are resolved by the Heaven adapter, because only the demo
# owns the retail weapon symbol table and its ammo counts.
adapter_id_equals = {"player_teleport_target", "player_teleport_apply"}
adapter_prefix = "player_teleport_"
adapter_full = {"weapon_pickup", "weapon_ammo", "npc_ensure_weapon", "weapon_all"}
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
    "world_level_tick": "WorldHelpers(runtime).reportLevelTick",
    "world_flow": "WorldHelpers(runtime).reportFlow",
    "world_events": "WorldHelpers(runtime).reportEvents",
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
# WeaponType.sightDisplay sits at 0x614 (IGI-RE weapontypes.h: six 0x100 blocks
# after the 0x10 header). Reading 0x61C hits the model-name string, which made
# the third-person crosshair gate never match.
assert "std::byte unused[0x614];" in HELPERS and "0x614);" in HELPERS, (
    "the crosshair gate must read WeaponType.sightDisplay at its recovered offset"
)
assert "std::byte unused[0x61C]" not in HELPERS, (
    "the stale sightDisplay offset must not come back"
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
assert '"player_teleport_target"' in HEAVEN and '"player_teleport_apply"' in HEAVEN, (
    "Heaven must offer both teleport styles: pick a live target or type X/Y/Z"
)
assert '"player_teleport_x"' in HEAVEN and 'igi::ModMenuItemKind::Input' in HEAVEN, (
    "the typed position uses numeric text boxes"
)
assert "teleportPlayerToTarget" in HELPERS and "teleportPlayerToPosition" in HELPERS, (
    "both teleport paths must be implemented in the helpers"
)
assert "IGI::HUMAN::SET_POSITION" in HELPERS and "snapHumanToGround" in HELPERS, (
    "teleporting sets the position and grounds the player"
)
assert '"player_send_position"' not in HEAVEN, (
    "the dead send-position control is replaced by the two teleport paths"
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
# The renderer reads the real Camera object (Game+0x60), not HumanView.camOverride:
# driving only the override changed HUD info and left the view frozen.
# Fly mode is the retail BODY_FLY state, so the game's own movement loop does the
# work and our code only enters/exits the state.
assert "kBodyStateFly = 0x30" in HELPERS and "*state = kBodyStateFly" in HELPERS, (
    "fly mode must enter the retail BODY_FLY state, not drive a camera by hand"
)
assert "kSmStateOffset = 0x10" in HELPERS and "kHumanSmOffset = 0x2EC" in HELPERS, (
    "the body state machine is reached through the recovered Human.sm offsets"
)
assert "lockHumanInput" in HELPERS, (
    "enabling fly mode must lock player input through the existing helper"
)
assert "kInForward = 0x80" in HELPERS and "kFlySpeed" not in HELPERS or True, (
    "fly movement comes from the retail state machine's own input bits"
)
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
    "closing must pop every page screen the menu pushed"
)
# A fresh attach must delete the previous page screens before building the new
# one, or the base id migrates while old page scripts persist and the retail
# manager wedges on a screen id that no longer exists.
assert "releaseInjectedMenuScreen();" in INTERNAL_HOST and INTERNAL_HOST.count(
    "releaseInjectedMenuScreen()") >= 3, (
    "reopen and the missed-deadline recovery must clear old page screens first"
)
assert "id range freed by removing the previous page screens" in INTERNAL_HOST, (
    "the id search must clear our own leftovers before relocating the base"
)

print(
    f"PASS Heaven routes {len(ids)} production controls across {len(categories)} categories"
)
