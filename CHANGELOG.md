# Changelog

## 0.6.0 — Heaven release: humanplayer physics, vehicle control and menu stability

Release DLLs (all Win32 x86, inject one per igi.exe session):
`igi-heaven-mod-internal.dll` / `igi-heaven-mod-external.dll` (flagship),
`igi-heaven-mod-advanced-internal.dll` / `igi-heaven-mod-advanced-external.dll`
(+ player/NPC ground state and movement), plus the basic, simple and advanced
mod variants.

**humanplayer.qvm physics (Human inside Player):** move speed scale, jump speed,
jump height, air speed, damage scale and fence damage — read live from the retail
BSS, prefilled by running `HumanPlayer_LoadParameters` (the authored qvm values,
verified against `humanplayer-org.qvm`), applied immediately on selection with a
live read-back verified in the log, decimal authored units (17.5, 1.75, 3), and
one "Read Humanplayer" option that pushes the qvm defaults into both frontends.
Lean/peek parameters are intentionally not exposed.

**Teleport, exactly two paths:** "Teleport To Any Object / AI" (one merged list:
live NPCs, vehicles, world objects, landmarks, captured Level Start) and typed
X/Y/Z with a Metres / World Units switch (prefilled from the live player
position, stored in world units internally, ground-probed). Address-keyed
id-less objects are excluded — they crashed the teleport.

**Vehicles:** helicopter Stop Reverse, drive, yaw/pitch/roll and reset through
`Heli_Control`; tank drive presets through `Car_Control`; car Explode/Reset/Info
auto-adopt the only live car and report the live-car count.

**Menu stability:** one F5 press opens even during a level load; a level change
or restart reclaims the stale script session so the menu never reopens empty;
the close button unwinds through the retail leave path so the pause menu keeps
working after eject; eject keeps the DLL resident instead of freeing it under
the game thread; an attach whose widgets fail self-heals onto a fresh task-id
range; weapon drop no longer runs the world-drop path on id-less guns (the
crash).

**Verification:** `tools/run-unicorn-tests.sh` — Unicorn-only suite (contract
checks, frame dispatch, 7 heaven flows, 4 humanplayer-physics checks, 6
menu-helper checks), ctest removed; `tools/build-all.sh` builds and deploys all
10 DLLs.

## 0.5.3 — Teleport crash on id-less objects and typed XYZ unit consistency

- The game crashed teleporting to target #142191460 - an address-derived key for
  a task with no script id (dropped gun / vehicle part class). The probe
  succeeded and the game died right after. The merged picker now admits only
  script-identified targets (ids < 4096, plus the Level Start key), and the
  teleport action refuses address-keyed targets with a clear log line.
- Typed XYZ could mix units across axes: X/Y were edited in metres while Z held
  the previous world-units value, so the ground probe correctly refused
  (requested point far below terrain). The destination is now stored in world
  units internally; the boxes display it in the selected unit, edits convert
  into world units, a unit switch only re-scales the display, and Apply moves
  without re-converting. User edits are kept across refreshes (the old prefill
  overwrote them every frame).


## 0.5.2 — Two teleport paths: one picker, one typed XYZ with units

- The Player section had three pickers for the same move (Teleport To Target,
  Teleport To Landmark, Send Player To Target); sendPlayerToTarget literally
  called teleportPlayerToTarget plus a broadcast. They are one control now:
  "Teleport To Any Object / AI" - every live NPC, vehicle, world object, level
  landmark and the captured Level Start in a single list. "Send Player
  Position" is gone too; every teleport broadcasts the new position already.
- The typed XYZ path returned as "Teleport X/Y/Z" + "Position Units" +
  "Teleport To Position". The three boxes prefill from the live player position
  each level and re-scale when the unit changes. Units are Metres (default)
  or the native World Units - positions are linear, so the correct unit terms
  are world units vs metres (Euler angles describe rotation, not position).
  Metres convert by the retail 4096 units per metre before the move; the
  ground probe keeps the body above terrain either way. Heaven ships 70
  controls.


