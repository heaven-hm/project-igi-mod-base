# Project IGI Natives SDK
## Technical Architecture and Implementation Plan

**Repository:** `heaven-hm/project-igi-natives-sdk`  
**Initial release:** v0.1.0  
**Primary language:** C++20  
**Primary target:** Original Project IGI 1 and IGI 2 — Windows x86  
**Project category:** Single-player game modding SDK and runtime framework  
**Architecture reference:** GTA V BigBaseV2

---

# 1. Project Purpose

Project IGI Natives SDK is a reusable C++ framework for creating single-player modifications for Project IGI 1 and Project IGI 2.

The project takes architectural inspiration from GTA V BigBaseV2 and adapts its useful framework concepts to the Project IGI game engine.

It should provide:

- Native function invocation and wrappers.
- Native API catalogue and documentation.
- Player, soldier, weapon, camera and world APIs.
- Game-thread scheduling and task management.
- Runtime lifecycle management.
- Hook management for necessary game integration.
- Logging and exception handling.
- Configuration management.
- Reusable MenuManager library.
- ImGui integration.
- Internal and external-style demonstration interfaces.
- Examples and developer documentation.

The SDK must make it straightforward for another developer to create a C++ mod without having to implement native invocation, menu infrastructure, scheduling, logging and runtime management independently.

## 1.1 Single-player-only policy

This framework is exclusively intended for offline, single-player modding, experimentation and game customization.

It must not provide multiplayer cheating functionality.

Out of scope:

- Multiplayer modifications.
- Online cheating.
- Aimbots and automated targeting.
- ESP or player tracking.
- Anti-cheat bypasses.
- Multiplayer manipulation.
- Unauthorized access to other players' sessions.

The project should clearly state this policy in its README and documentation.

---

# 2. Source Projects and Migration

The SDK will have one primary repository and one main codebase.

## 2.1 Primary source: project-igi-natives

Repository:

https://github.com/heaven-hm/project-igi-natives

Action: Fork this repository and evolve the fork into `project-igi-natives-sdk`.

Preserve and migrate the existing C++ implementation.

Existing functionality to audit:

- Native invocation.
- Player and soldier APIs.
- Camera functionality.
- Weapon functionality.
- Memory-related implementation needed internally for legitimate native invocation.
- Game resource functionality.
- Graph functionality.
- Hook implementation.
- Existing scheduler and FiberPool.
- Common types and definitions.
- Existing 100 native methods.
- Existing native metadata and documentation.

Do not discard the existing 100 native methods.

Do not rewrite working functionality without a technical reason.

## 2.2 IGI-RE native implementations

Repository reference:

https://github.com/SlickDaddy/IGI-RE

Use IGI-RE as the primary source of additional game-native methods.

Tasks:

1. Inspect its C++ source.
2. Identify all available native methods.
3. Compare its methods with the existing SDK methods.
4. Identify duplicate implementations.
5. Compare function signatures and calling conventions.
6. Separate IGI 1 and IGI 2 functionality.
7. Verify native implementations against the actual game.
8. Integrate compatible methods into the SDK.

The native catalogue should retain all useful research information, including methods that have not yet been verified.

Do not treat an unverified method as a working public SDK API.

## 2.3 GTLibCpp

Repository:

https://github.com/heaven-hm/GTLibCpp

Optional source for reusable utilities.

Inspect it for useful generic components, but do not make the SDK dependent on the entire library.

Only reuse a component when:

- It solves an actual SDK requirement.
- It does not duplicate existing functionality.
- Its licence permits reuse.
- It does not introduce unnecessary dependencies.

## 2.4 BigBaseV2

Reference:

https://github.com/Pocakking/BigBaseV2

Use it as an architectural reference, not as the Project IGI codebase.

Study its approach to:

- Runtime lifecycle.
- Script scheduling.
- Fiber management.
- Menu architecture.
- ImGui.
- Logging.
- Configuration.
- Hook management.
- Modular feature organization.

