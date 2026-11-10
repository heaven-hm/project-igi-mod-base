#pragma once

namespace igi::game {
// True when both display hook prologues still match the supported retail build.
// A permit-anything false here is not a refusal by itself: installers fall back
// to isIgiExecutableModule() plus a per-entry instruction plausibility check so
// a rebuilt/patched igi.exe whose code moved by a few bytes still opens.
bool isSupportedExecutable() noexcept;
// True when the display addresses live in the loaded main executable whose file
// name is igi.exe. This is the identity gate that replaces the old exact-prologue
// refusal (the reported "supported retail hook signatures mismatch" failure).
bool isIgiExecutableModule() noexcept;
// True when an address begins with a relative jump. Used per entry, because a
// resident mod may own only one of the display hooks.
bool isHookJumpAt(unsigned long address) noexcept;
// True when the entry begins with any common inline-hook stub (E9/EB, push-ret,
// mov-eax-jmp-eax, jmp [mem]). Trampolines often live outside a named module, so
// this recognizes a resident mod even when a module-name lookup cannot.
bool isPatchedEntry(unsigned long address) noexcept;
// True when either display entry already begins with a relative jump into another
// injected IGI mod DLL. A resident/retired mod menu leaves these hooks installed;
// the internal menu must report that instead of the generic signature error.
bool hasIgiModHook() noexcept;
} // namespace igi::game