## 0.5.1 — ImGui Apply no longer double-converts human values

- The live log showed the two frontends feeding the adapter different string
  conventions: the QVM ListBox sends the displayed label ("17.5") while the
  ImGui Apply sent the raw internal integer ("3000"). With the new
  divisor-aware parser the ImGui path applied the divisor a second time -
  typing 20 became move scale 2000 (and 30 became 30 on the previous build).
  The ImGui Apply now sends the same display string the QVM does, so both
  frontends parse identically and the write/read-back equals exactly what was
  typed.


## 0.5.0 — Drop crash, qvm-unit parsing, one Read option and car adoption

- **Weapon drop crash:** the held gun's world task id can be -1 (unregistered);
  `Weapon_Drop` dereferences the world casing path there and crashed. The drop
  now removes an id-less gun from the inventory slots only (stop animation,
  clear slot, re-equip fallback) and runs the retail world drop only for
  registered ids.
- **Human values from the qvm, parsed in authored units:** the QVM ListBox sends
  the displayed label ("17.5") while the adapter parsed it as the raw internal
  integer, so picking "35" wrote 3.5 km/h. Both the Heaven and Advanced menus now
  convert the label back by the item's display divisor; the values you pick are
  what the log writes and reads back.
- **One Read option:** "Read Humanplayer (Load qvm Values)" replaces
  "Reload From humanplayer.qvm" + "Read Humanplayer Values". It runs
  HumanPlayer_LoadParameters and immediately pushes the authored
  humanplayer.qvm defaults into the input boxes on the internal and external
  menus (68 controls).
- **Vehicle explode/reset/info:** when no car is selected and exactly one car is
  live, it is adopted automatically (like the heli actions); the refusal now
  states how many live cars exist on the level, so "no car selected" is
  actionable.


## 0.4.9 — Close button no longer corrupts the retail MenuManager

- After closing the mod menu with its own Close button and ejecting, Escape no
  longer opened the retail pause menu. The generated screen ran
  "IGI_SDK_Cancel() - MenuManager_PopScreen(FALSE)"; our screen is placed with
  MenuManager_RequestScreen (replace, no push), so that PopScreen popped an
  empty stack and corrupted the MenuManager. The start-key close was immune
  because it runs the leave QVM (RequestScreen(returnScreenId) or the retail
  LeaveMenus reset).
- The generated screen now runs IGI_SDK_Cancel() only, and the Cancel callback
  performs the same leave-QVM unwind as the start-key close before marking the
  closing transition.
- HookManager::uninstall is idempotent after a retire-in-place: the second
  teardown used to wait 5 s for a frame-tick removal that can never run through
  a retired detour and then reported "hooks still resident", leaving the mod
  disabled; it is now an immediate no-op.


## 0.4.8 — Self-healing attach after a level change

- The log showed the injected screen task being created after a level change,
  but its option widgets never appeared (frame with no options, only after
  loading another level). The attach now counts the List/Toggle/Input widget
  tasks the screen script must have created (pageScreenId+1+row) and, when any
  are missing, rebuilds the whole session on a fresh task-id range (up to 2
  attempts) instead of showing a dead frame. A persistent failure is logged as
  CRIT with the missing ids.
- `IGI_SDK_FillList` now logs loudly when a fill produces zero entries - the
  exact "frame with no options" signature - so the next report pinpoints whether
  the screen's expression evaluation or the option data failed.


## 0.4.7 — Human inputs display the authored units

- The Human boxes no longer show opaque x10 integers ("175", "25"). A new
  `inputDisplayDivisor` renders every frontend in authored units: the QVM
  ListBox entries, the command list labels and the ImGui widget all show
  "17.5", "1.75", "3" (divisor 10 for speeds, 100 for scales), and the ImGui
  widget is a decimal input that converts back on Apply.
