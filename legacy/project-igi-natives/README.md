# Preserved `project-igi-natives` baseline

This directory preserves the legacy Windows x86 SDK source imported from
`heaven-hm/project-igi-natives` at local revision `533664d3`.

It is intentionally excluded from the CMake build while its APIs, MinHook
integration, and third-party library boundaries are migrated into the SDK's
public C++20 modules. Its binary `.lib` artifacts were not imported.

Important: the preserved `FiberPool.hpp` describes a `TextPrintDetour()`
execution path. It is legacy-only and is not part of the active runtime. The
SDK runtime uses `Game_RunHandler` at `0x00416260` instead.

The imported files remain under the predecessor's MIT licence, reproduced in
`../../LICENSES/third-party/project-igi-natives-MIT.txt`.
