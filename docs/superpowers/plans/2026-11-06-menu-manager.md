# MenuManager Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Provide a reusable, renderer-independent MenuManager that schedules feature callbacks on the verified IGI 1 game thread.

**Architecture:** `MenuManager` owns menus, selection state, and input repeat state. Typed `MenuItem` variants mutate only their own values; action callbacks are forwarded to `igi::Scheduler::enqueueOnce`, so game-facing work cannot execute on the caller/input thread. A future internal renderer and ImGui renderer consume the same immutable snapshot API.

**Tech Stack:** C++20, existing `igi::Scheduler`, CMake/CTest; no UI or graphics dependency in this slice.

## Global Constraints

- Target only the verified Windows x86 IGI 1 executable profile.
- Never use `Draw`, `MenuManager_Draw`, or `TextPrintDetour()` as a scheduler hook.
- Menu callbacks that affect game state must run through `Scheduler::enqueueOnce`.
- No public arbitrary memory API, process attachment, scanner, or trainer interface.
- Keep MenuManager independent of demo modifications and render backends.

---

### Task 1: Define menu model and public snapshot API

**Files:**
- Create: `include/IGI/MenuManager.hpp`
- Create: `src/IGI/MenuManager.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/MenuManagerTests.cpp`

**Interfaces:**
- Consumes: `igi::Scheduler` from `include/IGI/Scheduler.hpp`.
- Produces: `igi::MenuManager`, `igi::MenuItem`, `igi::MenuSnapshot`, `igi::MenuItemKind`, and `igi::MenuInput`.

- [ ] **Step 1: Write the failing model test**

```cpp
igi::Scheduler scheduler;
igi::MenuManager menuManager(scheduler);
menuManager.createMenu("main", "Main");
menuManager.addInfo("main", "Status", "Ready");
const auto snapshot = menuManager.snapshot();
assert(snapshot.title == "Main");
assert(snapshot.items.size() == 1);
assert(snapshot.items[0].kind == igi::MenuItemKind::Info);
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --target igi_sdk_tests && ctest --test-dir build --output-on-failure`

Expected: compilation failure because `IGI/MenuManager.hpp` does not exist.

- [ ] **Step 3: Implement the minimal model**

```cpp
enum class MenuItemKind { Action, Toggle, Integer, Float, List, Information, Separator, Submenu };
struct MenuSnapshot { std::string title; std::vector<MenuItemSnapshot> items; std::size_t selectedIndex; bool visible; };
class MenuManager { public: explicit MenuManager(Scheduler&); void createMenu(std::string id, std::string title); void addInfo(std::string_view menuId, std::string label, std::string value); MenuSnapshot snapshot() const; };
```

Keep all menu lookup failures non-throwing and return `false` from mutation methods when an id is unknown.

- [ ] **Step 4: Run the model test to verify it passes**

Run: `cmake --build build -j2 && ctest --test-dir build --output-on-failure`