Implement the concepts using Project IGI's actual game engine and runtime.

## 2.5 DirectX 7 ImGui

Reference:

https://github.com/AndenDev/D3D7Imgui

Evaluate this project for ImGui integration with the original IGI DirectX 7 renderer.

Do not assume standard modern DirectX ImGui backends are compatible with DirectX 7.

Verify rendering, input handling, device lifecycle and cleanup.

---

# 3. Repository and Branch Strategy

Repository:

`heaven-hm/project-igi-natives-sdk`

Required branches:

- `master`: stable and release-ready code.
- `develop`: main integration branch.
- `feature/*`: individual feature development.
- `bugfix/*`: targeted bug fixes.
- `release/*`: release preparation where necessary.

Branch workflow:

`feature/*` → `develop` → `master`

Rules:

1. Do not develop directly on master.
2. Features should be developed independently.
3. Every feature must be reviewed before integration.
4. Build and verification must pass before merging.
5. Version tags must identify stable releases.
6. Keep the existing source history and attribution where practical.

---

# 4. Overall Architecture

The SDK should be divided into independent modules with clear responsibilities.

## 4.1 Core

Responsibilities:

- SDK initialization.
- Runtime state.
- Module registration.
- Lifecycle management.
- Shutdown.
- Shared types and interfaces.

## 4.2 Native System

Responsibilities:

- Native registry.
- Native invocation.
- Native wrappers.
- Native metadata.
- Game-version-specific definitions.
- Native validation and error reporting.

The native system must be the primary interface through which game modifications access game functionality.

## 4.3 Game API

Provide organized public APIs:

- `Player`
- `Human`
- `Soldier`
- `Weapon`
- `Camera`
- `World`
- `Weather`
- `Audio`
- `Time`
- `Physics`
- `Level`
- `Resources`
- `AI`

Expose only functionality backed by verified game natives.

## 4.4 Runtime

Responsibilities:

- Runtime initialization.
- Game-thread identification.
- Frame updates.
- Task scheduling.
- Safe task execution.
- Runtime shutdown.
- Exception containment.

## 4.5 Hook Manager

Responsibilities:

- Register required hooks.
- Install and remove hooks.
- Manage hook lifetime.
- Prevent duplicate installation.
- Restore original execution paths during shutdown.

Only implement hooks necessary for legitimate game integration, rendering, input and frame scheduling.

## 4.6 Memory boundary

The SDK must distinguish internal implementation requirements from public modding functionality.

Memory operations may exist internally where required for:

- Native invocation.
- Hook installation.
- Runtime integration.
- Access to verified engine interfaces.

Do not expose a general-purpose memory manipulation API to mod developers.

Do not build a public memory editor, arbitrary memory writer, trainer or memory-scanning toolkit.

Public game modifications should use native methods.

---

# 5. Native Catalogue and API Coverage

The existing project already contains approximately 100 native methods.

These must be retained and integrated.

The IGI-RE source should be audited to expand the available native catalogue.

Do not impose an arbitrary limit of 50 or 100 methods.

The SDK should distinguish three states:

1. **Catalogued:** Method is identified and documented.
2. **Verified:** Signature and behaviour have been checked against the target game.
3. **Exposed:** Method has a usable, documented SDK wrapper.

Version 0.1 requirements:

- Preserve all existing 100 native methods.
- Import all eligible native research from IGI-RE.
- Deduplicate matching methods.
- Record verification status.
- Implement all verified methods needed by the ten demonstration modifications.
- Include additional useful player, weapon, camera, world and environment methods where verification permits.

The catalogue may contain thousands of entries, but only verified and usable functions should be exposed as stable APIs.

Keep IGI 1 and IGI 2 definitions separate.

Native metadata should record:

- Native name.
- Game version.
- Address or invocation identifier where applicable.
- Function signature.
- Calling convention.
- Parameters.
- Return type.
- Verification status.
- Source attribution.
- Supporting notes.

