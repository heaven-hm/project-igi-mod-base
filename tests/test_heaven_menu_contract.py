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
assert defs, "Heaven menu has no configured items"
ids = [entry[0] for entry in defs]
assert len(ids) == len(set(ids)), f"duplicate Heaven item IDs: {ids}"

MIN_OPTIONS = 30
NORMAL_OPTIONS = 50
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
special_routes = {"weapon_pickup", "weapon_ammo", "weapon_refill", "weapon_refill_all",
                  "weapon_all", "npc_ensure_weapon"}
for item_id in ids:
    if item_id in special_routes:
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
    "npc_status": "NpcHelpers(runtime).reportStatus",
    "npc_params": "NpcHelpers(runtime).reportSoldierParams",
    "npc_stand_down": "NpcHelpers(runtime).standDownSoldier",
    "player_reset_damage": "PlayerHelpers(runtime).resetDamage",
    "player_snap_ground": "PlayerHelpers(runtime).snapToGround",
    "player_collision": "PlayerHelpers(runtime).setCollisionEnabled",
    "player_status": "PlayerHelpers(runtime).reportStatus",
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
assert '"weapon_refill"' in HEAVEN and "Refill Selected Weapon" in HEAVEN, (
    "Heaven must apply the typed ammo to the selected weapon"
)
assert "g_refillAmmo" in HEAVEN and "g_selectedWeapon" in HEAVEN, (
    "the weapon refill path must keep the typed ammo and selected weapon"
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
assert "IGI::WEAPON::DROP" in HELPERS and "WeaponHelpers::dropCurrentWeapon" in HELPERS, (
    "drop weapon must use the retail Weapon_Drop lifecycle"
)
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
assert "menuNeedsCommandList(config)" in (
    ROOT / "src/ui/menu_script.cpp"
).read_text(encoding="utf-8"), (
    "the generator must switch to the command-list layout when controls outgrow the frame"
)

print(
    f"PASS Heaven routes {len(ids)} production controls across {len(categories)} categories"
)
