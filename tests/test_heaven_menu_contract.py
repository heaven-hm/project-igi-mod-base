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
    "npc_execute": "NpcHelpers(runtime).executeSoldier",
    "npc_ensure_weapon": "NpcHelpers(runtime).ensureWeapon",
    "player_reset_damage": "PlayerHelpers(runtime).resetDamage",
    "player_snap_ground": "PlayerHelpers(runtime).snapToGround",
    "player_collision": "PlayerHelpers(runtime).setCollisionEnabled",
    "npc_reset_damage": "NpcHelpers(runtime).resetDamage",
    "restart_mission": "WorldHelpers(runtime).restartMission",
}
for item_id, native_path in required_native_paths.items():
    source = HEAVEN if item_id == "weapon_pickup" else HELPERS
    assert native_path in source, f"{item_id} lacks its verified helper path"

assert '"Car Target"' in HEAVEN and '"Car 8"' in HEAVEN, (
    "Heaven must retain its selectable car target slots"
)
assert '"NPC Target"' in HEAVEN and '"NPC 8"' in HEAVEN, (
    "Heaven must retain its selectable NPC target slots"
)
assert "EntityHelpers(runtime).carAt(g_selectedCarSlot)" in HELPERS, (
    "vehicle actions must resolve the selected live car at action time"
)
assert "EntityHelpers(runtime).npcAt(g_selectedNpcSlot)" in HELPERS, (
    "NPC actions must resolve the selected live NPC at action time"
)

assert 'WeaponEntry{"Binoculars", "WEAPON_ID_BINOCULARS", 0}' in HEAVEN, (
    "binocular pickup must use its retail symbol with no invented ammo count"
)
assert 'WeaponEntry{"Medipack", "WEAPON_ID_MEDIPACK", 50}' in HEAVEN, (
    "medipack pickup must carry the configured 50-charge bundle"
)

print(f"PASS Heaven routes {len(ids)} production controls across {len(categories)} categories")