Do not invent missing signatures or addresses.

---

# 6. MenuManager Library

Create a reusable MenuManager library inside the SDK.

This is an important deliverable, not just a demonstration menu.

The library should allow developers to create menu interfaces for internal single-player game modifications without implementing menu navigation and item management themselves.

## 6.1 Required menu components

- Menu.
- Submenu.
- Menu title.
- Menu item.
- Toggle.
- Boolean option.
- Integer option.
- Floating-point option.
- Slider.
- Numeric adjustment.
- List selector.
- Enumeration selector.
- Action button.
- Information item.
- Separator.
- Description and help text.

## 6.2 Input handling

Support:

- Keyboard navigation.
- Configurable navigation keys.
- Confirm and cancel.
- Back navigation.
- Menu visibility toggle.
- Input repeat handling.

## 6.3 Menu architecture

Separate:

- Menu state.
- Menu navigation.
- Menu item definitions.
- Input handling.
- Rendering.
- Feature callbacks.

Menu items must call public SDK APIs rather than implement native functionality themselves.

## 6.4 Reusability

A developer should be able to:

1. Initialize MenuManager.
2. Create a menu.
3. Add submenus.
4. Register menu items.
5. Bind items to SDK APIs.
6. Display the menu.
7. Shut it down safely.

MenuManager must not depend on the ten demo modifications.

---

# 7. ImGui Integration

Provide a separate ImGui UI implementation.

Reference:

https://github.com/AndenDev/D3D7Imgui

Responsibilities:

- Initialize the rendering backend.
- Handle DirectX 7 device integration.
- Render menu windows.
- Process user input.
- Display SDK information.
- Register modification controls.
- Release rendering resources safely.

Keep the UI independent of the game APIs.

Both MenuManager and ImGui must call the same SDK functionality.

The project should not duplicate modification logic between the two interfaces.

---

# 8. Two Demonstration Implementations

Version 0.1 must provide two complete demonstration interfaces.

Both should demonstrate the same ten single-player modifications.

## 8.1 Internal MenuManager demo

Requested output name:

`IGI-MenuManager-Internal.dll`

This demo should:

- Run inside the original game process.
- Use the SDK runtime.
- Use the reusable MenuManager library.
- Access game functionality through native APIs.
- Demonstrate all ten modifications.
- Support menu initialization and safe shutdown.

## 8.2 ImGui external-style demo

Requested module name:

`IGI-MenuManager-External.dll`

This demo should:

- Use ImGui.
- Demonstrate the same ten modifications.
- Reuse the SDK APIs.
- Keep the UI implementation separate from MenuManager.
- Provide independent UI controls and logging.

Important implementation distinction:

A DLL is not an independent external application. If this demo is genuinely out-of-process, provide a companion executable and a documented communication mechanism. If it is loaded into the game process, it is an internal plugin with an ImGui interface.

Do not claim external-process support without implementing and testing the actual architecture.

Both demos must share the same native functionality rather than implement their own game logic.

---

# 9. Ten Demonstration Modifications

Version 0.1 must demonstrate these features.

## Player and environment

1. Weather modification.
2. Environmental sound modification.
3. Game gravity adjustment.
4. Time-of-day modification.
5. Frame-rate or frame-setting modification where supported by verified game functionality.

## Camera

6. Enable third-person camera.
7. Adjust third-person camera distance.
8. Adjust camera position.
9. Adjust camera rotation.

## Weapons

10. Unlock or provide the available single-player weapons using verified game-native functionality.

The weapon feature should allow developers to demonstrate weapon selection and availability without requiring a memory editor.

Each modification must have:

- A documented native implementation.
- A menu item.
- An enable/disable or selection mechanism where appropriate.
- Safe handling of invalid values.
- Logging.
- A demonstration description.

If a requested behaviour cannot be implemented through verified game-native methods, document the limitation rather than silently substituting memory manipulation.

