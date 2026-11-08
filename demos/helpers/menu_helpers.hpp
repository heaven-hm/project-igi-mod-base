#pragma once

#include "config/menu_config.hpp"
#include "runtime/runtime.hpp"

#include <cstddef>
#include <string_view>

namespace igi {
// Purpose: adapt a configured menu item to a verified helper/native operation.
// Requires: a live game-thread Runtime and an item from the active MenuConfig.
// Retail path: dispatches only the typed IGI:: wrappers selected by the item.
// Failure: invalid or unsupported items are logged and leave the game unchanged.
// Use: menu hosts call this instead of placing gameplay-native chains in demos.
void executeMenuNativeAction(const MenuConfig &config, Runtime &runtime, std::size_t item,
                             std::string_view value);

// Purpose: queue the retail flow transition to a numbered mission.
// Requires: a live game-thread Runtime and a mission id from 1 through 14.
// Retail path: runs Game_SetMission, requests flow state 4, then unloads Game.
// Failure: returns false for invalid missions or a failed cleanup/script request.
// Use: call from a scheduler callback rather than a background DLL thread.
bool changeLevel(Runtime &runtime, int levelId);

// Purpose: create, replace, or remove the retail rain/snow precipitation task.
// Requires: a live game-thread Runtime and "Off", "Rain", or "Snow" input.
// Retail path: uses the RainEffect task lifecycle and GAME::SET_RAINING.
// Failure: returns false if the level task or temporary QScript command is unavailable.
// Use: invoke from a configured weather item, never from a worker thread.
bool setWeather(Runtime &runtime, std::string_view weather);

// Purpose: set the retail frame cap from a menu value such as "30" or "60".
// Requires: a live game-thread Runtime; malformed values intentionally fall back to 30.
// Retail path: passes the parsed value to IGI::MISC::FRAMES_SET.
// Failure: returns false only when the runtime is not in a game callback.
// Use: call from a display/FPS menu action rather than writing a game setting directly.
bool setFps(Runtime &runtime, std::string_view value);

// Purpose: enable the retail player's unlimited-ammo behavior.
// Requires: a live game-thread Runtime and an active game session.
// Retail path: invokes IGI::PLAYER::SET_UNLIMITED_AMMO without raw memory writes.
// Failure: returns false when the helper is not on the game callback thread.
// Use: call this helper from a menu action; it is deliberately one-way like the retail native.
bool enableUnlimitedAmmo(Runtime &runtime);

// Purpose: give the local player a named weapon and its configured ammunition count.
// Requires: a live game-thread Runtime, a local player, and a retail weapon-id constant.
// Retail path: resolves SCRIPT::EVAL_CONST then invokes PLAYER::PICK_UP_WEAPON.
// Failure: returns false when the player, callback, or weapon-id resolution is unavailable.
// Use: pass ids such as "WEAPON_ID_GLOCK" from menu data, never a raw memory address.
bool pickupPlayerWeapon(Runtime &runtime, std::string_view weaponId, int ammo);

// Purpose: ask the player controller to cycle its weapon using the supplied retail force value.
// Requires: a live game-thread Runtime and a valid local player.
// Retail path: invokes IGI::PLAYER::CYCLE_WEAPON on GAME::GET_PLAYER(0).
// Failure: returns false when the player or game-thread callback is unavailable.
// Use: pass only a force value verified for the current mod's weapon configuration.
bool cyclePlayerWeapon(Runtime &runtime, int force);

// Purpose: request one of the player's configured weapon animation ids.
// Requires: a live game-thread Runtime, a local player, and a retail animation id.
// Retail path: invokes IGI::PLAYER::REQUEST_WEAPON_ANIM on the local HumanPlayer.
// Failure: returns false when the player or callback is unavailable; ids are not guessed.
// Use: obtain the animation id from verified game/mod data before calling this helper.
bool requestPlayerWeaponAnimation(Runtime &runtime, int animationId);

// Purpose: clear the player's hit/flash visual feedback through retail player natives.
// Requires: a live game-thread Runtime and a valid local HumanPlayer.
// Retail path: invokes PLAYER::FADE_HIT_FLASH followed by PLAYER::FADE_FLASH.
// Failure: returns false when the player or callback is unavailable.
// Use: call after a verified gameplay effect that should restore normal screen feedback.
bool restorePlayerVisualFeedback(Runtime &runtime);

// Purpose: lock or unlock movement/input for the local human player.
// Requires: a live game-thread Runtime and a valid local player.
// Retail path: selects HUMAN::UNLOCK_INPUT when enabled, otherwise HUMAN::LOCK_INPUT.
// Failure: returns false when the player or callback is unavailable.
// Use: use false for a modal sequence and true to return normal player control.
bool setPlayerInputEnabled(Runtime &runtime, bool enabled);

// Purpose: enable or disable mouse/look processing for the local human player.
// Requires: a live game-thread Runtime and a valid local player.
// Retail path: selects HUMAN::ENABLE_LOOK when enabled, otherwise HUMAN::DISABLE_LOOK.
// Failure: returns false when the player or callback is unavailable.
// Use: pair a disabled look state with a visible UI that explains how to restore it.
bool setPlayerLookEnabled(Runtime &runtime, bool enabled);

// Purpose: apply one normalized music volume to both music and master controls.
// Requires: a live game-thread Runtime; values are clamped to the inclusive 0.0-1.0 range.
// Retail path: invokes IGI::AUDIO::MUSIC_SET_VOLUME(volume, volume).
// Failure: returns false when the runtime is not in a game callback.
// Use: call with a menu slider value rather than calling the audio native in a demo.
bool setMusicVolume(Runtime &runtime, float volume);

// Purpose: apply a normalized sound-effects volume.
// Requires: a live game-thread Runtime; values are clamped to the inclusive 0.0-1.0 range.
// Retail path: invokes IGI::AUDIO::SET_SFX_VOLUME.
// Failure: returns false when the runtime is not in a game callback.
// Use: call with a menu slider value rather than calling the audio native in a demo.
bool setSfxVolume(Runtime &runtime, float volume);

// Purpose: log the current retail flow FPS, frame count, and render count.
// Requires: a live game-thread Runtime so all flow reads share the game callback boundary.
// Retail path: queries IGI::OTHER::FLOW_GET_FPS, FLOW_GET_FRAMES, and FLOW_GET_RENDERS.
// Failure: returns false only when the runtime is not in a game callback.
// Use: attach this to a diagnostics action without duplicating flow-native calls in a demo.
bool logFlowDiagnostics(Runtime &runtime);

// Purpose: log and display the local player's retail team and life state.
// Requires: a live game-thread Runtime and a valid local player.
// Retail path: invokes HUMAN::GET_TEAM and HUMAN::IS_DEAD for GAME::GET_PLAYER(0).
// Failure: returns false when the player or callback is unavailable.
// Use: attach this to diagnostics UI instead of reading player state in a demo.
bool logPlayerState(Runtime &runtime);

// Purpose: switch the local player camera and record the helper-controlled view mode.
// Requires: a live game-thread Runtime and a valid local player.
// Retail path: invokes HUMAN::VIEW_CAM with retail mode 1 (first) or 2 (third).
// Failure: returns false when the player or callback is unavailable.
// Use: call this helper instead of directly chaining VIEW_CAM from a demo menu.
bool setPlayerThirdPersonView(Runtime &runtime, bool active);

// Purpose: report whether this helper API last selected third-person camera mode.
// Requires: camera changes to flow through setPlayerThirdPersonView.
// Retail path: no guessed camera-memory read; it returns the helper's atomic state.
// Failure: external game/script camera changes cannot be observed by this API.
// Use: gate helper-owned third-person UI, including renderThirdPersonCrosshair.
bool isThirdPersonActive();

// Purpose: draw the retail status-screen crosshair at the center in third-person mode.
// Requires: a live unpaused game, local player, status screen, and helper-owned third-person state.
// Retail path: mirrors retail routine 0x0047A11B using StatusScreen_Get and Draw_SpriteZ.
// Failure: returns false when a required HUD/display resource is not available.
// Use: call from the display render callback every frame; false means no draw was issued.
bool renderThirdPersonCrosshair();
} // namespace igi
