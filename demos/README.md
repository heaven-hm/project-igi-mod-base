# Project I.G.I. real mod menus

`demos/heaven/` is the production gameplay menu: `igi_heaven_mod_menu` builds the retail-QVM internal menu and `igi_heaven_mod_menu_external` builds the Direct3D 7 ImGui menu. Both are Windows x86 DLLs and share the same native-backed actions. Build outputs remain in `build-win32-release/`; repository `binaries/` is intentionally unused.

## Public helper API

All public helpers live in `demos/helpers/menu_helpers.hpp`, with their only implementation in `demos/helpers/menu_helpers.cpp`. Construct a helper with the `igi::Runtime` passed to a game-frame/menu callback. A helper returns `false` or `std::nullopt` when it is not executing on the live game callback thread, the game is inactive, or a required retail object is unavailable. Do not call these methods from a DLL worker thread.

### PlayerHelpers

`moveScale`, `state`, `team`, `index`, `isDead`, `isCrouching`, `isActive`, and `isPlayer` query the local retail player through the corresponding `IGI::PLAYER` and `IGI::HUMAN` natives. Use their optional result to populate a HUD/status view; do not treat an empty result as a game value.

`setInputEnabled` and `setLookEnabled` lock or unlock the local human’s retail controls. Use them for a temporary mod state and always restore both controls before closing that state.

`startAction`, `checkDeath`, and `sendPosition` dispatch the matching player-controller operation. Use only values and timings already validated by the game’s own action data; these methods deliberately do not invent action IDs.

`restoreVisualFeedback` removes the retail hit/flash overlays. `setFlagD44`, `clearFlagD44`, `setFlagD68`, and `clearFlagD68` expose the named player-controller flag natives for an advanced game-thread mod that has a verified reason to change them.

### WeaponHelpers

`pickup(weaponId, ammo)` resolves a retail `WEAPON_ID_*` symbol and gives it to the local player; `enableUnlimitedAmmo()` calls the retail unlimited-ammo behavior; `cycle(force)`, `selectCategory()`, `setAnimation(animation)`, and `requestAnimation(animation)` invoke the player’s verified weapon controls. Use these for visible weapon gameplay effects, with IDs from the retail data rather than guessed constants.

`hasAmmo(task)` and `type(task)` inspect an existing retail weapon task. `fireEvent`, `typeEvent`, `triggerEvent`, `stateEvent`, and `moveEvent` return the game’s weapon event identifiers, for use only by code already handling the matching weapon task/event path.

### CameraHelpers

`setViewMode(mode)` calls the retail `Human_ViewCam` setter for the local player; use the game’s camera mode values (the Heaven menu exposes first- and third-person). There is intentionally no `isThirdPersonActive()` helper: the retail catalog currently verifies a setter, not a safe state getter.

`setWeaponTarget`, `setWeaponAngles`, `setCamera`, `setTarget`, `setDip`, and `setZoomed` manipulate a supplied live `IGI::HumanView`. `eye`, `forward`, and `matrix` read that same live view into caller-owned storage. Obtain the view from a verified game callback/owner before use; these methods never manufacture pointers.

The old third-person crosshair API is deliberately absent. It had a helper-owned atomic flag rather than a retail state query, so it could claim a mode that the game had not selected. It will return only after the retail draw predicate and sprite ownership are independently recovered and live-tested on Windows x86.

### WorldHelpers

`changeLevel(levelId)` selects retail missions 1 through 14 through the menu/game task lifecycle, `restartMission()` invokes the retail current-mission restart path, and `setWeather("Off" | "Rain" | "Snow")` creates or removes the retail `RainEffect` task. Use these as game-thread menu actions; level changes clean up the injected menu before a level teardown.

`setPaused`, `isPaused`, `refreshDisplay`, and `setFrameCap` operate on verified game/display natives. `event`, `flowFps`, `flowFrames`, `flowRenders`, `taskCount`, and `logFlowDiagnostics` provide current game counters for a real mod status page.

### VehicleHelpers

`carControlInfo`, `carFrame`, `carLight`, `carMountedGun`, `carGunnerPose`, and `carTrackFactor` read data from a supplied live `IGI::Car`. `heliDoor`, `heliControlInfo`, `heliStateSize`, `heliConfigSize`, and `heliSoundInfo` do the corresponding operation for supplied live helicopter/task objects. These are public for vehicle mods that already obtain a vehicle from a verified retail task path; the Heaven menu does not fake a “nearest vehicle” selector.

### AiHelpers

`AiHelpers` forwards the verified retail AI script natives: view/alarm range and alpha/gamma setters, event priority, invulnerability, instant death, death animation, alarm trigger/control/access, current event type, and random value. Each method takes an `AiScriptContext` supplied by a real IGI script callback (`out`, `data`, and `args`); it is not valid to fabricate this context from a menu button.

### AudioHelpers and menu adapter

`setMusicVolume` and `setSfxVolume` clamp a user volume to `0.0F..1.0F` and call the retail audio natives. `executeMenuNativeAction(config, runtime, item, value)` is the single adapter used by the internal and external menus; it routes only configured menu actions to these helpers.

## Safe unload

Never force-eject a loaded mod DLL. Build `igi_injector` and request a coordinated unload with `IGI-Injector.exe -p <igi-pid> -i <absolute-dll-path> -e`. The DLL first removes its game-thread hooks, menu callbacks, and ImGui/QVM resources; only its own host thread then calls `FreeLibraryAndExitThread`. `run-igi-wine.sh` supports the same path with `IGI_SAFE_EJECT=true`.
