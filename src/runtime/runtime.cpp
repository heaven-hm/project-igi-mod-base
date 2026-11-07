#include "runtime/runtime.hpp"

#include "logging/logging.hpp"
#include "natives/igi_native_registry.hpp"
#include "runtime/target_profile.hpp"

#include <memory>

namespace igi {
bool Runtime::initializeDirectNativeMode() {
    if (initialized_)
        return true;
#if !defined(_WIN32) || (!defined(__i386__) && !defined(_M_IX86))
    log(LogLevel::Error,
        "IGI 1 direct-native mode requires the verified 32-bit Windows PE process");
    return false;
#else
    // This path follows the established project-igi-natives injected-worker
    // model. Its direct native calls do not invoke CryptoAPI while the game is
    // live; Wine 11 can terminate the game from that CryptoAPI sequence.
    // The hooked path retains the full executable SHA-256 validation.
    log(LogLevel::Info,
        "direct-native runtime: using injected retail process without CryptoAPI hash validation");
    catalogue_ = std::make_unique<NativeCatalogue>();
    log(LogLevel::Info, "direct-native runtime: native catalogue allocated");
    if (!loadIgiVerifiedNativeRegistry(*catalogue_)) {
        log(LogLevel::Error, "direct-native runtime: native registry load failed");
        catalogue_.reset();
        return false;
    }
    log(LogLevel::Info, "direct-native runtime: native registry loaded");
    scheduler_.bindGameThread();
    log(LogLevel::Info, "direct-native runtime: caller thread bound");
    caller_ = std::make_unique<NativeCaller>(*catalogue_, scheduler_, true);
    log(LogLevel::Info, "direct-native runtime: native caller allocated");
    bindIgiNativeCaller(caller_.get());
    log(LogLevel::Info, "direct-native runtime: native caller bound");
    directNativeMode_ = true;
    initialized_ = true;
    log(LogLevel::Info, "IGI 1 SDK direct-native runtime initialized without a scheduler hook");
    return true;
#endif
}

bool Runtime::allowDirectNativeCallbackThread() noexcept {
    if (!initialized_)
        return false;
    if (directNativeMode_)
        scheduler_.allowCurrentGameThread();
    return scheduler_.isGameThread();
}

bool Runtime::initialize(game::TickHook hook) {
    if (initialized_)
        return true;
    if (hook != game::TickHook::DisplayAddFrameCallback) {
        log(LogLevel::Error, "SDK scheduler requires the IGI 1 Display_AddFrameCallback hook");
        return false;
    }
#if !defined(_WIN32) || (!defined(__i386__) && !defined(_M_IX86))
    log(LogLevel::Error, "IGI 1 runtime requires the verified 32-bit Windows PE process");
    hooks_.uninstall();
    return false;
#endif
    catalogue_ = std::make_unique<NativeCatalogue>();
    if (!loadIgiVerifiedNativeRegistry(*catalogue_)) {
        hooks_.uninstall();
        catalogue_.reset();
        return false;
    }
    caller_ = std::make_unique<NativeCaller>(*catalogue_, scheduler_, true);
    bindIgiNativeCaller(caller_.get());
    initialized_ = true;
    if (!hooks_.installTickHook(hook)) {
        initialized_ = false;
        bindIgiNativeCaller(nullptr);
        caller_.reset();
        catalogue_.reset();
        return false;
    }
    log(LogLevel::Info, "IGI 1 SDK runtime initialized");
    return true;
}
void Runtime::shutdown() noexcept {
    if (!hooks_.uninstall())
        return;
    bindIgiNativeCaller(nullptr);
    caller_.reset();
    catalogue_.reset();
    scheduler_.reset();
    directNativeMode_ = false;
    initialized_ = false;
}
} // namespace igi
