# Verified Mod Helpers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Provide evidence-backed, game-thread-only helper APIs for common player, weapon, input/look, audio, world, and AI-context mod actions.

**Architecture:** Extend the existing `igi` helper façade rather than exposing raw native hashes. Public helpers resolve only retail-owned objects through typed wrappers, return `false` before a native call when their required game state is unavailable, and centralize status/log/menu-refresh behavior. AI helpers remain explicitly context-bound because their retail entry points consume QScript context.

**Tech Stack:** C++20, typed `IGI::` native wrappers, CTest, Python runtime harness, Unicorn, MinGW Win32 DLL build, Wine liveness runner.

## Global Constraints

- Use C++20 and format first-party C++ with `python3 tools/format_cpp.py`.
- Use typed `IGI::` wrappers; do not add raw offsets or unverified object layouts.
- Define helper behavior only in `demos/helpers/menu_helpers.cpp`; demo menu
  files may call helpers but may not define helper behavior or gameplay-native
  chains.
- Invoke gameplay natives only through the live game-thread helper path.
- Derive each helper call shape from `project-igi-internals/assets/IGINatives.json` and its native notes.
- Treat AI functions as QScript-context-only unless IGI-RE proves a selected-soldier context binding.
- Do not commit game assets or generated DLLs.
- Document every public helper with five usage-comment lines and in
  `demos/helpers/README.md`.
- Validate source changes with the existing harness before any Wine result is claimed.

---

### Task 1: Repair helper path verification and add source contracts

**Files:**
- Modify: `tests/MenuManagerTests.cpp:230-270`
- Modify: `tools/igi_runtime_harness.py:67`
- Test: `tests/MenuManagerTests.cpp`

**Interfaces:**
- Consumes: relocated `demos/helpers/menu_helpers.cpp` and `.hpp`.
- Produces: parity and source-contract checks that read the canonical helper source.

- [ ] **Step 1: Write failing source-contract assertions**

Add `verifiedMenuHelperContractTest()` that loads `demos/helpers/menu_helpers.cpp` and asserts the existing canonical helper evidence strings exist:

```cpp
assert(helperSource.find("IGI::AUDIO::MUSIC_SET_VOLUME") != std::string::npos);
assert(helperSource.find("IGI::MISC::FRAMES_SET") != std::string::npos);
assert(helperSource.find("IGI::HUMAN::VIEW_CAM") != std::string::npos);
```

- [ ] **Step 2: Run the focused test and verify it fails**

Run: `cmake --build build-macos-verify --target igi_sdk_tests && build-macos-verify/igi_sdk_tests`

Expected: failure because the test function does not yet exist.

- [ ] **Step 3: Repair stale helper paths**

Change the harness path to `ROOT / "demos" / "helpers" / "menu_helpers.cpp"`. Remove the obsolete external-to-internal include normalizer in `MenuManagerTests.cpp`, because merged internal and external demos now use the same helper include path.

- [ ] **Step 4: Run the focused test and verify the path repair passes**

Run: `cmake --build build-macos-verify --target igi_sdk_tests && build-macos-verify/igi_sdk_tests`

Expected: the source-contract and existing parity tests pass.

- [ ] **Step 5: Commit the isolated repair**

```bash
git add tests/MenuManagerTests.cpp tools/igi_runtime_harness.py
git commit -m "fix(tests): follow relocated menu helpers"
```

### Task 2: Add player, weapon, input/look, and audio helpers

**Files:**
- Modify: `demos/helpers/menu_helpers.hpp:10-22`
- Modify: `demos/helpers/menu_helpers.cpp:180-280`
- Modify: `tests/MenuManagerTests.cpp:240-285`
- Test: `tools/igi_runtime_harness.py --path demos/helpers/menu_helpers.cpp`

**Interfaces:**
- Consumes: `requireLiveGameThread(Runtime&)`, `IGI::GAME::GET_PLAYER(0)`.
- Produces:

```cpp
bool cyclePlayerWeapon(Runtime& runtime, int force);
bool requestPlayerWeaponAnimation(Runtime& runtime, int animationId);
bool restorePlayerVisualFeedback(Runtime& runtime);
bool setPlayerInputEnabled(Runtime& runtime, bool enabled);
bool setPlayerLookEnabled(Runtime& runtime, bool enabled);
bool setMusicVolume(Runtime& runtime, float volume);
bool setSfxVolume(Runtime& runtime, float volume);
```

- [ ] **Step 1: Extend the failing source-contract test**

Add assertions for the exact public function names above and their wrapper calls:

```cpp
assert(helperSource.find("bool cyclePlayerWeapon(Runtime &runtime, int force)") != std::string::npos);
assert(helperSource.find("IGI::PLAYER::CYCLE_WEAPON") != std::string::npos);
assert(helperSource.find("IGI::HUMAN::ENABLE_LOOK") != std::string::npos);
assert(helperSource.find("IGI::AUDIO::SET_SFX_VOLUME") != std::string::npos);
```

- [ ] **Step 2: Run the contract test and verify it fails**

Run: `cmake --build build-macos-verify --target igi_sdk_tests && build-macos-verify/igi_sdk_tests`

Expected: failure on the first absent function signature.

- [ ] **Step 3: Implement the typed player/world helpers**

For every player helper:

```cpp
if (!requireLiveGameThread(runtime))
    return false;
auto *player = IGI::GAME::GET_PLAYER(0);
if (!player)
    return false;
```

Cast the resolved player only to the typed parameter required by the existing
binding. Call `CYCLE_WEAPON`, `REQUEST_WEAPON_ANIM`, `FADE_HIT_FLASH` plus
`FADE_FLASH`, `LOCK_INPUT`/`UNLOCK_INPUT`, and `ENABLE_LOOK`/`DISABLE_LOOK`
respectively. Clamp audio volume with `std::clamp(volume, 0.0F, 1.0F)`, call
the typed `IGI::AUDIO` wrapper, then refresh the live menu.

- [ ] **Step 4: Run contracts, formatting, and the native scan**

Run:

```bash
python3 tools/format_cpp.py --check
cmake --build build-macos-verify --target igi_sdk_tests
build-macos-verify/igi_sdk_tests
IGI_EXE=/Users/haseeb-mir/Downloads/IGI/igi.exe python3 tools/igi_runtime_harness.py --path demos/helpers/menu_helpers.cpp
```

Expected: source contracts and formatting pass; harness reports every supported wrapper as PASS or explicitly DEFER/SKIP with its reason.

- [ ] **Step 5: Commit the helper group**

```bash
git add demos/helpers/menu_helpers.hpp demos/helpers/menu_helpers.cpp tests/MenuManagerTests.cpp
git commit -m "feat(helpers): add typed player and audio actions"
```

### Task 3: Enforce the verified AI-context boundary

**Files:**
- Modify: `demos/helpers/menu_helpers.cpp`
- Modify: `tests/MenuManagerTests.cpp`
- Test: `tools/igi_runtime_harness.py --path demos/helpers/menu_helpers.cpp`

**Interfaces:**
- Consumes: the evidence that every `IGI::AI::*` wrapper consumes retail
  QScript `out`, `data`, and `args` objects.
- Produces: a test-enforced guarantee that generic menu dispatch never invokes
  an AI native without a retail AI-script context.

- [ ] **Step 1: Write failing contract assertions**

Assert that `executeMenuNativeAction` does not contain `IGI::AI::`, while the
helper source contains the exact comment `AI script context is required`.

- [ ] **Step 2: Run the contract test and verify it fails**

Run: `cmake --build build-macos-verify --target igi_sdk_tests && build-macos-verify/igi_sdk_tests`

Expected: failure because the boundary documentation is absent.

- [ ] **Step 3: Implement explicit context rejection**

Place `// AI script context is required; generic menu dispatch must not invoke
IGI::AI wrappers.` above the menu dispatcher. Do not invoke `IGI::AI::*` from
a menu callback: their wrappers require QScript `out`, `data`, and `args`
objects owned by the retail AI event handler. A future AI helper is permitted
only after IGI-RE proves a safe context bridge.

- [ ] **Step 4: Validate the no-fabrication boundary**

Run:

```bash
python3 tools/format_cpp.py --check
cmake --build build-macos-verify --target igi_sdk_tests
build-macos-verify/igi_sdk_tests
IGI_EXE=/Users/haseeb-mir/Downloads/IGI/igi.exe python3 tools/igi_runtime_harness.py --path demos/helpers/menu_helpers.cpp
```

Expected: tests pass; the scan validates the helper source contains no raw AI
offset or fabricated `ScriptValue` object.

- [ ] **Step 5: Commit the API boundary**

```bash
git add demos/helpers/menu_helpers.cpp tests/MenuManagerTests.cpp
git commit -m "test(helpers): enforce AI script context boundary"
```

### Task 4: Pre-documentation integration baseline

**Files:**
- Verify: `demos/helpers/menu_helpers.cpp`
- Verify: `tests/MenuManagerTests.cpp`

**Interfaces:**
- Consumes: all helper APIs from Tasks 1-3.
- Produces: evidence that the helper set builds and scans on the supported
  local toolchains.

- [ ] **Step 1: Run the complete local quality gate**

```bash
python3 tools/format_cpp.py --check
cmake -S . -B build-macos-verify -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-macos-verify --parallel
ctest --test-dir build-macos-verify --output-on-failure
```