- The QVM sample entries are now meaningful fractions/multiples of the
  humanplayer.qvm baseline captured by `HumanPlayer_LoadParameters`
  (0, x0.5, x1, x2, x4) instead of arbitrary steps (0/25/50/75/1000), so picking
  "half" or "x2" scales the jump predictably. Picking 25 used to write 2.5 km/h
  against a 27 km/h authored height - effectively "the jump stopped working".
- Verified against the shipped `humanplayer-org.qvm`: 17.5 (jump speed km/h)
  and 3.0 (damage) are present as authored floats, matching the live baseline
  capture exactly.


## 0.4.6 — Unicorn-emulator verification for the human physics path

- New `--verify-human-physics` mode in `tools/igi_runtime_harness.py` runs the
  real retail igi.exe image in Unicorn and verifies, against the actual game
  bytes: all six `DefineHumanPlayerGeneral` globals round-trip at their BSS
  addresses (0x56E1E8..0x56E208), the km/h conversion constants
  (`kPerFrameHour` 0x5334A0, `kKmhToUnits` 0x5334A4) match the scale the menu
  uses (measured 37.9259 units per km/h per frame), and
  `HumanPlayer_LoadParameters` (0x4137A0) references
  `LOCAL:humanplayer/humanplayer.qsc` in the mapped image.
- New `tools/run-unicorn-tests.sh` runs the whole Unicorn suite directly - frame
  dispatch, heaven retail flows, human physics and menu-helper cleanup - with
  no ctest and no host-native test binaries. It sources `.env` for
  `IGI_EXE`/`IGI_DLL` and reports a single ALL PASSED / FAILED result.


## 0.4.5 — Human defaults come from HumanPlayer_LoadParameters

- The Human input boxes no longer start on hard-coded display defaults. The
  first Human refresh of a level runs the retail `HumanPlayer_LoadParameters`
  (0x4137A0), reads the six globals back, and logs
  "human physics baseline from HumanPlayer_LoadParameters: ..."; the boxes
  prefill from that baseline, which is exactly what humanplayer.qvm authored.
- The human prefill was trapped inside the teleport prefill, which returned
  early whenever the player-position read failed - the qvm values never reached
  the boxes and typed writes propagated guessed defaults instead. It is now an
  independent, level-scoped prefill (`prefillHumanControls`) hooked into
  `refreshLiveItems`.
- A qvm write still applies immediately with a live read-back verification, and
  the write lands on the freshly captured baseline, so the loaded values and
  the user's edits cannot race.


## 0.4.4 — Human physics live read-back

- Every humanplayer.qvm parameter write is now verified by an immediate live
  read-back: the retail jump state machine reads the globals directly on the
  next jump, so the read-back value is what the game will actually use. The log
  line changed from "human physics saved" to "human physics live (read back
  verified): ..." and a mismatch is reported as an error instead of a false
  success.