---

# 10. Scheduler and Game-Thread Execution

Audit the existing FiberPool implementation.

The current implementation must not depend on `TextPrintDetour()` as its only task execution point.

HUD text rendering is not a reliable scheduler trigger.

Implement a stable game-thread update mechanism.

Required operations:

- Enqueue.
- EnqueueOnce.
- EnqueueEveryFrame.
- EnqueueDelayed.
- Cancel.
- Clear.
- IsGameThread.
- RunPending.

Requirements:

- Correct game-thread execution.
- Safe task cancellation.
- No uncontrolled detached threads.
- Exception containment.
- Safe shutdown.
- Defined task lifetime.
- Clear scheduling semantics.

Game-native operations that modify game state must execute in the appropriate game execution context.

---

# 11. Logging and Exception Handling

Logging must be part of the framework from the beginning.

Provide severity levels:

- Trace.
- Debug.
- Info.
- Warning.
- Error.
- Critical.

Log important events:

- Runtime initialization.
- Runtime shutdown.
- Native invocation failures.
- Hook installation and removal.
- Scheduler errors.
- Menu initialization.
- Feature activation.
- Invalid configuration.
- Exception details.

Logs should include useful context, such as timestamps, subsystem names, severity and relevant error information.

Do not silently swallow exceptions.

Prevent an exception in one optional modification from unnecessarily crashing the entire SDK runtime.

Avoid logging every frame or generating excessive output.

---

# 12. Coding Standards

Use C++20.

Use descriptive variable, type and function names.

Examples:

- `index`, not `i` where a descriptive name is clearer.
- `pointer`, not `ptr`.
- `playerPosition`, not `pos`.
- `nativeFunctionAddress`, not `address` when more context is needed.
- `menuItemCount`, not `count` when the context is ambiguous.

Requirements:

- Consistent naming conventions.
- Small, focused classes and functions.
- Clear ownership and lifetime rules.
- RAII for resources.
- Avoid unnecessary global state.
- Avoid unnecessary abstractions.
- Avoid duplicate implementations.
- Minimal meaningful comments.
- No excessive documentation comments.
- No unnecessary Python runtime dependencies.

The code should be readable and maintainable by other C++ developers.

---

# 13. Proposed Repository Structure

```text
project-igi-natives-sdk/
│
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── LICENSE
├── LICENSES/
├── NOTICE
├── CHANGELOG.md
├── CONTRIBUTING.md
├── SECURITY.md
│
├── .github/
│   ├── workflows/
│   ├── ISSUE_TEMPLATE/
│   └── PULL_REQUEST_TEMPLATE.md
│
├── docs/
│   ├── architecture.md
│   ├── getting-started.md
│   ├── native-api.md
│   ├── native-verification.md
│   ├── menu-manager.md
│   ├── imgui.md
│   ├── runtime.md
│   ├── logging.md
│   ├── building.md
│   ├── troubleshooting.md
│   ├── licensing.md
│   └── single-player-policy.md
│
├── include/
│   └── IGI/
│       ├── Core/
│       ├── Native/
│       ├── Player/
│       ├── Human/
│       ├── Soldier/
│       ├── Weapon/
│       ├── Camera/
│       ├── World/
│       ├── Weather/
│       ├── Audio/
│       ├── Time/
│       ├── Physics/
│       ├── Level/
│       ├── Resources/
│       ├── Runtime/
│       ├── Scheduler/
│       ├── Hooks/
│       ├── Logging/
│       ├── Configuration/
│       └── MenuManager/
│
├── src/
│   ├── Core/
│   ├── Native/
│   ├── Player/
│   ├── Human/
│   ├── Soldier/
│   ├── Weapon/
│   ├── Camera/
│   ├── World/
│   ├── Weather/
│   ├── Audio/
│   ├── Time/
│   ├── Physics/
│   ├── Level/
│   ├── Resources/
│   ├── Runtime/
│   ├── Scheduler/
│   ├── Hooks/
│   ├── Logging/
│   ├── Configuration/
│   └── MenuManager/
│
├── data/
│   ├── IGI1/
│   └── IGI2/
│
├── third_party/
│   ├── minhook/
│   └── imgui/
│
├── demos/
│   ├── internal-menumanager/
│   │   ├── src/
│   │   └── CMakeLists.txt
│   │
│   └── imgui/
│       ├── src/
│       └── CMakeLists.txt
│
├── tests/
│   ├── Core/
│   ├── Native/
│   ├── Scheduler/
│   ├── MenuManager/
│   └── Configuration/
│
└── examples/
    ├── basic-mod/
    ├── player-modification/
    ├── camera-modification/
    └── weapon-modification/
```

