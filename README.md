# Project IGI Natives SDK

Project IGI Natives SDK is a C++20, IGI 1-only SDK for offline single-player
mod development. It provides a guarded game-thread scheduler, a reusable
renderer-independent menu model, logging, and the complete IGI-RE address and
signature catalogue.

The current runtime target is the EU/Chinese Windows x86 IGI 1 executable. The
macOS build is a portable development and test build: it validates the
scheduler, menu, catalogue, and logging code, but it does not install the
Windows x86 game hook.

This project does not provide multiplayer features, a trainer, a memory editor,
an arbitrary memory writer, or a general-purpose process scanner.

## Repository layout

```text
project-igi-natives-sdk/
├── include/igi_sdk/
│   ├── logging/       public logging API
│   ├── natives/       native address/signature catalogue API
│   ├── runtime/       scheduler, hook, target profile, DLL lifecycle
│   └── ui/            renderer-independent MenuManager API
├── src/igi_sdk/       implementations with the same module layout
├── tests/              portable CTest coverage
├── data/IGI1/          IGI-RE address/name/signature metadata
├── legacy/             preserved project-igi-natives predecessor source
├── tools/              verification and catalogue-import scripts
├── docs/               focused architecture, licensing, and runtime notes
├── CMakeLists.txt
├── LICENSE / NOTICE / CREDITS.md
└── CHANGELOG.md
```

The `legacy/` directory is historical reference material and is not compiled
into the active SDK. New code belongs in `include/igi_sdk/` and
`src/igi_sdk/`; do not add new modules under `legacy/`.

## Requirements

Common:

- CMake 3.20 or newer
- C++20 compiler
- Git

macOS:

- Apple Clang and Command Line Tools
- CMake from Homebrew or the system installation

Windows IGI 1 DLL:

- 32-bit Windows C++ toolchain, or a MinGW-w64 i686 cross-compiler
- The supported IGI 1 executable profile documented in
  [`docs/IGI1_HOOK_EVIDENCE.md`](docs/IGI1_HOOK_EVIDENCE.md)

## Build and test on macOS

Build outside the repository so generated files never enter Git:

```sh
cmake -S . -B /tmp/igi-sdk-macos-release -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/igi-sdk-macos-release -j2
ctest --test-dir /tmp/igi-sdk-macos-release --output-on-failure
```

The macOS build produces the portable static library `libigi_sdk.a` and the
`igi_sdk_tests` executable. The tests cover scheduler thread binding, native
catalogue loading and deduplication, MenuManager behavior, callbacks, repeat
input, and shutdown.

## Build and test the Windows x86 DLL

From macOS or Linux with MinGW-w64:

```sh
cmake -S . -B /tmp/igi-sdk-windows-release \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_CXX_COMPILER=i686-w64-mingw32-g++ \
  -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/igi-sdk-windows-release -j2
```

The game module is `IGI-Natives-SDK-IGI1.dll`. It exports:

```cpp
BOOL WINAPI IGI_SDK_Initialize();
void WINAPI IGI_SDK_Shutdown();
unsigned long long WINAPI IGI_SDK_GetGameHookInvocationCount();
```

Call `IGI_SDK_Initialize` after the DLL has finished loading, never from
`DllMain`. Call `IGI_SDK_Shutdown` before unloading. The runtime validates the
supported executable hash and `Game_RunHandler` prologue before patching.

## Creating an IGI 1 mod

A mod should own its feature code and use the SDK for lifecycle, scheduling,
native metadata, and UI state. A minimal portable feature setup looks like:

```cpp
#include "igi_sdk/runtime/runtime.hpp"
#include "igi_sdk/ui/menu_manager.hpp"

void registerMod(igi::Runtime& runtime) {
    igi::MenuManager menu(runtime.scheduler());
    menu.createMenu("main", "My IGI Mod");
    menu.addToggle("main", "Example option", false, [](bool enabled) {
        // Game-affecting work runs later on the verified game thread.
        (void)enabled;
    });
}
```

For an internal Windows mod DLL:

1. Load the SDK/runtime after the game process is ready.
2. Call `IGI_SDK_Initialize` from the mod's normal initialization path.
3. Register feature callbacks through `MenuManager` and `Scheduler`.
4. Render or consume `MenuSnapshot` from a UI layer owned by the mod.
5. Call `IGI_SDK_Shutdown` before unloading.

Menu callbacks are queued with `Scheduler::enqueueOnce`; they do not execute on
the input or renderer thread. A future renderer can consume snapshots without
changing feature logic.

## Operating-system boundaries

### Windows

Windows x86 is the supported injected-runtime target. The primary scheduler
hook is `Game_RunHandler` at `0x00416260`. The trampoline executes the original
handler first and then drains SDK tasks on the game thread.

`Flow_RunHandler` at `0x00402260` is retained as a lifecycle investigation
reference. `Game_Update` at `0x00401050` is catalogued but is not used as the
SDK hook because it dispatches the active task handler indirectly.

The SDK never uses `Draw`, `MenuManager_Draw`, or `TextPrintDetour()` as its
gameplay execution hook.

### macOS

macOS is a development/test platform for portable SDK modules. Build and run
`igi_sdk_tests` there to validate behavior. The current IGI 1 hook uses Windows
x86 executable patching and therefore intentionally refuses installation on
macOS. Do not claim macOS in-process IGI injection support from this build.

## Native catalogue

`data/IGI1/neonix-igi-re-signatures.tsv` contains all imported IGI-RE address,
name, source-kind, and signature records. `NativeCatalogue` treats them as
`Catalogued`; an address is not callable merely because it appears in the
table. A future wrapper must independently verify its function body, calling
convention, arguments, return value, and target-build behavior.

No IGI-RE implementation body is copied into the active SDK.

## Verification tools

```sh
./tools/verify_igi_re_hook_source.sh
./tools/verify_igi1_hook.sh /path/to/IGI.EXE
```

The first command checks the local IGI-RE source registration and 30 Hz source
cadence evidence. The second checks the supported executable hash, hook
prologues, calling convention metadata, task-table xrefs, and `Game_Update`
dispatch shape. The exported invocation counter can be sampled in a live game
to measure actual cadence.

## Branches and releases

The repository uses three branches:

- `master` — stable branch
- `develop` — active integration branch
- `feature` — focused feature work

Build directories, binaries, and caches are not committed. Windows and macOS
artifacts are built in temporary Release directories and published only after
manual testing through GitHub Releases.

## License and credits

Read [`CREDITS.md`](CREDITS.md), [`NOTICE`](NOTICE), and the files under
`LICENSES/` before redistributing. The original project-igi-natives source is
preserved under its predecessor terms. IGI-RE metadata is attributed and kept
as address/signature research only.