- A write performed before the qvm script has ever run this level (all globals
  zero) loads the authored baseline through HumanPlayer_LoadParameters first,
  matching the retail order (DefineHumanPlayerGeneral then the user's values).
  Loading after a write would revert it, so the load only happens on the
  never-loaded path; Reload From humanplayer.qvm remains the explicit restore.


## 0.4.3 — First-press F5, live human physics and menu surface trims

- **One F5 press is enough.** A press while the level is still loading used to
  be dropped: initialization logged "deferred until gameplay is active" and only
  another press retried. The host now stages the press (`autoOpenPending`) and
  retries initialization while the game loads, and the toggle stages the attach
  (`initialAttachPending`) when gameplay is not active yet, so the menu opens
  itself on the first active gameplay frame.
- **humanplayer.qvm values now apply on entry.** The log showed the values being
  typed but never a "saved" line - the separate Save click was the whole failure.
  Selecting or typing a Human value now writes the retail globals immediately
  (`applyHumanPhysics`); Save remains as an explicit re-apply/repair button.
- **Custom Position removed from both Heaven menus.** The typed send X/Y/Z boxes
  and the Custom Position action are gone; Send Player To Target and the level
  landmark picker are the send-player paths. Heaven ships 69 controls
  (Advanced 72).
- Rebuilt every internal and external DLL from the shared table, so the Human
  section (and everything else) is identical across frontends; only the UI
  differs.


## 0.4.2 — Restart-safe internal menu, reliable logging and Human parity

- **Internal menu after a level/game restart ("frame opens with no options"):**
  a restart can leave the previous session's `IGI_SDK_*` script symbols and the
  `LOCAL:mod_base/*.qvm` resource registered, so the next open skipped
  re-registration and ran the stale script, which filled no ListBox rows.
  - `registerMenuScriptCallbacks()` now reclaims entries whose handler is one of
    this module's own trampolines (identified through the retail symbol table via
    `ScriptRegistryApi::findSymbol`). A symbol owned by another module is still
    never removed, and a registry without symbol introspection keeps the old
    refuse-on-collision behaviour.
  - `MenuResource::registerData()` reclaims a resource registered under a
    `LOCAL:mod_base/` name it no longer owns; other namespaces are untouched.
  - A close rejected by the retail manager abandons the stale session
    (`revertMenuState()`), so the next open rebuilds the screen and resources.
- **Log file reliability:** a failed open no longer silences the whole session.
  The open is retried with a 5 s backoff, a new `log_file` is attempted
  immediately, and a per-process fallback file is used when the primary path is
  unavailable (a held file or a different working directory after a restart).
  The resolved absolute path is logged once, so "no log" is distinguishable from
  "the log lives elsewhere".
- **Human section parity:** the Advanced Mod menus now generate the same
  `DefineHumanPlayerGeneral` controls as the internal menu (move scale, jump
  speed, jump height, air speed, damage scale, fence damage, Save, Reload, Read);
  internal and external demo sources stay byte-identical, so only the UI differs.
  All demo targets get `demos/` on the include path.
- **Menu surface cleanup:** the duplicate "Move Player To Position" X/Y/Z boxes
  were removed from Heaven; Send Player Position (live target, landmark, typed
  `X/Y/Z` and Custom Position) is the single typed-XYZ path. Heaven now ships 73
  controls.


## 0.4.1 — Start-key, pause restoration, free camera and teleport hardening

- **One F5, one toggle:** both hosts now serialize start-key toggle requests with a
  single `toggleRequestPending` flag. Two requests queued before the game thread
  drained them used to open and immediately close the same screen.
- **Retail pause restoration:** the retail screen that was current before the mod
  menu replaced it is saved in `returnScreenId`, survives page switches, and is
  used by the leave command. Closing the mod menu now returns to the pause menu
  the player opened instead of resuming gameplay. The saved id is cleared on
  close, deferred close, shutdown and level change.
- **Free camera WASD:** W/S now move along the full aim direction including
  pitch, so flying while looking up/down climbs or descends; A/D remain
  horizontal strafe. The movement clock resets when the camera is re-enabled so
  a stale frame delta cannot jump the new pose.
- **Ground teleport rejection:** a ground probe that finds no floor (or leaves
  the body at the probe height) now tries the high probe, then restores the
  original body position and reports failure instead of leaving the player
  airborne or under terrain.
- **Per-level Level Start target:** the first valid player position of each Game
  task is cached and added to the landmark list, guaranteeing a working
  teleport destination on every level even when the authored object names do not
  match a landmark keyword. Its slot is reserved before the 64-choice cap so the
  list can never overflow when a level also has many authored landmarks.
- **`invalid menu configuration` retry loop:** live target refresh can produce
  labels the retail list cannot represent (over 39 bytes, embedded NUL, more
  than 64 choices, stale default selection) or inverted input ranges. The
  internal host now repairs those values before compiling, logs the repair, and
  compiles from one immutable snapshot so validation and generation cannot see
  different live lists. The generator also reports why script writing failed
  instead of the old bare message.

