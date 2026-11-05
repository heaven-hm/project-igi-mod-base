# IGI 1 hook evidence

Target: `/Users/haseeb-mir/Downloads/IGI/IGI.EXE` (PE32 x86)

SHA-256: `dea34892d417715873ea61d7b2ba5556655f1ad65385104348146f9679915a01`

Static radare2 inspection of the target shows:

- `Game_RunHandler` at `0x00416260` begins `53 56 57 e8 78 01 00`, a function prologue followed by a call into the game loop. It is the primary scheduler hook candidate.
- `Flow_RunHandler` at `0x00402260` begins `83 ec 08 53 55 56 57`, has a larger lifecycle-oriented body, and remains a secondary investigation candidate.
- `Game_Update` at `0x00401050` is a short dispatcher containing indirect calls and is intentionally not selected without further call-flow evidence.

The MVP hooks only the primary address. The hook validates the exact prologue, runs the scheduler on that thread, then jumps through a trampoline containing the original prologue and returns to `Game_RunHandler+7`. This preserves original execution. `Draw`, `MenuManager_Draw`, and `TextPrintDetour()` are not used.

Call-frequency and complete argument semantics still require runtime verification in the target game. The detour is naked and forwards the original stack unchanged so it does not invent a public function signature while that investigation remains open.
