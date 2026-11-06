#pragma once

namespace igi {
class NativeCaller;
class NativeCatalogue;

// Adds the SDK's reviewed, callable IGI 1 bindings. The bulk IGI-RE TSV stays
// catalogued until each native has an equivalent reviewed binding here.
bool loadIgi1VerifiedNativeRegistry(NativeCatalogue& catalogue);
void bindIgi1NativeCaller(NativeCaller* caller) noexcept;
NativeCaller* igi1NativeCaller() noexcept;
} // namespace igi
