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
│   ├── natives/       catalogue, runner, and domain APIs
│   │   └── domains/   AI, soldiers, vehicles, camera, graphs, memory
│   ├── runtime/       scheduler, hook, target profile, DLL lifecycle
│   └── ui/            renderer-independent MenuManager API
├── src/igi_sdk/       implementations with the same module layout
├── demos/
│   ├── internal-menumanager/  in-process MenuManager DLL example
│   └── imgui/                 external-style catalogue companion example
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
into the active SDK. Its old executor, MinHook wrappers, and game-specific
helpers remain there only for attribution and migration review. New code
belongs in `include/igi_sdk/` and `src/igi_sdk/`; do not add new modules under
`legacy/`.

The internal `Igi1MenuManager` model is based on the decoded retail files
`ingamemenu.qvm` and `MAINMENU.QVM`. Their structure is represented as
`MenuManager` → numbered `MenuScreen`/`DialogWindow` → `MenuFrame` → widgets.
The widget API includes the retail `MenuText`, `MenuTextConditional`,
`SlideBar`, `ListBox`, `ToggleBox`, `InputBox`, `AsciiBox`, `ScrollListBox`,
`PictureBox`, `TextBox`, `TypeWriterBox`, and `ControlsMenu` components.
Screen transitions preserve the retail `PushScreen`, `RequestScreen`,
`PopScreen`, popup, and leave-menu concepts. The source evidence is retained
outside the repository because game assets are not redistributed.

The demo directory contains examples only. `internal-menumanager` is the
in-process Windows DLL shape requested by the original specification. The
`imgui` directory currently contains a safe external-style companion that
reads the native catalogue; it does not inject, scan, attach to, or write to
the game. A real out-of-process mod requires a separately reviewed IPC
contract and is intentionally not implied by this example.

## Requirements

Common:

- CMake 3.20 or newer
- C++20 compiler
- Git

macOS:

- Apple Clang and Command Line Tools
- CMake from Homebrew or the system installation

Linux:

- GCC or Clang with C++20 support
- CMake and a native build tool such as Ninja or Make

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
portable `IGI-Natives-SDK-IGI1.dylib` export library plus the
`igi_sdk_tests` executable. The tests cover scheduler thread binding, native
catalogue loading and deduplication, MenuManager behavior, callbacks, repeat
input, and shutdown.

## Build and test on Linux

The Linux build produces `libigi_sdk.a`,
`libIGI-Natives-SDK-IGI1.so`, the internal shared-library demo, the external
companion, and the portable test executable:

```sh
cmake -S . -B /tmp/igi-sdk-linux-release -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/igi-sdk-linux-release --parallel
ctest --test-dir /tmp/igi-sdk-linux-release --output-on-failure
```

Linux uses the same portable scheduler, native catalogue, runner, IGI 1 QVM
menu model, logging, and demo ABI as macOS. It does not install the Windows
x86 executable patch; `HookManager` reports that the Windows hook backend is
unavailable on Linux.

To build the examples as well, leave the default enabled or set the option
explicitly:

```sh
cmake -S . -B /tmp/igi-sdk-macos-demos \
  -DCMAKE_BUILD_TYPE=Release -DIGI_SDK_BUILD_DEMOS=ON
cmake --build /tmp/igi-sdk-macos-demos -j2
/tmp/igi-sdk-macos-demos/IGI-MenuManager-External
```

On Windows the internal target is emitted as `IGI-MenuManager-Internal.dll`.
It exports `IGI_MenuManager_Internal_Initialize`,
`IGI_MenuManager_Internal_RunFrame`, and
`IGI_MenuManager_Internal_Shutdown`; use it as a complete reference for a mod
DLL's startup, menu registration, frame scheduling, and shutdown path. It is
also built on macOS as `libIGI-MenuManager-Internal.dylib`; there it runs the
menu and scheduler without installing the Windows game hook. The external
target is emitted as `IGI-MenuManager-External.exe` on Windows or
`IGI-MenuManager-External` on macOS. It loads every shipped record, reports
domain counts, resolves `Game_Start` through `NativeRunner`, and runs that
resolution callback through `Scheduler`.

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

### Loading the internal demo

`IGI-MenuManager-Internal.dll` is a complete in-process shared-library demo,
not a standalone executable. An authorized mod loader must load it into the
same 32-bit IGI process, then call the exported functions from its normal
plugin lifecycle:

```cpp
using Initialize = bool (*)();
using RunFrame = std::size_t (*)();
using Shutdown = void (*)();

// Resolve these from IGI-MenuManager-Internal.dll after the game is ready.
Initialize initialize = /* loader symbol lookup */;
RunFrame runFrame = /* loader symbol lookup */;
Shutdown shutdown = /* loader symbol lookup */;

initialize();
// The real Windows runtime hook drains the scheduler. RunFrame is also
// exported for a host/plugin harness and portable demo testing.
shutdown();
```

Do not call initialization from `DllMain`. The repository does not ship a
remote-process injector. Use the loader already authorized for the user's own
game installation and match the game's 32-bit architecture. On macOS, build
`libIGI-MenuManager-Internal.dylib` and load it from a same-process test host
with `dlopen`/`dlsym`; it exercises the menu and scheduler but does not patch
or inject into the Windows IGI executable.

### Running the external example

The external example is a normal process and requires no injection:

```sh
./IGI-MenuManager-External /path/to/neonix-igi-re-signatures.tsv
```

It loads all records, reports domain counts, resolves a native by name, and
executes that metadata callback through `Scheduler`. It deliberately does not
attach to another process or write game memory. Any future external control
must use a separately reviewed IPC protocol shared with an in-process mod.

The internal demo's IGI 1 mod screen contains ten example options: weather,
all weapons, sound profile, free camera, invulnerability, unlimited ammo,
enemy visibility, player speed, mission teleport, and runtime logging. These
are menu action definitions tied to the SDK scheduler boundary. A mod must
bind each action to a separately verified native wrapper before it changes
game state; the demo never calls an unverified address just because a name is
present in the catalogue.

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

For a standalone macOS or Windows companion:

1. Link against the portable `igi_sdk` library.
2. Read the shipped TSV catalogue with `NativeCatalogue`.
3. Use an explicit, user-controlled IPC protocol if the companion later needs
   to communicate with a running mod; do not infer addresses from the TSV and
   write into another process.
4. Keep OS-specific process, window, and renderer code outside the portable
   SDK modules.

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


### macOS

macOS is a development/test platform for portable SDK modules. Build and run
`igi_sdk_tests` there to validate behavior. The current IGI 1 hook uses Windows
x86 executable patching and therefore intentionally refuses installation on
macOS. Do not claim macOS in-process IGI injection support from this build.

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

## License and credits

Read [`CREDITS.md`](CREDITS.md), [`NOTICE`](NOTICE), and the files under
`LICENSES/` before redistributing. The original project-igi-natives source is
preserved under its predecessor terms. IGI-RE metadata is attributed and kept
as address/signature research only.