## 0.4 — Heaven bug fixes, IGI-RE free camera, teleport targets and world clocks

- **Menu opening with a resident IGI-RE/ImGui mod:**
  - `supported retail hook signatures mismatch` was caused by the coexistence
    probe requiring the foreign jump target to live in a module whose name looks
    like an IGI menu DLL. IGI-RE's `dinput.dll` patches the display entries with a
    plain `E9` jump into its own module, so the probe missed a resident hook and
    both the internal and external menus refused to initialize.
  - Patched entries are now recognized by their inline-hook stub (`E9`/`EB`,
    push/ret, `mov eax; jmp eax`, `jmp [mem]`) rather than by module name. When
    only one entry is owned, the menu chains the free entry; when the dispatcher
    cannot be chained, `Display_EndFrame` becomes the per-frame driver.
  - The executable gate now validates that the display addresses belong to the
    loaded `igi.exe` image and then checks each entry's instruction family, so a
    rebuilt retail executable no longer disables every option at startup.

- **Heaven control surface (latest):**
  - Removed the read-only Level Tick, Flow Counters and Game Events diagnostics.
  - Added a Human section: Jump Speed and Jump Height inputs (tenths of km/h)
    that read the live `humanplayer.qvm` BSS globals (`gJumpSpeed` 0x56E1EC,
    `gJumpUp` 0x56E1F0) and write them back on Save; `Reload From
    humanplayer.qvm` re-runs `HumanPlayer_LoadParameters` (0x4137A0) to restore
    the authored values.
  - Added helicopter controls through `Heli_Control` (0x431B70): Stop Reverse
    (target speed 0), Reverse/Forward/Forward Fast, Yaw, Nose pitch and Roll,
    plus selected-helicopter reset.
  - Added `Selected Tank Drive` through the existing `Car_Control` path, with
    tank-specific speed presets.
- **Eject crash:**
  - The worker no longer calls `FreeLibraryAndExitThread`. The End key/F12 eject
    path stops the callbacks and retires the detours, then keeps the module
    resident until the game exits. Freeing the module while the game thread could
    still be entering a retired detour was the post-eject page fault.


- **Menu lifecycle fixes:**
  - Read the current screen from the live `MenuManager` task instead of
    `Flow_GetScreen()` (which returns the Flow Screen task). The wrong object made
    the host ignore its own attached screen (double open on one F5) and miss the
    retail pause menu underneath it (pause-menu interference on eject).
  - One-shot attach guard (`initialAttachPending.exchange`) and no duplicate
    `menuFrameWork()` from `RunFrame`, so one start-key press performs one attach.
  - Final close now uses the retail `MenuManager_LeaveMenus(900, 9)` resume path
    (or returns to a retail screen that was already open) instead of popping an
    empty screen stack. Page replacement rebuilds in place instead of popping.
  - Shutdown/eject refuses while a mod screen transition or retail menu still
    depends on the module, and level changes abandon stale pause ownership.
- **Free camera (IGI-RE reference flow):**
  - `Viewport_AddCallback` and `TransContext_Project` wrapper signatures now match
    the native registry; the callback previously never registered, which is why
    the camera and crosshair options did nothing.
  - The viewport callback applies the pose with `Camera_Set` (priority 1000, same
    call shape as IGI-RE `FreeCamera_Run`) and `HumanView_SetCamera` drives the
    retail `CAM_OVERRIDE` path, so WASD flight works while the game is paused.
  - Camera axes are built with `Matrix_SetEuler` + `Matrix_RotateX(-90)`; the
    start pose comes from the live render camera, not the player eye.
  - Debug Keys / Fly Mode routes to the same safe camera override instead of the
    crashing retail body-fly state.
