#include "igi_sdk/natives/native_caller.hpp"

#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/runtime/igi1_hooks.hpp"

#include <cstdio>
#include <string>

namespace igi {
NativeCaller::NativeCaller(const NativeCatalogue& catalogue, Scheduler& scheduler,
                           bool targetValidated) noexcept
    : catalogue_(catalogue), scheduler_(scheduler), targetValidated_(targetValidated) {}

bool NativeCaller::targetValidated() const noexcept {
    return targetValidated_;
}

NativeCallStatus NativeCaller::resolve(std::string_view name, std::string_view signature,
                                       std::uintptr_t& address) const noexcept {
    address = 0;
    if (!targetValidated_) {
        reportFailure(name, NativeCallStatus::NotTargetBuild);
        return NativeCallStatus::NotTargetBuild;
    }
    const NativeRecord* record = catalogue_.findByName(name);
    if (!record) {
        reportFailure(name, NativeCallStatus::MissingNative);
        return NativeCallStatus::MissingNative;
    }
    if (record->signature != signature) {
        reportFailure(name, NativeCallStatus::SignatureMismatch);
        return NativeCallStatus::SignatureMismatch;
    }
    if (record->verification == NativeVerification::Catalogued) {
        reportFailure(name, NativeCallStatus::UnverifiedNative);
        return NativeCallStatus::UnverifiedNative;
    }
    if (record->address < igi1::imageBase ||
        record->address >= igi1::imageBase + igi1::expectedImageSize) {
        reportFailure(name, NativeCallStatus::InvalidFunction);
        return NativeCallStatus::InvalidFunction;
    }
    address = record->address;
    reportResolved(name, record->address, signature);
    return NativeCallStatus::Success;
}

NativeCallStatus NativeCaller::resolveByAddress(std::uint32_t nativeAddress,
                                                std::uintptr_t& address) const noexcept {
    const auto* record = catalogue_.findByAddress(nativeAddress);
    return resolveByAddress(nativeAddress, record ? std::string_view(record->signature) : std::string_view{}, address);
}

NativeCallStatus NativeCaller::resolveByAddress(std::uint32_t nativeAddress, std::string_view signature,
                                                std::uintptr_t& address) const noexcept {
    address = 0;
    if (!targetValidated_) {
        reportFailure("address", NativeCallStatus::NotTargetBuild);
        return NativeCallStatus::NotTargetBuild;
    }
    const NativeRecord* record = catalogue_.findByAddress(nativeAddress);
    if (!record) {
        reportFailure("address", NativeCallStatus::MissingNative);
        return NativeCallStatus::MissingNative;
    }
    if (record->signature != signature) {
        reportFailure(record->name, NativeCallStatus::SignatureMismatch);
        return NativeCallStatus::SignatureMismatch;
    }
    if (record->verification == NativeVerification::Catalogued) {
        reportFailure(record->name, NativeCallStatus::UnverifiedNative);
        return NativeCallStatus::UnverifiedNative;
    }
    if (record->address < igi1::imageBase || record->address >= igi1::imageBase + igi1::expectedImageSize) {
        reportFailure(record->name, NativeCallStatus::InvalidFunction);
        return NativeCallStatus::InvalidFunction;
    }
    address = record->address;
    reportResolved(record->name, record->address, signature);
    return NativeCallStatus::Success;
}

std::string_view NativeCaller::nameForAddress(std::uint32_t nativeAddress) const noexcept {
    const NativeRecord* record = catalogue_.findByAddress(nativeAddress);
    return record ? std::string_view(record->name) : std::string_view{};
}

Scheduler& NativeCaller::scheduler() noexcept {
    return scheduler_;
}

TaskId NativeCaller::queue(std::string_view nativeName, std::function<void()> invocation) {
    if (!invocation) return 0;
    const std::string name(nativeName);
    return scheduler_.enqueueOnce([name, invocation = std::move(invocation)]() mutable {
        try {
            invocation();
            reportSuccess(name);
        } catch (...) {
            reportException(name);
        }
    });
}

void NativeCaller::reportSuccess(std::string_view name) noexcept {
    char message[192]{};
    std::snprintf(message, sizeof(message), "native invoked: %.*s",
                  static_cast<int>(name.size()), name.data());
    log(LogLevel::Trace, message);
}

void NativeCaller::reportException(std::string_view name) noexcept {
    reportFailure(name, NativeCallStatus::Exception);
}

void NativeCaller::reportResolved(std::string_view name, std::uint32_t address,
                                  std::string_view signature) noexcept {
    char message[256]{};
    std::snprintf(message, sizeof(message), "native bound: %.*s @ 0x%08X signature=%.*s",
                  static_cast<int>(name.size()), name.data(), address,
                  static_cast<int>(signature.size()), signature.data());
    log(LogLevel::Debug, message);
}

void NativeCaller::reportFailure(std::string_view name, NativeCallStatus status) noexcept {
    const char* reason = status == NativeCallStatus::NotTargetBuild ? "target not validated" :
                         status == NativeCallStatus::MissingNative ? "native missing" :
                         status == NativeCallStatus::SignatureMismatch ? "signature mismatch" :
                         status == NativeCallStatus::UnverifiedNative ? "native not verified" :
                         status == NativeCallStatus::InvalidFunction ? "invalid function" :
                         status == NativeCallStatus::Exception ? "native threw" : "call failed";
    char message[224]{};
    std::snprintf(message, sizeof(message), "native call refused: %.*s (%s)",
                  static_cast<int>(name.size()), name.data(), reason);
    log(LogLevel::Warning, message);
}
} // namespace igi
