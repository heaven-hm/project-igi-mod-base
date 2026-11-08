# Project I.G.I. real mod menus

`demos/heaven/` is the production gameplay menu: `igi_heaven_mod_menu` builds the retail-QVM internal menu and `igi_heaven_mod_menu_external` builds the Direct3D 7 ImGui menu. Both are Windows x86 DLLs and share the same native-backed actions. Build outputs remain in `build-win32-release/`; repository `binaries/` is intentionally unused.

## Public helper API

All public helpers live in `demos/helpers/menu_helpers.hpp`, with their only implementation in `demos/helpers/menu_helpers.cpp`. Construct a helper with the `igi::Runtime` passed to a game-frame/menu callback. A helper returns `false` or `std::nullopt` when it is not executing on the live game callback thread, the game is inactive, or a required retail object is unavailable. Do not call these methods from a DLL worker thread.

### EntityHelpers

`moveHuman(human, position)` uses the engine position-update path for a supplied live player or NPC, so collision placement, view position, slide state, and push state update together. `setHumanYaw` updates the human rotation and animation bones through the game native. `snapHumanToGround` and `leaveHumanGround` attach or detach a live human from ground handling, while `setHumanCollision(human, enabled)` changes that human's retail collision state. Obtain the human from a real game callback, such as `firstNpc`, and keep the position array in game coordinates.

### PlayerHelpers

`moveScale`, `state`, `team`, `index`, `isDead`, `isCrouching`, `isActive`, and `isPlayer` query the local retail player through the corresponding `IGI::PLAYER` and `IGI::HUMAN` natives. Use their optional result to populate a HUD/status view; do not treat an empty result as a game value.

`setInputEnabled` and `setLookEnabled` lock or unlock the local human’s retail controls. Use them for a temporary mod state and always restore both controls before closing that state.

`startAction`, `checkDeath`, and `sendPosition` dispatch the matching player-controller operation. Use only values and timings already validated by the game’s own action data; these methods deliberately do not invent action IDs.

`restoreVisualFeedback` removes the retail hit/flash overlays. `setFlagD44`, `clearFlagD44`, `setFlagD68`, and `clearFlagD68` expose the named player-controller flag natives for an advanced game-thread mod that has a verified reason to change them.

`resetDamage` calls retail `Human_ResetDamage` on the live local player. It clears the current damage total and restores the configured multipliers for all damage zones; Heaven exposes it as a recovery action only while a live player exists.

`snapToGround` uses the game’s ground-probe and position flow for the local player. `setCollisionEnabled` changes the local player’s collision state through the corresponding game native; Heaven provides both options as immediate gameplay controls.

### WeaponHelpers

`pickup(weaponId, ammo)` resolves a retail `WEAPON_ID_*` symbol and gives it to the local player; `enableUnlimitedAmmo()` calls the retail unlimited-ammo behavior; `cycle(force)`, `selectCategory()`, `setAnimation(animation)`, and `requestAnimation(animation)` invoke the player’s verified weapon controls. Pass `1` to `cycle` for a direct forced next-weapon selection; pass `0` only from the game input update path, where it processes the next/previous key edges. Use IDs from retail data rather than guessed constants.

`hasAmmo(task)` and `type(task)` inspect an existing retail weapon task. `fireEvent`, `typeEvent`, `triggerEvent`, `stateEvent`, and `moveEvent` return the game’s weapon event identifiers, for use only by code already handling the matching weapon task/event path.

### CameraHelpers

`setViewMode(mode)` calls the retail `Human_ViewCam` setter for the local player and accepts mode `1` for first person or `2` for third person; the Heaven menu exposes both. `mode()` obtains the effective mode through the retail camera-info handler, including built-in camera toggles and overrides, and `isThirdPersonActive()` returns true only when that handler reports mode `2`; both return an empty result if there is no live player.