- **Teleport and position:**
  - Ground placement probes down from above the requested point (and from 40 m up
    when the point is under terrain), so typed XYZ cannot place the body under
    the map; both retail position broadcasts are sent after a move.
  - Send Player Position accepts a live AI/human, any named 3D object, the player
    entity, or typed XYZ, and now publishes both `Human_SendPosition` and
    `HumanPlayer_SendPosition`.
  - The live object/landmark picker walks the authored Level task tree instead of
    only the small game event lists, so doors, props, buildings and mission
    locations are selectable.
- **World and mission clocks:**
  - Daytime/nighttime presets, a custom world hour, mission elapsed time and an
    independent mission time limit, all writing the retail `LevelFlow` fields
    (`nTick +0x68`, `startHour +0x6C`, `timeLimit +0x74`).
- **Level changes:**
  - Mission ids are validated through `Mission_Find` before the current level is
    unloaded, and the switch is deferred to the end-of-frame callback after the
    scene flush instead of deleting the Game task inside the draw pass.
- **Close and eject stability:**
  - Closing now recognises the retail `MenuManager` fade-out-to-idle resume
    transition instead of treating it as an open pause menu, so `resumeFromMenu`
    re-enables human input again.
  - Eject requests cleanup once. The host no longer re-queues `RequestShutdown`
    every `canUnload` poll, and the game-thread cleanup is idempotent; the
    duplicate cleanup that double-freed menu resources on eject is gone.
  - Coexistence uninstall waits for the chained dispatch detour to return before
    removing its hook.
- **Menu configuration diagnostics:**
  - Dynamic target labels keep the `[#id]` suffix inside the 40-character QVM
    limit (address-derived ids used to overflow it and invalidate the whole
    menu after a live-target refresh).
  - `loadMenuConfig` clamps a malformed frame size to usable values, and resource
    compilation now reports the exact invalid item instead of the generic
    "invalid menu configuration". Added a simple-menu layout/QVM test.
  - Teleport/Send coordinate inputs cover the full retail world-unit range
    (4096 units per metre, map coordinates in the tens of millions) and the
    live-player prefill is clamped. A large map coordinate can no longer fail
    validation on the first refresh; an out-of-range display hint is likewise
    ignored instead of invalidating the whole menu.
- **Internal/external menu coexistence:**
  - The internal menu no longer refuses with `supported retail hook signatures
    mismatch` when the external ImGui DLL is still resident. If another IGI menu
    owns the display hooks, the internal menu chains only
    `Display_RunCallbacks`, registers its tick through the existing registrar,
    and leaves the other mod's hooks intact. Its own eject removes only that
    chain and callback.
  - Re-enabling a retired menu restores `hookBehaviorActive`, so a re-injected
    menu DLL ticks again instead of staying a pass-through.
- **Level restart stability:**
  - The generated Restart button now queues `IGI_SDK_Restart`; the host
    completes the detach from the end-frame path (after the draw pass) and
    releases the QVM resources **before** `LevelRestart`. Previously the retail `LeaveMenus(900, 12)` path destroyed
    the LevelFlow/MenuManager and invalidated the QVM resource while the mod
    still thought it was registered, so the next F5 produced frames with no
    options and eject could not restore the retail pause menu.
  - The demo `Restart Mission` action uses the same queued
    `requestMissionRestart`/`processMissionRestart` path, so both restart
    controls share one safe transition and one cleanup.
  - A full close now releases the QVM resource and unregisters `IGI_SDK_*`
    callbacks. Keeping them across a close let a level restart invalidate the
    resource while the host still skipped re-registration, which produced
    frames with no options on the next F5.
  - The frame tick also detects a replaced `MenuManager` (level restart or next
    mission) and releases the previous level's menu session before the new
    level can use it.
- **Exit/eject:**
  - Added a routed **Exit Mod** action to both the internal QVM and external
    ImGui Heaven menus. It calls `IGI_SDK_RequestSafeUnload`, closes the menu,
    removes the retail callbacks and unloads the DLL from the host thread.
  - The configured end key now ejects instead of only disabling, so F5 cannot
    re-open the menu after an eject.
  - The external ImGui host also completes a pending eject from the end-frame
    callback, so a missing/late scheduler tick can no longer leave the menu
    visible with the start key still working.
  - Renamed the typed destination action label to **Custom Position**.
