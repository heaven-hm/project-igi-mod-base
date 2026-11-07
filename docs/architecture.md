# IGI 1 MVP architecture

Headers and implementations are grouped by responsibility under `src/`,
including `src/ui`, `src/runtime`, `src/natives`, and `src/logging`. The shared
menu host lives in `src/mod_base`; mods include `natives/mvp_natives.hpp` directly.

## Source roles

| Source | Role in this SDK | Current treatment |
| --- | --- | --- |
| `project-igi-natives` | historical design input | Not copied into the build; the active implementation is grouped under `src/`. |
| Neonix Modding Team `IGI-RE` | IGI 1 native address/name/signature research | Imported as catalogued metadata only; no implementation source is compiled. |
| BigBaseV2 | runtime, script-scheduler, hook/lifecycle design reference | Architectural reference only; no code copied. |
| GTLibCpp | optional generic utility reference | Not linked or exposed; its process-memory surface conflicts with the SDK's native-first public boundary. |
| D3D7Imgui | DirectX 7 ImGui backend reference | Selected for a later audited integration; not yet vendored or linked. |

## Runtime path

```text
Host calls IGI_SDK_Initialize
        ↓
HookManager validates IGI 1 build bytes at Game_RunHandler
        ↓
Game_RunHandler trampoline executes original game function
        ↓
Scheduler binds this thread on its first observed callback
        ↓
Scheduler executes due SDK tasks with exception containment
```

`Flow_RunHandler` and `Game_Update` are catalogued lifecycle references only.
The MVP never schedules through `Draw`, `MenuManager_Draw`, or
`TextPrintDetour()`.

Menu input only updates the menu's visual state and enqueues feature callbacks;
those callbacks execute only when `Scheduler::runPendingFromGameHook()` enters
the verified game-thread boundary.

## Native states

- **Catalogued**: a Neonix source record is present.
- **Verified**: target executable, signature, calling convention, and behavior
  have evidence for this supported build.
- **Exposed**: a verified record has a documented public wrapper.

No catalogued record becomes invocable merely because it has an address or a
source signature.