`setWeaponTarget`, `setWeaponAngles`, `setCamera`, `setTarget`, `setDip`, and `setZoomed` manipulate a supplied live `IGI::HumanView`. `eye`, `forward`, and `matrix` read that same live view into caller-owned storage. Obtain the view from a verified game callback/owner before use; these methods never manufacture pointers.

The old third-person crosshair API is deliberately absent. It had a helper-owned atomic flag rather than a retail state query, so it could claim a mode that the game had not selected. It will return only after the retail draw predicate and sprite ownership are independently recovered and live-tested on Windows x86.

### WorldHelpers

`changeLevel(levelId)` selects retail missions 1 through 14 through the menu/game task lifecycle, `restartMission()` invokes the retail current-mission restart path, and `setWeather("Off" | "Rain" | "Snow")` creates or removes the retail `RainEffect` task. Use these as game-thread menu actions; level changes clean up the injected menu before a level teardown.

`setPaused`, `isPaused`, `refreshDisplay`, and `setFrameCap` operate on verified game/display natives. `event`, `flowFps`, `flowFrames`, `flowRenders`, `taskCount`, and `logFlowDiagnostics` provide current game counters for a real mod status page.

### VehicleHelpers

`EntityHelpers::firstHuman`, `nextHuman`, `firstCar`, and `nextCar` resolve live retail task objects by their registered task type; a null result means the active level has none. `carControlInfo`, `carFrame`, `carLight`, `carMountedGun`, `carGunnerPose`, and `carTrackFactor` read a supplied live `IGI::Car`; `resetCar` invokes the car’s own reset branch through its control native, while `resetControls` and `explodeCar` perform their corresponding vehicle operations. `heliDoor`, `heliControlInfo`, `heliStateSize`, `heliConfigSize`, and `heliSoundInfo` operate on supplied live helicopter/task objects. Heaven uses `firstCar` for its visible vehicle actions and reports a failure instead of manufacturing a target when the level has no car.

### AiHelpers

`NpcHelpers::ensureWeapon` invokes the retail soldier weapon-creation path for a supplied non-player human. Heaven resolves the first real NPC before it calls this method and rejects the action if the level has none. `AiHelpers` forwards the verified retail AI script natives: alarm-action removal, view/alarm range and alpha/gamma setters, event priority, invulnerability, instant death, death animation, alarm trigger/control/access, gunner ID, animation interval, current event type, and random value. Each method takes an `AiScriptContext` supplied by a real IGI script callback (`out`, `data`, and `args`); it is not valid to fabricate this context from a menu button.

`NpcHelpers::resetDamage` applies that same retail damage reset to a supplied live NPC. It deliberately requires the caller to resolve a real non-player human first; Heaven uses `firstNpc` and refuses the action when there is no valid target.

### AudioHelpers and menu adapter

`setMusicVolume` and `setSfxVolume` clamp a user volume to `0.0F..1.0F` and call the retail audio natives. `executeMenuNativeAction(config, runtime, item, value)` is the single adapter used by the internal and external menus; it routes only configured menu actions to these helpers.

## Safe unload

Never force-eject a loaded mod DLL. Build `igi_injector` and request a coordinated unload with `IGI-Injector.exe -p <igi-pid> -i <absolute-dll-path> -e`. The DLL first removes its game-thread hooks, menu callbacks, and ImGui/QVM resources; only its own host thread then calls `FreeLibraryAndExitThread`. `run-igi-wine.sh` supports the same path with `IGI_SAFE_EJECT=true`.

## Windows x86 verification

Build and run the Windows unit executable under Wine without launching the game: `WINEPREFIX=<prefix> WINEDEBUG=-all <wine> build-win32-release/igi_sdk_tests.exe`. Then use the retail executable only through the Unicorn harness, which emulates the DLL/frame callback rather than injecting it: `IGI_EXE=<retail-igi.exe> IGI_DLL=build-win32-release/igi-heaven-mod-internal.dll python3 tools/igi_runtime_harness.py --frames 5 --lenient`. Run `--verify-menu-helpers` as a separate harness invocation to validate the weather and level-transition native sequence.