- [ ] **Step 2: Build both Windows menu hosts and run helper verification**

```bash
IGI_EXE=/Users/haseeb-mir/Downloads/IGI/igi.exe IGI_GAME=/Users/haseeb-mir/Downloads/IGI tools/build-win32-dll.sh --release --target igi_advanced_mod_menu --verify --deploy
IGI_EXE=/Users/haseeb-mir/Downloads/IGI/igi.exe IGI_GAME=/Users/haseeb-mir/Downloads/IGI tools/build-win32-dll.sh --release --target igi_advanced_mod_menu_external --verify --deploy
```

- [ ] **Step 3: Review and commit verification-only adjustments if needed**

Review `git diff --check`, `git status --short`, and generated-artifact status. Commit only source/test changes; leave generated DLLs untracked.

### Task 5: Verify and document third-person helpers

**Files:**
- Modify: `demos/helpers/menu_helpers.hpp`
- Modify: `demos/helpers/menu_helpers.cpp`
- Create: `demos/helpers/README.md`
- Modify: `tests/MenuManagerTests.cpp`
- Test: `tools/igi_runtime_harness.py --path demos/helpers/menu_helpers.cpp`

**Interfaces:**
- Consumes: `isThirdPersonActive()` and `renderThirdPersonCrosshair()`.
- Produces: evidence-backed third-person behavior and mod-author documentation
  for every public helper.

- [ ] **Step 1: Add failing source-contract checks**

Require the helper source to call typed `IGI::HUMAN::VIEW_CAM` for view mode,
and require the public header to contain five adjacent documentation lines for
`isThirdPersonActive` and `renderThirdPersonCrosshair` describing purpose,
game-thread precondition, retail call chain, failure behavior, and example use.

- [ ] **Step 2: Run the test and verify it fails**

Run: `cmake --build build-macos-verify --target igi_sdk_tests && build-macos-verify/igi_sdk_tests`

Expected: failure until the comments and verified call path are present.

- [ ] **Step 3: Verify or remove unverified third-person operations**

Inspect the IGI-RE catalog and native notes for every helper call. Keep only
typed wrapper calls or catalog-backed native functions. If the camera-state
memory layout used by `isThirdPersonActive` lacks evidence, retain only the
explicit atomic state set by `setThirdPersonView`; do not read an undocumented
camera offset. If the crosshair renderer lacks evidence for its native call
shape, disable it with a documented `false` result rather than using a raw
offset.

- [ ] **Step 4: Write mod-author documentation**

Create `demos/helpers/README.md` with one section per public helper group.
Every section must include its preconditions and a C++ call example from a
`runModMenu(const igi::MenuConfig&, igi::Runtime&, std::size_t, std::string_view)`
implementation.

- [ ] **Step 5: Run docs/source checks and commit**

Run:

```bash
python3 tools/format_cpp.py --check
cmake --build build-macos-verify --target igi_sdk_tests
build-macos-verify/igi_sdk_tests
IGI_EXE=/Users/haseeb-mir/Downloads/IGI/igi.exe python3 tools/igi_runtime_harness.py --path demos/helpers/menu_helpers.cpp
```

Commit:

```bash
git add demos/helpers/menu_helpers.hpp demos/helpers/menu_helpers.cpp demos/helpers/README.md tests/MenuManagerTests.cpp
git commit -m "docs(helpers): document verified mod helper usage"
```

### Task 6: Final integration verification

**Files:**
- Verify: `demos/helpers/menu_helpers.cpp`
- Verify: `demos/helpers/README.md`
- Verify: `tests/MenuManagerTests.cpp`

**Interfaces:**
- Consumes: the complete public helper API and documentation from Tasks 1-5.
- Produces: final build and harness evidence.

- [ ] **Step 1: Run the full local quality gate**

```bash
python3 tools/format_cpp.py --check
cmake -S . -B build-macos-verify -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-macos-verify --parallel
ctest --test-dir build-macos-verify --output-on-failure
```

- [ ] **Step 2: Build and verify both Win32 menu hosts**

```bash
IGI_EXE=/Users/haseeb-mir/Downloads/IGI/igi.exe IGI_GAME=/Users/haseeb-mir/Downloads/IGI tools/build-win32-dll.sh --release --target igi_advanced_mod_menu --verify --deploy
IGI_EXE=/Users/haseeb-mir/Downloads/IGI/igi.exe IGI_GAME=/Users/haseeb-mir/Downloads/IGI tools/build-win32-dll.sh --release --target igi_advanced_mod_menu_external --verify --deploy
```

- [ ] **Step 3: Inspect generated-artifact status**

Run: `git status --short`

Expected: generated DLLs are ignored or untracked and are not staged; only
reviewed source, test, and documentation files may be committed.
