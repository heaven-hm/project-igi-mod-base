#include "igi_sdk/natives/igi1_native_registry.hpp"

#include "igi_sdk/natives/native_caller.hpp"
#include "igi_sdk/natives/native_catalogue.hpp"
#include "igi_sdk/natives/mvp_natives.hpp"

#include <atomic>

namespace {
std::atomic<igi::NativeCaller*> activeCaller{nullptr};
}

namespace igi {
bool loadIgi1VerifiedNativeRegistry(NativeCatalogue& catalogue) {
    #include "mvp_registry.inc"
    return catalogue.size() == IGI::NativeBindingCount();
}

void bindIgi1NativeCaller(NativeCaller* caller) noexcept {
    activeCaller.store(caller, std::memory_order_release);
}

NativeCaller* igi1NativeCaller() noexcept {
    return activeCaller.load(std::memory_order_acquire);
}
} // namespace igi
