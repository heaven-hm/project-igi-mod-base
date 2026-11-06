#pragma once

#include "igi_sdk/logging/logging.hpp"
#include "igi_sdk/natives/igi1_native_registry.hpp"
#include "igi_sdk/natives/igi_re/hashes.hpp"
#include "igi_sdk/natives/native_api.hpp"
#include "igi_sdk/natives/native_caller.hpp"

#include <cstdint>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#if defined(_MSC_VER)
#define IGI_NATIVE_CDECL __cdecl
#elif defined(__GNUC__) && defined(__i386__)
#define IGI_NATIVE_CDECL __attribute__((cdecl))
#else
#define IGI_NATIVE_CDECL
#endif

namespace IGI {
using NativeHash = std::uint32_t;
// Compatibility sentinel retained for the original NATIVE_INVOKE<Void>(...) API.
using Void = void*;

// HASH carries every IGI-RE symbol from the generated igi::re::HASH table
// (6,929 entries) plus the upstream project-igi-natives compatibility names
// kept for existing mods.
namespace HASH {
using namespace ::igi::re::HASH;
inline constexpr NativeHash FRAMES_SET = 0x00402820;
inline constexpr NativeHash TASKTYPE_NEW = 0x00401900;
inline constexpr NativeHash CONFIG_READ = 0x00405850;
inline constexpr NativeHash GAMEMATERIAL_LOAD = 0x00408350;
inline constexpr NativeHash LEVEL_QUIT = 0x00416550;
inline constexpr NativeHash QSCRIPT_COMPILE = 0x004B8410;
inline constexpr NativeHash SCRIPT_SETSYMBOL_CXT = 0x004B8930;
inline constexpr NativeHash QSCRIPT_ASSEMBLE = 0x004BB270;
}

class NativeCaller final {
public:
    template <typename ReturnType = Void, typename... Args>
    ReturnType Invoke(Void nativeHash, Args&&... args) const {
        const NativeHash hash = static_cast<NativeHash>(reinterpret_cast<std::uintptr_t>(nativeHash));
        ::igi::NativeCaller* caller = ::igi::igi1NativeCaller();
        std::uintptr_t address = 0;
        if (!caller || caller->resolveByAddress(hash, address) != ::igi::NativeCallStatus::Success)
            return defaultResult<ReturnType>();

        const std::string nativeName(caller->nameForAddress(hash));
        if constexpr (std::is_same_v<ReturnType, Void>) {
            using Function = void(IGI_NATIVE_CDECL *)(std::decay_t<Args>...);
            const auto function = reinterpret_cast<Function>(address);
            auto values = std::make_tuple(std::forward<Args>(args)...);
            (void)caller->queue(nativeName, [function, values = std::move(values)]() mutable {
                std::apply(function, std::move(values));
            });
            return nullptr;
        } else {
            if (!caller->scheduler().isGameThread()) {
                ::igi::log(::igi::LogLevel::Warning,
                           "native with a return value refused outside the Game_RunHandler thread");
                return defaultResult<ReturnType>();
            }
            using Function = ReturnType(IGI_NATIVE_CDECL *)(std::decay_t<Args>...);
            const auto function = reinterpret_cast<Function>(address);
            try {
                ReturnType result = std::apply(function,
                    std::make_tuple(std::forward<Args>(args)...));
                ::igi::NativeCaller::reportSuccess(nativeName);
                return result;
            } catch (...) {
                ::igi::NativeCaller::reportException(nativeName);
                return defaultResult<ReturnType>();
            }
        }
    }

private:
    template <typename ReturnType>
    static ReturnType defaultResult() {
        if constexpr (std::is_pointer_v<ReturnType>) return nullptr;
        else return ReturnType{};
    }
};

inline NativeCaller g_NativeCaller;
} // namespace IGI

#define NATIVE_INVOKE ::IGI::g_NativeCaller.Invoke

namespace IGI::MISC {
NATIVE_DECL void FRAMES_SET(int frames) {
    NATIVE_INVOKE<Void>((Void)HASH::FRAMES_SET, frames);
}
NATIVE_DECL void GAMEMATERIAL_LOAD() {
    NATIVE_INVOKE<Void>((Void)HASH::GAMEMATERIAL_LOAD);
}
} // namespace IGI::MISC

namespace IGI::CONFIG {
NATIVE_DECL void READ(const char* path) {
    NATIVE_INVOKE<Void>((Void)HASH::CONFIG_READ, path);
}
} // namespace IGI::CONFIG

namespace IGI::LEVEL {
NATIVE_DECL void QUIT_GAME() {
    NATIVE_INVOKE<Void>((Void)HASH::LEVEL_QUIT);
}
} // namespace IGI::LEVEL

namespace IGI::SCRIPT {
NATIVE_DECL void COMPILE(const char* path) {
    NATIVE_INVOKE<Void>((Void)HASH::QSCRIPT_COMPILE, path);
}
NATIVE_DECL void SET_SYMBOL_CXT(const char* name, void* context) {
    NATIVE_INVOKE<Void>((Void)HASH::SCRIPT_SETSYMBOL_CXT, name, context);
}
} // namespace IGI::SCRIPT

namespace IGI::QVM {
NATIVE_DECL int ASSEMBLE(const char* path, const char* source) {
    return NATIVE_INVOKE<int>((Void)HASH::QSCRIPT_ASSEMBLE, path, source);
}
} // namespace IGI::QVM

namespace IGI::TASK {
NATIVE_DECL std::uint16_t TYPE_NEW(const char* name, int size, std::uint16_t parent, int poolCount) {
    return NATIVE_INVOKE<std::uint16_t>((Void)HASH::TASKTYPE_NEW, name, size, parent, poolCount);
}
} // namespace IGI::TASK

#undef IGI_NATIVE_CDECL
