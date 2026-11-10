#pragma once

namespace igi::game {
// True when both display hook prologues still match the supported retail build.
bool isSupportedExecutable() noexcept;
// True when both display entries already begin with a relative jump into another
// injected IGI mod DLL. A resident/retired mod menu leaves these hooks installed;
// the internal menu must report that instead of the generic signature error.
bool hasIgiModHook() noexcept;
} // namespace igi::game
