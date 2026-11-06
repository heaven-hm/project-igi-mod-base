#include "igi_sdk/runtime/runtime.hpp"

#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/natives/igi1_native_registry.hpp"
#include "igi_sdk/runtime/target_profile.hpp"

#include <memory>


namespace igi {
bool Runtime::initializeDirectNativeMode() {
    if (initialized_) return true;
#if !defined(_WIN32) || (!defined(__i386__) && !defined(_M_IX86))
    log(LogLevel::Error, "IGI 1 direct-native mode requires the verified 32-bit Windows PE process");
    return false;
#else
    // This path follows the established project-igi-natives injected-worker
    // model. Its direct native calls do not invoke CryptoAPI while the game is
    // live; Wine 11 can terminate the game from that CryptoAPI sequence.
    // The hooked path retains the full executable SHA-256 validation.
    log(LogLevel::Info, "direct-native runtime: using injected retail process without CryptoAPI hash validation");
    catalogue_ = std::make_unique<NativeCatalogue>();
    log(LogLevel::Info, "direct-native runtime: native catalogue allocated");
    if (!loadIgi1VerifiedNativeRegistry(*catalogue_)) {
        log(LogLevel::Error, "direct-native runtime: native registry load failed");
        catalogue_.reset();
        return false;
    }
    log(LogLevel::Info, "direct-native runtime: native registry loaded");
    scheduler_.bindGameThread();
    log(LogLevel::Info, "direct-native runtime: caller thread bound");
    caller_ = std::make_unique<NativeCaller>(*catalogue_, scheduler_, true);
    log(LogLevel::Info, "direct-native runtime: native caller allocated");
    bindIgi1NativeCaller(caller_.get());
    log(LogLevel::Info, "direct-native runtime: native caller bound");
    initialized_ = true;
    log(LogLevel::Info, "IGI 1 SDK direct-native runtime initialized without a scheduler hook");
    return true;
#endif
}

bool Runtime::initialize(igi1::TickHook hook) {
    if (initialized_) return true;
    if (hook != igi1::TickHook::DisplayAddFrameCallback) {
        log(LogLevel::Error, "SDK scheduler requires the IGI 1 Display_AddFrameCallback hook");
        return false;
    }
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
    if (!hooks_.installTickHook(hook)) {
        initialized_ = false;
        bindIgi1NativeCaller(nullptr);
        caller_.reset();
        catalogue_.reset();
        return false;
    }
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
