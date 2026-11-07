# IGI 1 hook evidence

Target: supported retail `igi.exe` (PE32 x86)

SHA-256: `dea34892d417715873ea61d7b2ba5556655f1ad65385104348146f9679915a01`

Static PE i386 disassembly of the target shows:

- `Game_RunHandler` at `0x00416260` begins `53 56 57 e8 78 01 00`, a function prologue followed by `Game_UpdateCheats`. IGI-RE registers it as task-slot 1 with the recovered signature `void __cdecl Game_RunHandler(Game *g)`. Its plain `ret` matches caller cleanup. It runs child gameplay tasks and game music work, making it the primary scheduler hook candidate.
- `Flow_RunHandler` at `0x00402260` begins `83 ec 08 53 55 56 57`. IGI-RE recovers it as `void __cdecl Flow_RunHandler(Flow *flow)`; it schedules simulation and render child-task passes and handles game-flow events. It remains a secondary investigation candidate, not an SDK hook.
- `Game_Update` at `0x00401050` establishes a `setjmp` recovery point and dispatches the active task-slot-1 handler through the handler table. It is intentionally not selected as the SDK hook.

IGI-RE's `Game_Init` registers `Game_RunHandler` for the run slot and registers `GAME_FREQUENCY` as 30. `Flow_RunHandler` also invokes `FramesSet(30)` when entering/restarting a game. This is source-level evidence for a gameplay cadence of 30 Hz; an in-process counter is still required before claiming measured frequency on this exact executable hash. Re-run `tools/verify_igi_re_hook_source.sh` to check these source facts against the local IGI-RE checkout.

The MVP hooks only the primary address. Before writing executable memory it
hashes the loaded main executable with Windows CryptoAPI and requires the exact
SHA-256 above, then validates the exact 8-byte prologue
(`53 56 57 e8 78 01 00 00`). It writes a five-byte relative jump when possible
and uses `push imm32; ret` only as an x86 fallback. The naked detour preserves
flags plus volatile registers, runs the scheduler, then tail-jumps through a
trampoline containing the relocated `Game_UpdateCheats` call and resumes at
`Game_RunHandler+8`. The first successful detour establishes the scheduler's
game-thread identity, rather than incorrectly using the DLL loader thread.
`Draw`, `MenuManager_Draw`, and `TextPrintDetour()` are not used.

`tools/verify_igi1_hook.sh` verifies this exact target with its SHA-256 and
PE i386 disassembly. It does not use r2. The hook's runtime invocation counter
is exported for cadence measurement; an observed in-game cadence sample is
still required before documenting a measured value for a running process.

The recovered argument and calling convention are documented above. Runtime call-frequency measurement remains pending for this exact target hash. The detour is naked and forwards the original stack unchanged.
