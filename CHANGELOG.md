# Changelog

## 0.1.0 — IGI 1 MVP

- Added the guarded `Game_RunHandler` scheduler hook at `0x00416260`.
- Preserved original handler execution through an x86 trampoline.
- Added game-thread scheduling, lifecycle reset, logging, and cadence counting.
- Added the IGI-RE address/signature catalogue for IGI 1.
- Added the renderer-independent MenuManager model and input handling.
- Added host tests and Windows x86 Release verification.

The supported executable profile is the IGI-RE EU/Chinese IGI 1 build. Runtime
artifacts are published through GitHub Releases and are not committed here.
