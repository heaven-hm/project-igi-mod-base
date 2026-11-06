#include "igi_sdk/runtime/runtime.hpp"

#include "igi_sdk/logging/logging.hpp"

namespace igi {
bool Runtime::initialize(igi1::TickHook hook) {
    if (initialized_) return true;
    if (hook == igi1::TickHook::DisplayAddFrameCallback) {
        if (!hooks_.installDisplayAddFrameCallbackTick()) return false;
    } else {
        if (!hooks_.installTickHook(hook)) return false;
    }
    initialized_ = true;
    log(LogLevel::Info, "IGI 1 SDK runtime initialized");
    return true;
}
void Runtime::shutdown() noexcept {
    hooks_.uninstall();
    scheduler_.reset();
    initialized_ = false;
}
}
