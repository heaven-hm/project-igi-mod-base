# Project I.G.I. Mod Base agent guide

## Scope

This repository builds Windows x86 (32-bit only) DLLs for the retail single-player Project I.G.I. executable (x64 / 64-bit is forbidden).
Keep the public mod API in `src/mod_base`, common implementation in `src/{config,logging,natives,platform,runtime,ui}`, and examples in `demos/`.

## Do

- Use C++20 and format first-party C++ with `python3 tools/format_cpp.py`.
- Run `python3 tools/format_cpp.py --check` before handing off a C++ change.
- Use the typed `IGI::` native wrappers. Verify menu-source native calls with `tools/igi_runtime_harness.py` before live injection.
- Keep menu-specific items and actions in the relevant demo; keep `src/` generic and reusable.
- Read settings from `project-igi-mod-config.ini`; do not hard-code user paths or hotkeys.
- Preserve the x86 Windows ABI and use the supported build script: `tools/build-win32-dll.sh`.
- Validate behavior-changing injected-DLL work in the Unicorn harness first, then in the game/Wine workflow when available.

## Do not

- Do not target, build, or add x64/64-bit architectures; the retail executable (igi.exe) is strictly 32-bit (x86) Windows only.
- Do not edit, commit, or require local game assets in `data/`; the checked-in native bindings must still build without them.
- Do not format or modify `third_party/` unless deliberately updating that dependency.
- Do not add raw game-memory offsets to demos when an `IGI::` native wrapper exists.
- Do not run UI, script, or game natives from a background DLL thread; use the game-thread runtime path.
- Do not hard-code a user name, machine-specific path, retail executable, Wine prefix, or key binding.
- Do not claim live-game success from a build or emulator result alone.

## Verification

```sh
python3 tools/format_cpp.py --check
cmake -S . -B build-macos-verify -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-macos-verify --parallel
tools/run-unicorn-tests.sh
```

For a Windows DLL, build with `tools/build-win32-dll.sh --release --target <target>` and use paths supplied through environment variables or command-line arguments for the live-game checks.
