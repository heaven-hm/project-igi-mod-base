# Demonstration targets

The repository contains two deliberately separate demo entry points:

- `IGI-MenuManager-Internal.dll` / `libIGI-MenuManager-Internal.dylib` — a
  complete shared-library example with exported initialize, run-frame, and
  shutdown entry points. It creates a real `MenuManager` and routes its work
  through the SDK scheduler.
- `IGI-MenuManager-External` — a standalone companion/catalogue inspector. It
  is not a game-process injector and does not claim external game control.

The external-style target remains intentionally limited until a reviewed IPC
contract is added. This avoids falsely presenting a DLL or standalone process
as a working out-of-process game modification interface.

Both targets use the same SDK modules and can be built with the normal CMake
commands in the root README. The internal shared library is loaded by a mod's
own in-process plugin loader; this repository does not include a remote
process injector. On macOS, call the exported entry points from a test host or
load the dylib through `dlopen`; it intentionally does not patch the Windows
x86 IGI executable. No game assets or executable files are included.
