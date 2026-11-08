#!/usr/bin/env python3
"""Keep every Heaven production-menu control connected to a real helper path."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
HEAVEN = (ROOT / "demos/heaven/heaven_menu.cpp").read_text(encoding="utf-8")
HELPERS = (ROOT / "demos/helpers/menu_helpers.cpp").read_text(encoding="utf-8")

ids = re.findall(r'\{"([a-z_]+)",\s*igi::ModMenuItemKind::', HEAVEN)
assert ids, "Heaven menu has no configured items"
assert len(ids) == len(set(ids)), f"duplicate Heaven item IDs: {ids}"
assert "input" not in ids and "look" not in ids, (
    "Heaven must keep its Player category focused on gameplay-impacting controls"
)

categories = {"Weapons", "Player", "Camera", "World", "Vehicles", "AI / NPC"}
for category in categories:
    assert category in HEAVEN, f"missing Heaven category: {category}"

assigned_categories = {
    int(index): category
    for index, category in re.findall(
        r'config\.items\[(\d+)\]\.category\s*=\s*"([^"]+)"', HEAVEN
    )
}
assert set(assigned_categories) == set(range(len(ids))), (
    "every Heaven control must have exactly one category; "
    f"got indexes {sorted(assigned_categories)} for {len(ids)} items"
)
assert set(assigned_categories.values()) == categories, (
    "Heaven categories must exactly match the production category set; "
    f"got {set(assigned_categories.values())}"
)

assert "const int mode = active ? 2 : 1;" in HELPERS, (
    "the retail camera path uses mode 1 for first person and mode 2 for third person"
)
assert "IGI::HUMAN::GET_CAMERA_INFO" in HELPERS, (
    "camera state must come from the retail camera-info handler, not a helper-owned cache"
)
assert '"void Human_SnapToGround(Human *h)"' in HELPERS, (
    "ground recovery must refuse an unresolved native instead of reporting a false success"
)
assert '"void Human_EnableCollision(Human *h)"' in HELPERS, (
    "collision controls must refuse an unresolved native instead of reporting a false success"
)

# weapon_pickup is intentionally resolved by the Heaven adapter so it can bind
# the selected retail weapon symbol and ammo value together.
special_routes = {"weapon_pickup"}
for item_id in ids:
    if item_id in special_routes:
        assert f'config.items[item].id == "{item_id}"' in HEAVEN
        continue
    assert f'id == "{item_id}"' in HELPERS, f"unrouted Heaven item: {item_id}"

required_native_paths = {
    "weapon_pickup": "WeaponHelpers(runtime).pickup",
    "weapon_cycle": "WeaponHelpers(runtime).cycle(1)",
    "vehicle_reset": "VehicleHelpers(runtime).resetCar",
    "vehicle_explode": "VehicleHelpers(runtime).explodeCar",
    "vehicle_drive": "VehicleHelpers(runtime).driveCar",
    "npc_execute": "NpcHelpers(runtime).executeSoldier",
    "npc_ensure_weapon": "NpcHelpers(runtime).ensureWeapon",
    "npc_snap_ground": "NpcHelpers(runtime).snapToGround",
    "npc_collision": "NpcHelpers(runtime).setCollisionEnabled",
    "player_reset_damage": "PlayerHelpers(runtime).resetDamage",
    "player_snap_ground": "PlayerHelpers(runtime).snapToGround",
    "player_collision": "PlayerHelpers(runtime).setCollisionEnabled",
    "npc_reset_damage": "NpcHelpers(runtime).resetDamage",
    "restart_mission": "WorldHelpers(runtime).restartMission",
}
for item_id, native_path in required_native_paths.items():
    source = HEAVEN if item_id == "weapon_pickup" else HELPERS
    assert native_path in source, f"{item_id} lacks its verified helper path"

assert '"Live Car Target"' in HEAVEN and '"Waiting for live car list"' in HEAVEN, (
    "Heaven must render names from the active car list rather than ordinal slots"
)
assert '"Live NPC Target"' in HEAVEN and '"Waiting for live NPC list"' in HEAVEN, (
    "Heaven must render names from the active NPC list rather than ordinal slots"
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
assert 'clearLiveTargetSelections("level change unloads the current level task list")' in HELPERS, (
    "a level change must drop stale NPC/car selections instead of logging fake warnings"
)
assert (
    'clearLiveTargetSelections("mission restart rebuilds the level task list")' in HELPERS
), "a mission restart must drop stale NPC/car selections too"
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

assert 'WeaponEntry{"Binoculars", "WEAPON_ID_BINOCULARS", 0}' in HEAVEN, (
    "binocular pickup must use its retail symbol with no invented ammo count"
)
assert 'WeaponEntry{"Medipack", "WEAPON_ID_MEDIPACK", 50}' in HEAVEN, (
    "medipack pickup must carry the configured 50-charge bundle"
)

print(f"PASS Heaven routes {len(ids)} production controls across {len(categories)} categories")