The final layout may be adjusted after auditing the existing repository. Do not move everything merely to achieve a visually perfect directory tree.

---

# 14. Build System

Use CMake and C++20.

Primary target:

- Windows x86.
- Original retail Project IGI.
- Internal plugin DLL.

Build outputs should be clearly separated by game version, configuration and architecture.

Do not mix 32-bit original-game binaries with 64-bit host applications.

Keep build configurations explicit:

- Debug.
- Release.

Do not introduce macOS, Linux or recompilation backends into v0.1 unless there is a verified runtime interface and a working implementation.

---

# 15. Testing and Verification

Testing must verify real functionality, not merely successful compilation.

## Core tests

- Runtime initialization.
- Runtime shutdown.
- Module registration.
- Configuration loading.
- Logging.
- Error handling.

## Native tests

- Native signature validation.
- Duplicate detection.
- Game-version separation.
- Invocation behaviour.
- Return value and parameter validation.

## Scheduler tests

- Task enqueueing.
- One-time execution.
- Repeated execution.
- Delayed execution.
- Cancellation.
- Exception handling.
- Correct game-thread execution.

## MenuManager tests

- Menu creation.
- Submenu navigation.
- Item registration.
- Toggle behaviour.
- Slider behaviour.
- Input navigation.
- Callback execution.
- Shutdown.

## In-game verification

Test both demos in offline single-player sessions.

Verify each of the ten modifications independently.

Record which native methods are used and which game versions are supported.

---

# 16. Licensing, Attribution and Credits

Do not blindly replace the existing licence.

Before migration, audit:

- `project-igi-natives` licence.
- Existing file-level copyright notices.
- IGI-RE licence and source attribution.
- GTLibCpp licence, if any code is reused.
- BigBaseV2 licence.
- D3D7Imgui licence.
- MinHook licence.
- Dear ImGui licence.
- Other third-party dependencies.

Preserve existing permissions and notices.

Create:

- `LICENSE`
- `LICENSES/`
- `NOTICE`
- `docs/licensing.md`
- `CREDITS.md`

Every reused implementation must have appropriate attribution.

Do not claim ownership of third-party code.

Do not redistribute original IGI game assets or copyrighted game files.

If a source project's licence does not permit code reuse, use it as a research reference and independently implement the required functionality.

---

# 17. README and Developer Documentation

The README must clearly explain:

1. What Project IGI Natives SDK is.
2. What it is used for.
3. Its single-player-only purpose.
4. What developers can build with it.
5. Which game versions are supported.
6. How to build the SDK.
7. How to load the internal demo.
8. How to use MenuManager.
9. How to use ImGui.
10. How to create a custom modification.
11. How to use native APIs.
12. How to contribute.
13. Credits and licences.

Include screenshots of both demonstration interfaces.

Keep the README concise and practical. Put detailed technical explanations in `docs/`.

---

# 18. Development Skills and Engineering Workflow

Use the following engineering references while designing, implementing and reviewing the project.

## Heaven Skills

Repository:

https://github.com/haseeb-heaven/heaven-skills

Apply relevant skills from:

