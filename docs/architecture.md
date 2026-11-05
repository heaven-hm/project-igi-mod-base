# IGI 1 MVP architecture

Public headers are grouped by responsibility using the BigBase-style module
paths `IGI/UI`, `IGI/Runtime`, `IGI/Natives`, and `IGI/Logging`. The original
flat headers remain compatibility entry points; the grouped headers forward to
the single implementation so the SDK does not maintain duplicate APIs.

## Source roles

| Source | Role in this SDK | Current treatment |
| --- | --- | --- |
| `project-igi-natives` | primary legacy SDK base | Preserved under `legacy/`; migrated module by module under its MIT licence. |
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
