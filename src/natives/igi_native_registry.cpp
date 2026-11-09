#include "natives/igi_native_registry.hpp"

#include "natives/native_caller.hpp"
#include "natives/native_catalogue.hpp"
#include "natives/extra_natives.hpp"
#include "natives/mvp_natives.hpp"

#include <atomic>

namespace {
std::atomic<igi::NativeCaller *> activeCaller{nullptr};
}

namespace igi {
bool loadIgiVerifiedNativeRegistry(NativeCatalogue &catalogue) {
#include "mvp_registry.inc"
    if (!IGI::loadExtraNativeBindings(catalogue))
        return false;
    return catalogue.size() == IGI::NativeBindingCount() + IGI::ExtraBindingCount();
}

void bindIgiNativeCaller(NativeCaller *caller) noexcept {
    activeCaller.store(caller, std::memory_order_release);
}

NativeCaller *igiNativeCaller() noexcept {
    return activeCaller.load(std::memory_order_acquire);
}
} // namespace igi
