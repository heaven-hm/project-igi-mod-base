# Demonstration targets

The repository contains two deliberately separate demo entry points:

- `IGI-MenuManager-Internal.dll` — an in-process Windows x86 demo that creates
  a reusable `MenuManager` after the SDK runtime is initialized.
- `IGI-MenuManager-External` — a standalone companion/catalogue inspector. It
  is not a game-process injector and does not claim external game control.

The external-style target remains intentionally limited until a reviewed IPC
contract is added. This avoids falsely presenting a DLL or standalone process
as a working out-of-process game modification interface.

Both targets use the same SDK modules and can be built with the normal CMake
commands in the root README. No game assets or executable files are included.
