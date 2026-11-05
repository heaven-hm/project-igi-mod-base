# Project IGI Natives SDK — IGI 1 MVP

This repository currently ships the first IGI 1-only runtime slice: a game-thread scheduler and a guarded `Game_RunHandler` hook for the EU/Chinese executable build documented in [`docs/IGI1_HOOK_EVIDENCE.md`](docs/IGI1_HOOK_EVIDENCE.md).

The SDK is for offline, single-player experimentation only. It does not provide multiplayer functionality, a memory editor, or a general-purpose memory-writing API.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The runtime hook is compiled only for Windows x86. The scheduler tests are portable. The hook refuses to install if the known IGI 1 prologue does not match.