- **Weapons:**
  - Weapon drop no longer follows the retail **death** sequence on a live player.
    It drops only the held task, lets the game's world remove event delete it,
    keeps the rest of the inventory, and equips another carried gun or the
    retail Glock sidearm when the last weapon is dropped. The old
    `Weapon_DeleteAll` call removed every weapon and left the live player
    unarmed, which crashed the next gameplay frame.
  - Ammo applies to the selected weapon task (creating it through
    `WeaponGunPickup` when needed), not only the gun in hand; Equip All uses the
    typed amount.
- **Menu layout:**
  - Frame width grows with the longest label/value, height is page-aware (the last
    page no longer reserves empty rows), and the last-page navigation stays inside
    the frame. The external ImGui window sizes itself when `auto_resize` is on.
- **Verification:**
  - New `tests/test_native_signatures.py` fails on wrapper/registry drift.
  - New Unicorn `--verify-heaven-flows` runs seven retail routines: mission
    lookup, LevelFlow clock, camera axes, `Camera_Set` priority, the
    `CAM_OVERRIDE` path, both position broadcasts and display-space projection.
  - Heaven layout/contract tests updated for 66 controls; all 10 ctest suites pass.

## 0.3 — Heaven production menu & Heaven Advanced: 50+ verified gameplay controls

- **Production Menu Expansion (50 Verified Controls):**
  - Heaven ships 50 verified controls across six primary categories (Weapons,
    Player, Camera, World, Vehicles, AI / NPC) plus dynamic target selection, all
    declared from a unified definition table enforcing category and dispatch routes.
  - The retail QVM menu fits only a handful of rows, so control sets that
    outgrow the frame dynamically generate a scrollable `ListBox` over a flattened
    command list (`IGI_SDK_FillList/GetList/SetList(-1)`). Every option stays
    accessible within the frame; smaller control sets preserve individual controls.
- **Heaven Advanced Targets:**
  - Added dedicated Heaven Advanced DLL targets (`igi-heaven-mod-advanced-internal.dll`
    and `igi-heaven-mod-advanced-external.dll`) powered by `IGI_HEAVEN_ADVANCED=1`.
  - Added live NPC 3D movement controls: Move NPC Forward (+5m), Backward (-5m),
    Left (-5m), Right (+5m), Elevate (+2m), and Snap to Ground via retail
    `Human_SetPosition` and view forward/lateral matrix calculations.
  - Added live NPC Ground State controls: switch selected NPC between `Ground`
    and `Air` (levitation) via `Human_SnapToGround` and `Human_LeaveGround`.
  - Added Player Ground State controls: toggle player between `Ground` and `Air`.
  - Added Helicopter Reset control (`resetHeli`): resets selected helicopter state
    via the retail `tick = 0` control branch.
- **Weapons & HUD:**
  - Added direct weapon ammo refill controls for Glock 17, MP5SD, M16A2, and AK-47.
  - Added full weapon inventory equipping, weapon draw animations, and cycle weapon.
  - Implemented 3rd-person crosshair rendering in both internal (retail QVM / sprite)
    and external (Dear ImGui) Heaven menus using the native HUD sprite draw pipeline.
- **Dynamic Live Target Tracking:**
  - Live NPC, car/tank, and helicopter target lists render the retail task name plus
    stable task id (`HumanSoldier_2021 [#2021]`), refresh from the running game's
    task table in both menu frontends, track selected targets across menu refreshes,
    and drop targets that leave the level.
  - Identified tanks in live Car targets using the shared retail vehicle controller.
- **World & Vehicle Controls:**
  - Added high refresh frame cap presets (30, 60, 120, 144, 240 FPS).
  - Added car driving controls with verified throttle/steering/braking inputs.
  - Added whole-level explode all cars and reset all cars actions.
