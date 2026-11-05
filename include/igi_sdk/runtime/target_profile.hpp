#pragma once

namespace igi::igi1 {
// Verifies the loaded main executable, not an arbitrary file supplied by a mod.
bool isSupportedExecutable() noexcept;
}
