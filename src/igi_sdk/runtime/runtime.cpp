#include "igi_sdk/runtime/runtime.hpp"

#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/natives/igi1_native_registry.hpp"

#include <memory>


namespace igi {
bool Runtime::initialize(igi1::TickHook hook) {
    if (initialized_) return true;
    if (hook != igi1::TickHook::GameRunHandler) {
        log(LogLevel::Error, "SDK scheduler requires the IGI 1 Game_RunHandler gameplay hook");
        return false;
    }
    if (!hooks_.installPrimaryGameRunHandler()) return false;
#if !defined(_WIN32) || (!defined(__i386__) && !defined(_M_IX86))
    log(LogLevel::Error, "IGI 1 runtime requires the verified 32-bit Windows PE process");
    hooks_.uninstall();
    return false;
#endif
    catalogue_ = std::make_unique<NativeCatalogue>();
    if (!loadIgi1VerifiedNativeRegistry(*catalogue_)) {
        hooks_.uninstall();
        catalogue_.reset();
        return false;
    }
    caller_ = std::make_unique<NativeCaller>(*catalogue_, scheduler_, true);
    bindIgi1NativeCaller(caller_.get());
    initialized_ = true;
    log(LogLevel::Info, "IGI 1 SDK runtime initialized");
    return true;
}
void Runtime::shutdown() noexcept {
    if (!hooks_.uninstall()) return;
    bindIgi1NativeCaller(nullptr);
    caller_.reset();
    catalogue_.reset();
    scheduler_.reset();
    initialized_ = false;
}
}