- **Platform & Injection:**
  - Fixed Wine module matching in `tools/igi_injector.cpp` by matching loaded
    modules by DLL filename rather than strict full-path matching.
- **Quality Assurance & Verification:**
  - Added `igi_heaven_layout` and `igi_heaven_menu_contract` test suites that enforce:
    - Normal Heaven ships exactly 50 working controls without dropping categories.
    - Advanced controls are properly gated under `#if defined(IGI_HEAVEN_ADVANCED)`.
    - Every menu action has a dedicated game-thread native execution branch.

## 0.2 — Restructure, Dear ImGui Direct3D 7, and runtime lifecycle

- Integrated Dear ImGui Direct3D 7 renderer backend from
  [AndenDev/D3D7Imgui](https://github.com/AndenDev/D3D7Imgui) (`imgui_impl_dx7.cpp`,
  `imgui_impl_dx7.h`, `imgui_impl_win32.cpp`) for legacy DirectX 7 game compatibility.
- Implemented Dear ImGui external mod menus for Basic, Simple, and Advanced menus
  (`igi-basic-mod-external.dll`, `igi-simple-mod-external.dll`, `igi-advanced-mod-external.dll`)
  via `src/mod_base/imgui_menu_host.cpp`.
- Added comprehensive `[imgui]` configuration options in `project-igi-mod-config.ini`
  for themes (dark/light/classic), font scaling, alpha opacity, window dimensions,
  mouse navigation, and persistent layout management.
- Reorganized the codebase into focused `src/config`, `src/logging`,
  `src/mod_base`, `src/natives`, `src/platform`, `src/runtime`, and `src/ui`
  modules.
- Added shared internal-menu hosting for the Basic, Simple, and Advanced mod
  menus, while keeping each menu’s actions in its demo source.
- Added C++ formatting, clang-tidy, CTest, Unicorn validation, and GitHub
  Actions quality checks.
- Updated the Windows x86 build, deployment, Wine, and injector helpers around
  the renamed mod artifacts and configuration files.
- Fixed level switching to follow the game’s native unload/load sequence:
  select mission, request Flow game event, then delete the current `Game` task.
- Release injected QVM resources before changing levels so the menu can attach
  again in the newly loaded mission.
- Added lifecycle guards for native calls when exiting to the main menu.
- Made the configured end key stop the resident host thread after safe cleanup;
  the start key remains disabled until `igi.exe` exits.
- Added Simple and Advanced menu screenshots, feature lists, and contributor
  credits to the README.
- Fixed visible weather: shared menu helpers now parent the retail `RainEffect`
  task under the active `Level` task and broadcast `Param_GetChangedEvent` for
  live rain and snow precipitation effects.
- Added prebuilt x86 release DLLs in `binaries/` (`igi-advanced-mod-internal.dll`
  and `igi-advanced-mod-external.dll`) with automatic deployment on build completion.
- Clarified repository branching model: `develop` is strictly dedicated to the
  retail IGI Game Menu UI (Internal Mod Menu via QVM MenuManager), while
  `feature/imgui-mod` provides the external Dear ImGui Direct3D 7 menu
  implementation backed by [AndenDev/D3D7Imgui](https://github.com/AndenDev/D3D7Imgui).

## 0.1 — IGI 1 MVP

- Added Windows x86 internal menu demos, configurable hotkeys, logging, and DLL lifecycle management.
- Added game-thread scheduling through display frame callbacks and an IGI-RE native catalogue.
- Added Windows build and injection helpers, Wine launch scripts, and Unicorn native verification.
- Added menu controls for weather, frame rate, weapons, and other native actions.
- Added configurable menu titles, the simple-menu screenshot, and game-constant weapon selection.
- Release builds omit debug sections. Level switching now requests game teardown before starting the selected mission; live verification remains pending.

The supported executable profile is the IGI-RE EU/Chinese IGI 1 build.
