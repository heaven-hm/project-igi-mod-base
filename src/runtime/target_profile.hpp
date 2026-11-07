#pragma once

namespace igi::game {
// Verifies the loaded main executable, not an arbitrary file supplied by a mod.
bool isSupportedExecutable() noexcept;
} // namespace igi::game
