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

categories = {"Weapons", "Player", "Camera", "World", "Vehicles", "AI / NPC"}
for category in categories:
    assert category in HEAVEN, f"missing Heaven category: {category}"

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
    "vehicle_reset": "VehicleHelpers(runtime).resetControls",
    "vehicle_explode": "VehicleHelpers(runtime).explodeCar",
    "npc_ensure_weapon": "NpcHelpers(runtime).ensureWeapon",
    "restart_mission": "WorldHelpers(runtime).restartMission",
}
for item_id, native_path in required_native_paths.items():
    source = HEAVEN if item_id == "weapon_pickup" else HELPERS
    assert native_path in source, f"{item_id} lacks its verified helper path"

print(f"PASS Heaven routes {len(ids)} production controls across {len(categories)} categories")
