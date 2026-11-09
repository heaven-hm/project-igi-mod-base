# Changelog

## 0.3 — Heaven production menu & Heaven Advanced: 50+ verified gameplay controls

- **Production Menu Expansion (50 Verified Controls):**
  - Heaven ships 50 verified controls across six primary categories (Weapons,
    Player, Camera, World, Vehicles, AI / NPC) plus dynamic target selection, all
    declared from a unified definition table enforcing category and dispatch routes.
  - The retail QVM menu fits only a handful of rows, so control sets that
    outgrow the frame dynamically generate a scrollable `ListBox` over a flattened
    command list (`IGI_SDK_FillList/GetList/SetList(-1)`). Every option stays
    accessible within the frame; smaller control sets preserve individual controls.
- **Heaven Advanced Targets:**
  - Added dedicated Heaven Advanced DLL targets (`igi-heaven-mod-advanced-internal.dll`
    and `igi-heaven-mod-advanced-external.dll`) powered by `IGI_HEAVEN_ADVANCED=1`.
  - Added live NPC 3D movement controls: Move NPC Forward (+5m), Backward (-5m),
    Left (-5m), Right (+5m), Elevate (+2m), and Snap to Ground via retail
    `Human_SetPosition` and view forward/lateral matrix calculations.
  - Added live NPC Ground State controls: switch selected NPC between `Ground`
    and `Air` (levitation) via `Human_SnapToGround` and `Human_LeaveGround`.
  - Added Player Ground State controls: toggle player between `Ground` and `Air`.
  - Added Helicopter Reset control (`resetHeli`): resets selected helicopter state
    via the retail `tick = 0` control branch.
- **Weapons & HUD:**
  - Added direct weapon ammo refill controls for Glock 17, MP5SD, M16A2, and AK-47.
  - Added full weapon inventory equipping, weapon draw animations, and cycle weapon.
  - Implemented 3rd-person crosshair rendering in both internal (retail QVM / sprite)
    and external (Dear ImGui) Heaven menus using the native HUD sprite draw pipeline.
- **Dynamic Live Target Tracking:**
  - Live NPC, car/tank, and helicopter target lists render the retail task name plus
    stable task id (`HumanSoldier_2021 [#2021]`), refresh from the running game's
    task table in both menu frontends, track selected targets across menu refreshes,
    and drop targets that leave the level.
  - Identified tanks in live Car targets using the shared retail vehicle controller.
- **World & Vehicle Controls:**
  - Added high refresh frame cap presets (30, 60, 120, 144, 240 FPS).
  - Added car driving controls with verified throttle/steering/braking inputs.
  - Added whole-level explode all cars and reset all cars actions.
- **Platform & Injection:**
  - Fixed Wine module matching in `tools/igi_injector.cpp` by matching loaded
    modules by DLL filename rather than strict full-path matching.
- **Quality Assurance & Verification:**
  - Added `igi_heaven_layout` and `igi_heaven_menu_contract` test suites that enforce:
    - Normal Heaven ships exactly 50 working controls without dropping categories.
    - Advanced controls are properly gated under `#if defined(IGI_HEAVEN_ADVANCED)`.
    - Every menu action has a dedicated game-thread native execution branch.

## 0.2 — Restructure, Dear ImGui Direct3D 7, and runtime lifecycle

- Integrated Dear ImGui Direct3D 7 renderer backend from
  [AndenDev/D3D7Imgui](https://github.com/AndenDev/D3D7Imgui) (`imgui_impl_dx7.cpp`,
  `imgui_impl_dx7.h`, `imgui_impl_win32.cpp`) for legacy DirectX 7 game compatibility.
- Implemented Dear ImGui external mod menus for Basic, Simple, and Advanced menus
  (`igi-basic-mod-external.dll`, `igi-simple-mod-external.dll`, `igi-advanced-mod-external.dll`)
  via `src/mod_base/imgui_menu_host.cpp`.
- Added comprehensive `[imgui]` configuration options in `project-igi-mod-config.ini`
  for themes (dark/light/classic), font scaling, alpha opacity, window dimensions,
  mouse navigation, and persistent layout management.
- Reorganized the codebase into focused `src/config`, `src/logging`,
  `src/mod_base`, `src/natives`, `src/platform`, `src/runtime`, and `src/ui`
  modules.
- Added shared internal-menu hosting for the Basic, Simple, and Advanced mod
  menus, while keeping each menu’s actions in its demo source.
- Added C++ formatting, clang-tidy, CTest, Unicorn validation, and GitHub
  Actions quality checks.
- Updated the Windows x86 build, deployment, Wine, and injector helpers around
  the renamed mod artifacts and configuration files.
- Fixed level switching to follow the game’s native unload/load sequence:
  select mission, request Flow game event, then delete the current `Game` task.
- Release injected QVM resources before changing levels so the menu can attach
  again in the newly loaded mission.
- Added lifecycle guards for native calls when exiting to the main menu.
- Made the configured end key stop the resident host thread after safe cleanup;
  the start key remains disabled until `igi.exe` exits.
- Added Simple and Advanced menu screenshots, feature lists, and contributor
  credits to the README.
- Fixed visible weather: shared menu helpers now parent the retail `RainEffect`
  task under the active `Level` task and broadcast `Param_GetChangedEvent` for
  live rain and snow precipitation effects.
- Added prebuilt x86 release DLLs in `binaries/` (`igi-advanced-mod-internal.dll`
  and `igi-advanced-mod-external.dll`) with automatic deployment on build completion.
- Clarified repository branching model: `develop` is strictly dedicated to the
  retail IGI Game Menu UI (Internal Mod Menu via QVM MenuManager), while
  `feature/imgui-mod` provides the external Dear ImGui Direct3D 7 menu
  implementation backed by [AndenDev/D3D7Imgui](https://github.com/AndenDev/D3D7Imgui).

## 0.1 — IGI 1 MVP

- Added Windows x86 internal menu demos, configurable hotkeys, logging, and DLL lifecycle management.
- Added game-thread scheduling through display frame callbacks and an IGI-RE native catalogue.
- Added Windows build and injection helpers, Wine launch scripts, and Unicorn native verification.
- Added menu controls for weather, frame rate, weapons, and other native actions.
- Added configurable menu titles, the simple-menu screenshot, and game-constant weapon selection.
- Release builds omit debug sections. Level switching now requests game teardown before starting the selected mission; live verification remains pending.

The supported executable profile is the IGI-RE EU/Chinese IGI 1 build.
