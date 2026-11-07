# Changelog

## 0.2 — Restructure and runtime lifecycle

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
