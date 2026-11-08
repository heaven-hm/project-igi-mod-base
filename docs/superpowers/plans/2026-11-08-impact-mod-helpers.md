# Impact Mod Helpers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver a class-based, typed-native helper API with at least fifty high-impact Project I.G.I. player, weapon, camera, world, vehicle, and explicit-context AI methods.

**Architecture:** `demos/helpers/menu_helpers.hpp` becomes the compact public API. `demos/helpers/menu_helpers.cpp` owns game-thread validation, local-player conversion, menu refresh, QScript execution, and all rendering/native implementation. Demos construct helper classes from their `Runtime` and never define gameplay helper chains.

**Tech Stack:** C++20, typed `IGI::` wrappers, MinGW-w64 x86 DLL targets, IGI-RE Unicorn runtime harness.

## Global Constraints

- Call only typed wrappers already declared in `src/natives/mvp_natives.hpp`.
- Do not use raw offsets in demos or invent an NPC-speed, health, vehicle-control, free-camera, or AI ABI.
- AI public methods require an explicit `AiScriptContext` supplied by a valid retail script callback.
- Keep long usage explanation in `demos/README.md`; public header declarations use at most one short line comment.
- Build with `tools/build-win32-dll.sh --release --target <target>`.
- Run `python3 tools/format_cpp.py --check` and Unicorn `tools/igi_runtime_harness.py`; do not launch or inject the game.

---

### Task 1: Establish compact classes and contract tests

**Files:**
- Modify: `demos/helpers/menu_helpers.hpp`
- Modify: `demos/helpers/menu_helpers.cpp`
- Modify: `tests/MenuManagerTests.cpp`

**Interfaces:**
- Produces `PlayerHelpers`, `WeaponHelpers`, `CameraHelpers`, `WorldHelpers`, `VehicleHelpers`, `AiHelpers`, `AudioHelpers`, and `AiScriptContext`.
- Each helper takes `Runtime&` in its constructor; object-specific methods take typed `IGI` pointers supplied by the caller.

- [ ] **Step 1: Write the failing source contract**

```cpp
assert(helperHeader.find("class PlayerHelpers") != std::string::npos);
assert(helperHeader.find("class WeaponHelpers") != std::string::npos);
assert(helperHeader.find("class CameraHelpers") != std::string::npos);
assert(helperHeader.find("class AiHelpers") != std::string::npos);
assert(helperSource.find("g_thirdPersonActive") == std::string::npos);
```

- [ ] **Step 2: Run the source contract without a host-platform DLL build**

Run: `python3 -c 'from pathlib import Path; assert "class PlayerHelpers" not in Path("demos/helpers/menu_helpers.hpp").read_text()'`

Expected: PASS before classes replace flat helper declarations.

- [ ] **Step 3: Replace flat declarations with classes**

```cpp
struct AiScriptContext { IGI::ScriptValue* out; void* data; void* args; };
class PlayerHelpers { public: explicit PlayerHelpers(Runtime&); bool setInputEnabled(bool); private: Runtime& runtime_; };
class WeaponHelpers { public: explicit WeaponHelpers(Runtime&); bool enableUnlimitedAmmo(); private: Runtime& runtime_; };
```

Keep `executeMenuNativeAction` as the one compatibility adapter while migrating menus.

- [ ] **Step 4: Run the source contract**

