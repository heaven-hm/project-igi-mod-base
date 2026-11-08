# Demo menus

## Branch Architecture

- **`develop` branch**: Dedicated to the native in-game menu (`demos/internal/`), rendered by Project I.G.I.'s retail QVM `MenuManager` and game-thread runtime.
- **`feature/imgui-mod` branch**: Dedicated to the Dear ImGui external overlay mod menu (`demos/external/`), rendered via Direct3D 7 using [AndenDev/D3D7Imgui](https://github.com/AndenDev/D3D7Imgui).

## Internal Menus (`demos/internal/`)

The `internal/` directory contains basic, simple and advanced in-game menus. Their shared host lives in `src/mod_base/menu_host.cpp`. Build the Windows x86 targets:
- `igi_basic_mod_menu` -> `binaries/igi-basic-mod-internal.dll`
- `igi_simple_mod_menu` -> `binaries/igi-simple-mod-internal.dll`
- `igi_advanced_mod_menu` -> `binaries/igi-advanced-mod-internal.dll`

## External Menus (`demos/external/` on `feature/imgui-mod`)

On the `feature/imgui-mod` branch, `demos/external/` implements matching Dear ImGui Direct3D 7 mod menus sharing the same configuration and native action bindings. Build the Windows x86 targets:
- `igi_basic_mod_menu_external` -> `binaries/igi-basic-mod-external.dll`
- `igi_simple_mod_menu_external` -> `binaries/igi-simple-mod-external.dll`
- `igi_advanced_mod_menu_external` -> `binaries/igi-advanced-mod-external.dll`

## Public helper API

Public gameplay helpers are declared in `demos/helpers/menu_helpers.hpp` and
implemented only in `demos/helpers/menu_helpers.cpp`. Demo sources call this
API; they do not define their own gameplay-helper functions or write game
memory directly. Every helper must be called from the game's frame/menu
callback through the supplied `igi::Runtime`.

The stable public surface is:

- `executeMenuNativeAction(config, runtime, item, value)` adapts a configured
  generic menu item to the verified helper/native path.
- `changeLevel(runtime, levelId)` selects retail missions 1 through 14.
- `setWeather(runtime, "Off" | "Rain" | "Snow")` manages the retail
  `RainEffect` task.
- `setFps(runtime, "30" | "60" | "120")` sets the retail frame cap.
- `enableUnlimitedAmmo(runtime)` enables the game's one-way unlimited-ammo
  native.
- `pickupPlayerWeapon(runtime, weaponId, ammo)` resolves a retail
  `WEAPON_ID_*` symbol and gives the local player that weapon.
- `cyclePlayerWeapon(runtime, force)` and
  `requestPlayerWeaponAnimation(runtime, animationId)` issue the matching
  player-controller requests. `force` and `animationId` must come from
  verified weapon/mod data; the helpers do not invent values.
- `restorePlayerVisualFeedback(runtime)` clears the player's retail hit/flash
  overlays.
- `setPlayerInputEnabled(runtime, enabled)` and
  `setPlayerLookEnabled(runtime, enabled)` control the local human player.
- `setMusicVolume(runtime, volume)` and `setSfxVolume(runtime, volume)` clamp
  a slider value to `0.0F..1.0F` before calling the audio natives.
- `logFlowDiagnostics(runtime)` writes retail FPS/frame/render counters to the
  mod log.
- `logPlayerState(runtime)` logs and displays the local player's verified team
  and life state.
- `setPlayerThirdPersonView(runtime, active)` switches between retail camera
  modes 1 and 2. `isThirdPersonActive()` reports the last mode selected through
  this helper.
- `renderThirdPersonCrosshair()` is for the display render callback. It returns
  `true` only when it issued the verified retail status-screen sprite draw.

For example, a demo action should use helpers rather than inline native chains:

```cpp
if (item.id == "view") {
    (void)igi::setPlayerThirdPersonView(runtime, value == "Third Person");
} else if (item.id == "sfx") {
    const float volume = value == "Mute" ? 0.0F : 1.0F;
    (void)igi::setSfxVolume(runtime, volume);
}
```

`isThirdPersonActive()` intentionally tracks helper-owned camera transitions;
it does not read an undocumented camera-memory offset. The crosshair renderer
uses the retail status-screen sprite path observed in IGI 1, but live rendering
still requires a real Windows x86 game session; a DLL build or emulator alone
does not prove on-screen visibility.
