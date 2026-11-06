#pragma once

#include "igi_sdk/natives/native_catalogue.hpp"
#include "igi_sdk/runtime/scheduler.hpp"

#include <cstdint>
#include <functional>
#include <string_view>

namespace igi {

enum class NativeCallStatus {
    Success,
    NotTargetBuild,
    MissingNative,
    SignatureMismatch,
    UnverifiedNative,
    InvalidFunction,
    Exception
};

// Resolves verified IGI-RE metadata and queues work on the game scheduler.
// Concrete, typed native-domain bindings are added in their own .cpp files
// only after target-build verification; this header never embeds a native.
class NativeCaller final {
public:
    NativeCaller(const NativeCatalogue& catalogue, Scheduler& scheduler,
                 bool targetValidated) noexcept;

    bool targetValidated() const noexcept;
    NativeCallStatus resolve(std::string_view name, std::string_view signature,
                             std::uintptr_t& address) const noexcept;
    NativeCallStatus resolveByAddress(std::uint32_t nativeAddress,
                                      std::uintptr_t& address) const noexcept;
    std::string_view nameForAddress(std::uint32_t nativeAddress) const noexcept;
    Scheduler& scheduler() noexcept;
    TaskId queue(std::string_view nativeName, std::function<void()> invocation);

    static void reportSuccess(std::string_view name) noexcept;
    static void reportException(std::string_view name) noexcept;

private:
    static void reportResolved(std::string_view name, std::uint32_t address,
                               std::string_view signature) noexcept;
    static void reportFailure(std::string_view name, NativeCallStatus status) noexcept;

    const NativeCatalogue& catalogue_;
    Scheduler& scheduler_;
    bool targetValidated_ = false;
};

} // namespace igi
