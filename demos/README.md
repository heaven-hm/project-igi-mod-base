# Demo menus

The `internal/` directory contains basic, simple and advanced in-game menus rendered by IGI 1's native QVM MenuManager. Their shared host lives in `src/mod_base/menu_host.cpp`. Build the Windows x86 targets `igi_basic_mod_menu`, `igi_simple_mod_menu`, and `igi_advanced_mod_menu`.

The `external/` directory contains the matching basic, simple and advanced Dear ImGui (Direct3D 7) mod menus with identical menu items and native actions. Their shared ImGui host lives in `src/mod_base/imgui_menu_host.cpp`. Build the Windows x86 targets `igi_basic_mod_menu_external`, `igi_simple_mod_menu_external`, and `igi_advanced_mod_menu_external`.