Expected: all existing tests and the new MenuManager model test pass.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt include/IGI/MenuManager.hpp src/IGI/MenuManager.cpp tests/MenuManagerTests.cpp
git commit -m "feat: add renderer-independent menu model"
```

### Task 2: Add navigation and typed item behavior

**Files:**
- Modify: `include/IGI/MenuManager.hpp`
- Modify: `src/IGI/MenuManager.cpp`
- Modify: `tests/MenuManagerTests.cpp`

**Interfaces:**
- Consumes: `MenuManager::createMenu`, `MenuManager::snapshot` from Task 1.
- Produces: `MenuManager::addAction`, `addToggle`, `addInteger`, `addFloat`, `addList`, `addSubmenu`, and `handleInput(MenuInput)`.

- [ ] **Step 1: Write failing navigation tests**

```cpp
menuManager.addToggle("main", "God mode", false, [](bool) {});
menuManager.addInteger("main", "Distance", 10, 1, 20, 1, [](int) {});
menuManager.handleInput(igi::MenuInput::Down);
menuManager.handleInput(igi::MenuInput::Confirm);
assert(menuManager.snapshot().items[1].boolValue);
menuManager.handleInput(igi::MenuInput::Down);
menuManager.handleInput(igi::MenuInput::Right);
assert(menuManager.snapshot().items[2].numberValue == 11.0);
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build -j2 && ctest --test-dir build --output-on-failure`

Expected: compilation failure because typed insertion and input handling do not exist.

- [ ] **Step 3: Implement navigation semantics**

```cpp
enum class MenuInput { ToggleVisibility, Up, Down, Left, Right, Confirm, Back };
bool MenuManager::handleInput(MenuInput input);
```

Skip separators and information rows during selection. Clamp integers/floats to their declared ranges. Cycle list selectors in both directions. `Confirm` toggles booleans, invokes actions, and enters submenus; `Back` returns to the parent menu or hides the root menu.

- [ ] **Step 4: Run tests to verify behavior**

Run: `cmake --build build -j2 && ctest --test-dir build --output-on-failure`

Expected: navigation, clamping, list wrapping, and submenu/back tests pass.

- [ ] **Step 5: Commit**

```bash
git add include/IGI/MenuManager.hpp src/IGI/MenuManager.cpp tests/MenuManagerTests.cpp
git commit -m "feat: add menu navigation and typed items"
```

### Task 3: Schedule feature callbacks on the game thread

**Files:**
- Modify: `src/IGI/MenuManager.cpp`
- Modify: `tests/MenuManagerTests.cpp`
- Modify: `docs/architecture.md`

**Interfaces:**
- Consumes: `Scheduler::enqueueOnce`, `Scheduler::runPendingFromGameHook`, and all typed item callbacks from Task 2.
- Produces: callback execution semantics: mutations enqueue callbacks; callbacks do not run during `handleInput`.

- [ ] **Step 1: Write the failing scheduler-boundary test**

```cpp
int calls = 0;
menuManager.addAction("main", "Apply", [&] { ++calls; });
menuManager.handleInput(igi::MenuInput::Confirm);
assert(calls == 0);
scheduler.runPendingFromGameHook();
assert(calls == 1);
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build -j2 && ctest --test-dir build --output-on-failure`

Expected: assertion failure because the callback currently executes inline or is not scheduled.

- [ ] **Step 3: Enqueue every feature callback**

```cpp
scheduler_.enqueueOnce([callback = item.action] { callback(); });
```

Capture the changed value by value for toggle, integer, float, and list callbacks. If `enqueueOnce` returns zero, preserve the visual value but return `false` from `handleInput` and log a warning.

- [ ] **Step 4: Run scheduler-boundary tests**

Run: `cmake --build build -j2 && ctest --test-dir build --output-on-failure`

Expected: callbacks run exactly once only after the scheduler is entered through its game-hook method.

- [ ] **Step 5: Document and commit**

```bash
git add src/IGI/MenuManager.cpp tests/MenuManagerTests.cpp docs/architecture.md
git commit -m "feat: schedule menu callbacks on game thread"
```

### Task 4: Add input repeat and safe shutdown

**Files:**
- Modify: `include/IGI/MenuManager.hpp`
- Modify: `src/IGI/MenuManager.cpp`
- Modify: `tests/MenuManagerTests.cpp`
- Create: `docs/menu-manager.md`

**Interfaces:**
- Consumes: `MenuManager::handleInput` from Task 2 and callback scheduling from Task 3.
- Produces: `MenuManager::updateInput(MenuInput, std::chrono::milliseconds)` and `MenuManager::shutdown()`.

- [ ] **Step 1: Write failing repeat and shutdown tests**

```cpp
menuManager.updateInput(igi::MenuInput::Down, 0ms);
const auto first = menuManager.snapshot().selectedIndex;
menuManager.updateInput(igi::MenuInput::Down, 50ms);
assert(menuManager.snapshot().selectedIndex == first);
menuManager.shutdown();
assert(!menuManager.snapshot().visible);
assert(!menuManager.handleInput(igi::MenuInput::Confirm));
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build -j2 && ctest --test-dir build --output-on-failure`

Expected: compilation failure because repeat/update and shutdown APIs do not exist.

- [ ] **Step 3: Implement deterministic repeat and shutdown**

```cpp
void MenuManager::updateInput(MenuInput input, std::chrono::milliseconds elapsed);
void MenuManager::shutdown() noexcept;
```

Use a 300 ms initial delay and 100 ms repeat interval. Clear all menus and callbacks on shutdown, hide the menu, and reject all later input.

- [ ] **Step 4: Run complete tests and cross-build**

Run: `cmake --build build -j2 && ctest --test-dir build --output-on-failure && cmake --build build-win32 -j2`

Expected: host tests pass and the Windows x86 DLL still links.

- [ ] **Step 5: Commit**

```bash
git add include/IGI/MenuManager.hpp src/IGI/MenuManager.cpp tests/MenuManagerTests.cpp docs/menu-manager.md
git commit -m "feat: add menu input repeat and shutdown"
```

## Self-review

- Spec coverage: model, all required basic item categories, navigation, scheduler dispatch, input repeat, rendering independence, and shutdown each have a task.
- Placeholder scan: no unbounded work or deferred implementation is included.
- Type consistency: the `MenuManager`, `MenuInput`, `MenuSnapshot`, and scheduler APIs are defined before later tasks consume them.
