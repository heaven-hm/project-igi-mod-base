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