Run: `python3 -c 'from pathlib import Path; assert "class PlayerHelpers" in Path("demos/helpers/menu_helpers.hpp").read_text()'`

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add demos/helpers/menu_helpers.hpp demos/helpers/menu_helpers.cpp tests/MenuManagerTests.cpp
git commit -m "refactor(demos): introduce impact helper classes"
```

### Task 2: Implement player and weapon impact methods

**Files:**
- Modify: `demos/helpers/menu_helpers.hpp`
- Modify: `demos/helpers/menu_helpers.cpp`
- Modify: `tests/MenuManagerTests.cpp`

**Interfaces:**
- `PlayerHelpers`: `moveScale`, `state`, `team`, `index`, `isDead`, `isCrouching`, `isActive`, `isPlayer`, `setInputEnabled`, `setLookEnabled`, `startAction`, `checkDeath`, `sendPosition`, `setFlagD44`, `clearFlagD44`, `setFlagD68`, `clearFlagD68`.
- `WeaponHelpers`: `pickup`, `enableUnlimitedAmmo`, `setAnimation`, `requestAnimation`, `cycle`, `selectCategory`, `hasAmmo`, `type`, `fireEvent`, `typeEvent`, `triggerEvent`, `stateEvent`, `moveEvent`.

- [ ] **Step 1: Add failing native-call assertions**

```cpp
assert(helperSource.find("IGI::PLAYER::START_ACTION") != std::string::npos);
assert(helperSource.find("IGI::PLAYER::SELECT_WEAPON_CATEGORY") != std::string::npos);
assert(helperSource.find("IGI::WEAPON::HAS_AMMO") != std::string::npos);
assert(helperSource.find("IGI::HUMAN::GET_MOVE_SCALE") == std::string::npos);
```

The final assertion prevents inventing a human speed setter; the supported native is `PLAYER::GET_MOVE_SCALE`.

- [ ] **Step 2: Run the failing contract**

Run: `python3 tools/igi_runtime_harness.py --verify-menu-helpers --lenient`

Expected: FAIL until typed player and weapon calls are present.

- [ ] **Step 3: Implement methods with private local-player conversion**

```cpp
IGI::HumanPlayer* localPlayer(Runtime& runtime) {
    if (!requireLiveGameThread(runtime)) return nullptr;
    return reinterpret_cast<IGI::HumanPlayer*>(IGI::GAME::GET_PLAYER(0));
}
```

Every action returns `false` on a missing runtime/player; every query returns `std::optional<T>` when a missing player would otherwise be ambiguous.

- [ ] **Step 4: Run contract and formatting**

Run: `python3 tools/format_cpp.py --check && python3 tools/igi_runtime_harness.py --verify-menu-helpers --lenient`

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add demos/helpers/menu_helpers.hpp demos/helpers/menu_helpers.cpp tests/MenuManagerTests.cpp
git commit -m "feat(demos): add player and weapon impact helpers"
```

### Task 3: Implement camera, world, and mission methods

**Files:**
- Modify: `demos/helpers/menu_helpers.hpp`
- Modify: `demos/helpers/menu_helpers.cpp`
- Modify: `src/mod_base/menu_host.hpp`
- Modify: `src/mod_base/menu_host.cpp`
- Modify: `src/mod_base/imgui_menu_host.cpp`

**Interfaces:**
- `CameraHelpers`: `setViewMode`, `setWeaponTarget`, `setWeaponAngles`, `setCamera`, `setTarget`, `setDip`, `setZoomed`, `eye`, `forward`, `matrix`, `renderCrosshair`.
- `WorldHelpers`: `changeLevel`, `setWeather`, `setPaused`, `isPaused`, `refreshDisplay`, `setFrameCap`, `event`, `flowFps`, `flowFrames`, `flowRenders`, `taskCount`.

- [ ] **Step 1: Add failing camera/world contract**

```cpp
assert(helperSource.find("IGI::CAMERA::SET_CAMERA") != std::string::npos);
assert(helperSource.find("IGI::GAME::SET_PAUSED") != std::string::npos);
assert(helperSource.find("IGI::OTHER::FLOW_SET_REQUEST") != std::string::npos);
assert(helperSource.find("isThirdPersonActive") == std::string::npos);
```

- [ ] **Step 2: Run the failing contract**

Run: `python3 tools/igi_runtime_harness.py --verify-menu-helpers --lenient`

Expected: FAIL.

- [ ] **Step 3: Implement only truthful camera behavior**

`setViewMode` calls `HUMAN::VIEW_CAM`. Remove `isThirdPersonActive`; the catalog has no verified camera-mode getter. `renderCrosshair` must use a caller-supplied, verified render request and return `false` when the status sprite/display path is unavailable. It must not read a camera offset or atomic cached mode.

- [ ] **Step 4: Migrate both render hosts**

Replace global crosshair calls in `menu_host.cpp` and `imgui_menu_host.cpp` with `CameraHelpers(runtime).renderCrosshair()` only at the game-thread render callback.

- [ ] **Step 5: Verify and commit**

Run: `python3 tools/format_cpp.py --check && python3 tools/igi_runtime_harness.py --verify-menu-helpers --lenient`

```bash
git add demos/helpers src/mod_base/menu_host.* src/mod_base/imgui_menu_host.cpp tests/MenuManagerTests.cpp
git commit -m "feat(demos): add camera and world impact helpers"
```

### Task 4: Implement vehicle and explicit-context AI methods

**Files:**
- Modify: `demos/helpers/menu_helpers.hpp`
- Modify: `demos/helpers/menu_helpers.cpp`
- Modify: `tests/MenuManagerTests.cpp`