- Codebase design.
- Research.
- Diagnosing bugs.
- Test-driven development.
- Code review.
- Removing unnecessary or low-quality generated code.

## Matt Pocock Skills

Repository:

https://github.com/mattpocock/skills

Use its engineering and documentation guidance where applicable, especially:

- Writing technical documentation.
- Architecture decision records.
- Clear implementation instructions.
- Maintainable engineering practices.
- Structured review.

Do not force unrelated skills into this C++ project. Use the relevant instructions for each task.

Maintain architectural decisions in `docs/decisions/` or an equivalent ADR directory.

---

# 19. Implementation Milestones

## Milestone 1 — Repository foundation

- Fork existing project.
- Establish `master` and `develop`.
- Set up feature branch workflow.
- Audit existing code.
- Audit all licences.
- Establish CMake and C++20 configuration.
- Create the initial architecture.

## Milestone 2 — Native integration

- Preserve existing 100 native methods.
- Inspect IGI-RE methods.
- Import eligible methods.
- Deduplicate the catalogue.
- Separate IGI 1 and IGI 2.
- Verify methods required by the ten demos.
- Document native status.

## Milestone 3 — Core framework

- Runtime lifecycle.
- Native registry and invocation.
- Public game APIs.
- Internal memory boundary.
- Hook manager.
- Logging.
- Exception handling.
- Configuration.
- Scheduler correction.

## Milestone 4 — MenuManager library

- Menu architecture.
- Menu items.
- Submenus.
- Input handling.
- Navigation.
- Rendering interface.
- SDK callback integration.

## Milestone 5 — ImGui integration

- ImGui integration.
- DirectX 7 compatibility evaluation.
- Rendering lifecycle.
- Input integration.
- Logging and error handling.

## Milestone 6 — Two demonstration modules

- Internal MenuManager DLL.
- ImGui external-style demonstration.
- Ten single-player modifications.
- Shared SDK APIs.
- Independent UI implementations.

## Milestone 7 — Testing and documentation

- Unit tests.
- Integration tests.
- In-game verification.
- Developer examples.
- README.
- API documentation.
- Credits and licence notices.

## Milestone 8 — v0.1.0 release

Release only when the framework, both demos, documentation and verification requirements are satisfied.

---

# 20. Version 0.1 Acceptance Criteria

The release is complete when:

- [ ] Repository is `heaven-hm/project-igi-natives-sdk`.
- [ ] `master`, `develop` and feature branches are established.
- [ ] Existing 100 native methods are preserved.
- [ ] Eligible IGI-RE native methods are integrated.
- [ ] Native catalogue distinguishes catalogued, verified and exposed methods.
- [ ] IGI 1 and IGI 2 APIs are separated.
- [ ] Native invocation works.
- [ ] Core runtime works.
- [ ] Scheduler no longer depends solely on `TextPrintDetour()`.
- [ ] Public APIs use game-native functionality.
- [ ] No public arbitrary memory manipulation API exists.
- [ ] Logging and exception handling work.
- [ ] MenuManager is a reusable library.
- [ ] ImGui integration works with the selected rendering backend.
- [ ] Internal demo builds as `IGI-MenuManager-Internal.dll`.
- [ ] ImGui demo has a clearly documented loading architecture and output.
- [ ] Both demos demonstrate the same ten modifications.
- [ ] Offline single-player verification is complete.
- [ ] Documentation is usable by new developers.
- [ ] Licences and credits are preserved.
- [ ] v0.1.0 can be built from documented instructions.

---

# Final Project Definition

Project IGI Natives SDK is a BigBase-inspired, C++20 single-player modding framework for Project IGI 1 and Project IGI 2.

It combines verified game-native functionality, a reusable runtime, scheduling, logging, configuration, a reusable MenuManager library and an ImGui interface.

Its purpose is to give developers a well-structured foundation for building their own Project IGI single-player modifications and user interfaces.

It is not a multiplayer cheating framework, memory editor, trainer or game executable replacement.
