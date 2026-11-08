# Impact Mod Helpers Design

## Goal

Replace the flat demo helper API with a class-based, high-impact Project I.G.I.
modding API. The API serves player, weapons, mission/world, vehicles, camera,
and AI work using only typed, evidence-backed IGI wrappers. It deliberately
does not grow a collection of cosmetic convenience calls.

## Scope

Public helper classes live in `demos/helpers/menu_helpers.hpp`; all private
state, validation, native adaptation, and render implementation live in
`demos/helpers/menu_helpers.cpp`. Demo source files consume classes only and
must not define a gameplay helper or call a gameplay native chain directly.

The public classes are:

| Class | Focus |
| --- | --- |
| `PlayerHelpers` | Local-player actions, input, look, visual feedback, state queries |
| `WeaponHelpers` | Weapon pickup, switching, animation, and verified weapon queries |
| `WorldHelpers` | Mission flow, level weather, game pause/display/world diagnostics |
| `VehicleHelpers` | Only typed vehicle/heli operations for objects the caller supplies |
| `CameraHelpers` | Retail camera/view operations and third-person HUD support |
| `AiHelpers` | Explicit-context AI script operations; no fabricated generic enemy API |
| `AudioHelpers` | Existing audio helpers retained but secondary to gameplay helpers |

The target is approximately fifty public methods across these classes. The
priority order is player movement/action, weapons, enemy behavior, camera,
mission/world flow, and vehicles. A method may be a query only when it exposes
state needed to build or validate one of those mod features; otherwise it must
have a meaningful gameplay effect.

## Verified-native boundary

Every public method calls only a typed `IGI::` wrapper already declared in
`src/natives/mvp_natives.hpp`. It performs the game-thread check in private
code and rejects missing live objects. No demo code may use a raw game-memory
offset. A method is not added merely because a name appears in the hash list.

AI wrappers in the catalog use the retail QScript `(ScriptValue*, data, args)`
ABI. `AiHelpers` exposes those only through an explicit `AiScriptContext`
provided by a valid game-script callback; generic menus cannot invoke them.
This prevents calls with invented `data` or `args` pointers.

Vehicle helpers require a typed `IGI::Car*` or `IGI::Heli*` returned from a
verified caller/native. They do not search object memory or reinterpret a
player pointer as a vehicle.

The current verified catalog exposes AI perception, alarm, invulnerability,
instant-death, death-animation, and event-priority operations through that
script context. It does not expose a verified direct NPC-speed, NPC-health,
vehicle-speed, vehicle-steering, or vehicle-health setter. Those methods are
not invented. The public API instead makes the supported high-impact calls
clear and leaves a documented extension point for new IGI-RE evidence.

## Camera and crosshair

`CameraHelpers::setThirdPerson` calls the retail `HUMAN::VIEW_CAM` path. The
previous atomic-only `isThirdPersonActive` is removed because it was merely a
record of a prior helper call. A truthful query is only exposed when a
verified native or retail state accessor exists. If the retail catalog lacks
such an accessor, the API returns an explicit unavailable result rather than
claiming the camera mode.

`renderThirdPersonCrosshair` is retained only after validating its exact retail
sprite/draw call sequence against the retail executable. It returns a draw
result and is called from both internal and external render loops. It never
uses a guessed camera offset as its activation condition.

## Public documentation

`demos/README.md` documents only public methods. Each method gets one concise
paragraph covering the native-backed behavior, required runtime/object/context,
failure behavior, and a minimal use case. Header comments are one line or are
omitted when the method name is self-explanatory; long instructional blocks do
not belong in C++ declarations.

## Testing and delivery

Tests enforce the class declarations, the absence of flat legacy helpers, and
the expected typed-wrapper calls. The targeted Windows x86 DLLs build with
`tools/build-win32-dll.sh`. `tools/igi_runtime_harness.py` runs against the
retail executable and the built DLL for its supported initialization/native
contract coverage. No Wine/game injection is part of this work.

## Exclusions

- No raw offsets, NOP patches, or guessed health/free-camera/AI access.
- No AI call without a valid retail QScript context.
- No live injection, Wine launch, or in-game claim from build/harness results.
- No release binary directory; artifacts remain build outputs unless deployed
  by an explicitly requested workflow.
