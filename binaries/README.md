# Project I.G.I. Mod Base Prebuilt Binaries

This directory contains prebuilt x86 Windows (Win32) binaries and mod menu DLLs for Project I.G.I.

## Available Binaries

| Binary | Menu Type | Subsystem / Backend | Description |
| :--- | :--- | :--- | :--- |
| `igi-advanced-mod-internal.dll` | **Internal** | IGI 1 QVM `MenuManager` | Resident in-game menu running on the game thread with native UI controls (Weather, FPS, Weapons, God Mode / Ammo, Team, Level Select). Default hotkey: **F5**. |
| `igi-advanced-mod-external.dll` | **External** | Dear ImGui + Direct3D 7 | Modern Dear ImGui overlay hooked into the Direct3D 7 render pipeline via [D3D7Imgui](https://github.com/AndenDev/D3D7Imgui). Default hotkey: **F5** (toggle window overlay). |

## Branch Architecture

- **`develop`**: Dedicated strictly to the **IGI Game Menu UI (Internal Mod Menu)** using Project I.G.I.'s retail QVM script and `MenuManager` lifecycle.
- **`feature/imgui-mod`**: Dedicated to the **Dear ImGui (External Mod Menu)** using legacy Direct3D 7 overlay rendering via `D3D7Imgui`.

## Building Binaries

To build these DLLs from source using the cross-compilation toolchain:

```sh
# Internal Advanced Mod Menu (develop & feature/imgui-mod)
tools/build-win32-dll.sh --release --target igi_advanced_mod_menu

# External ImGui Advanced Mod Menu (feature/imgui-mod)
tools/build-win32-dll.sh --release --target igi_advanced_mod_menu_external

# Injector tool
tools/build-win32-dll.sh --release --target igi_injector
```

Built DLLs are automatically copied to this `binaries/` directory upon build completion.

## Injection & Usage

Inject the desired DLL into running `igi.exe` using `IGI-Injector.exe`:

```cmd
:: Inject internal mod menu
IGI-Injector.exe <PID> binaries\igi-advanced-mod-internal.dll

:: Inject external ImGui mod menu
IGI-Injector.exe <PID> binaries\igi-advanced-mod-external.dll
```

Or under Wine on macOS / Linux:

```sh
WINEPREFIX=~/.wine tools/wine_igi_inject.sh binaries/igi-advanced-mod-internal.dll igi.exe
```
