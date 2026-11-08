# Changelog

## 0.3 — Heaven production menu: 50 verified controls

- Heaven now ships 50 controls across seven categories (Weapons, Player,
  Camera, World, Vehicles, AI / NPC, Audio), all declared from one table so a
  control cannot ship without a category or an action route.
- The retail QVM menu fits only a handful of rows, so a control set that
  outgrows the frame now generates one scrollable `ListBox` over a flattened
  command list (`IGI_SDK_FillList/GetList/SetList(-1)`). Every option stays
  reachable instead of being drawn past the frame edge; small control sets keep
  the previous per-control layout.
- Live NPC, car, and helicopter target lists render the retail task name plus a
  stable task id (`HumanSoldier_2021 [#2021]`), refresh from the running game's
  task table in both menu frontends, follow the selected target by task
  identity, and drop a target that leaves the task table.
- Added gameplay readouts backed by verified retail getters: player status,
  camera info, NPC status, soldier parameters, car and helicopter info, level
  tick, flow counters, game events, weapon events, and audio info/rate.
- Added player position sync, player controller flag controls, all-weapon equip,
  all-car explode/reset, and soldier stand-down.
- Native verification found and fixed three defects before release:
  `Task_Find`-by-name faulted without a populated type table (helicopters now
  match the retail type name), `Level_GetTick` dereferenced an absent level
  (now guarded), and `Vehicle_ResetControls` used a widened pointer signature.
- Added `igi_heaven_layout` and route-audit tests that fail if the shipped
  control set drops below 30 controls, loses a category, loses its dispatch
  branch, or stops generating a complete compilable menu resource.

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