**Interfaces:**
- `VehicleHelpers`: `carControlInfo`, `carFrame`, `carLight`, `carMountedGun`, `carGunnerPose`, `carTrackFactor`, `heliDoor`, `heliControlInfo`, `heliStateSize`, `heliConfigSize`, `heliSoundInfo`.
- `AiHelpers`: `setViewLength`, `setAlarmViewLength`, `setViewAlpha`, `setViewGamma`, `setSecondaryViewLength`, `setSecondaryAlarmViewLength`, `setSecondaryViewAlpha`, `setSecondaryViewGamma`, `setEventPriority`, `setInvulnerability`, `setInstantDeath`, `setDeathAnimation`, `setAlarmTriggerId`, `setAlarmControlId`, `setAlarmAccess`, `currentEventType`, `randomValue`.

- [ ] **Step 1: Add failing ABI-boundary test**

```cpp
assert(helperHeader.find("struct AiScriptContext") != std::string::npos);
assert(helperSource.find("IGI::AI::SET_INVULNERABILITY(context.out, context.data, context.args)") != std::string::npos);
assert(helperSource.find("reinterpret_cast<IGI::Car *>(IGI::GAME::GET_PLAYER") == std::string::npos);
```

- [ ] **Step 2: Run the failing contract**

Run: `python3 tools/igi_runtime_harness.py --verify-menu-helpers --lenient`

Expected: FAIL.

- [ ] **Step 3: Implement object/context-safe wrappers**

Vehicle methods reject null caller-supplied `IGI::Car*` or `IGI::Heli*`. AI methods reject null `out`, `data`, or `args` and pass those exact values to typed `IGI::AI` wrappers. Do not add speed, health, steering, or free-camera methods without a verified matching native.

- [ ] **Step 4: Verify and commit**

Run: `python3 tools/format_cpp.py --check && python3 tools/igi_runtime_harness.py --verify-menu-helpers --lenient`

```bash
git add demos/helpers/menu_helpers.hpp demos/helpers/menu_helpers.cpp tests/MenuManagerTests.cpp
git commit -m "feat(demos): add vehicle and AI impact helpers"
```

### Task 5: Migrate demos and publish public API documentation

**Files:**
- Modify: `demos/internal/simple-mod/simple_menu.cpp`
- Modify: `demos/external/simple-mod/simple_menu.cpp`
- Modify: `demos/internal/advanced-mod/advanced_menu.cpp`
- Modify: `demos/external/advanced-mod/advanced_menu.cpp`
- Modify: `demos/README.md`

**Interfaces:**
- All demo operations consume helper classes constructed from the provided `Runtime`.
- README contains a one-paragraph entry for every public method, grouped by class.

- [ ] **Step 1: Add a demo-source contract**

```cpp
assert(internalSimple.find("IGI::PLAYER::") == std::string::npos);
assert(externalSimple.find("IGI::PLAYER::") == std::string::npos);
assert(internalSimple.find("WeaponHelpers") != std::string::npos);
assert(externalSimple.find("CameraHelpers") != std::string::npos);
```

- [ ] **Step 2: Migrate menus and write README paragraphs**

For each public method, document its typed native, required runtime/object/context, return/failure meaning, and one practical modding use. Do not document private helpers, raw offsets, or unavailable speed/health controls as public capabilities.

- [ ] **Step 3: Run format and contracts**

Run: `python3 tools/format_cpp.py --check && python3 tools/igi_runtime_harness.py --verify-menu-helpers --lenient`

Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add demos tests/MenuManagerTests.cpp
git commit -m "docs(demos): document impact helper classes"
```

### Task 6: Windows x86 build and Unicorn verification

**Files:**
- Verify: `demos/helpers/menu_helpers.cpp`
- Verify: `build-win32-release/igi-simple-mod-internal.dll`
- Verify: `build-win32-release/igi-simple-mod-external.dll`

- [ ] **Step 1: Build all affected Windows x86 targets**

Run:

```bash
tools/build-win32-dll.sh --release --target igi_simple_mod_menu
tools/build-win32-dll.sh --release --target igi_advanced_mod_menu
tools/build-win32-dll.sh --release --target igi_simple_mod_menu_external
tools/build-win32-dll.sh --release --target igi_advanced_mod_menu_external
```

Expected: every DLL target links successfully.

- [ ] **Step 2: Run Unicorn harness only**

Run:

```bash
IGI_EXE=/Users/haseeb-mir/Downloads/IGI/igi.exe \
IGI_DLL=build-win32-release/igi-simple-mod-external.dll \
python3 tools/igi_runtime_harness.py --verify-menu-helpers
```

Expected: supported initialization/native-contract checks pass; record any explicit live-game-only skips without claiming rendered behavior.

- [ ] **Step 3: Final hygiene and commit**

Run: `python3 tools/format_cpp.py --check && git diff --check && git status --short`

```bash
git add demos/helpers demos README.md tests
git commit -m "test(demos): validate impact helper API"
```
