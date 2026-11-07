#pragma once

namespace igi {
class NativeCaller;
class NativeCatalogue;

// Adds the SDK's reviewed, callable IGI 1 bindings. The bulk IGI-RE TSV stays
// catalogued until each native has an equivalent reviewed binding here.
bool loadIgiVerifiedNativeRegistry(NativeCatalogue &catalogue);
void bindIgiNativeCaller(NativeCaller *caller) noexcept;
NativeCaller *igiNativeCaller() noexcept;
} // namespace igi
