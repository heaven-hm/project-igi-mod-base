#include "natives/extra_natives.hpp"

#include "logging/logging.hpp"
#include "natives/igi_native_registry.hpp"
#include "natives/native_caller.hpp"

#include <type_traits>

namespace IGI {
namespace {
// Mirrors the generated binding contract: a call is refused unless it runs on
// the verified game thread and the address resolves with the exact signature.
template <class R, class... A>
R InvokeExtraNative(std::uint32_t address, const char *signature, A... args) {
    auto *caller = igi::igiNativeCaller();
    if (!caller || !caller->scheduler().isGameThread()) {
        igi::log(igi::LogLevel::Warning, "native refused: runtime/game thread unavailable");
        if constexpr (!std::is_void_v<R>)
            return R{};
        else
            return;
    }
    std::uintptr_t resolved = 0;
    const auto name = caller->nameForAddress(address);
    if (caller->resolveByAddress(address, signature, resolved) != igi::NativeCallStatus::Success) {
        if constexpr (!std::is_void_v<R>)
            return R{};
        else
            return;
    }
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    using Function = R(__cdecl *)(A...);
    const auto function = reinterpret_cast<Function>(resolved);
    try {
        if constexpr (std::is_void_v<R>) {
            function(args...);
            igi::NativeCaller::reportSuccess(name);
            return;
        } else {
            R result = function(args...);
            igi::NativeCaller::reportSuccess(name);
            return result;
        }
    } catch (...) {
        igi::NativeCaller::reportException(name);
    }
#endif
    if constexpr (!std::is_void_v<R>)
        return R{};
}
} // namespace

namespace WEAPON {
NATIVE_DECL Task *DROP(Task *w) {
    return InvokeExtraNative<Task *>(HASH::WEAPON_DROP_HANDLER, "Task *Weapon_Drop(Task *w)", w);
}
} // namespace WEAPON

namespace AI {
NATIVE_DECL char MOVE_TO(HumanAI *ai, int dest, const Vec3d *pos, char replan) {
    return InvokeExtraNative<char>(HASH::HUMAN_AI_MOVE_TO_HANDLER,
                                   "char HumanAI_MoveTo(HumanAI *ai, int dest, const Vec3d *pos, "
                                   "char replan)",
                                   ai, dest, pos, replan);
}

NATIVE_DECL char RUN_TO(HumanAI *ai, int dest, const Vec3d *pos, char replan) {
    return InvokeExtraNative<char>(HASH::HUMAN_AI_RUN_TO_HANDLER,
                                   "char HumanAI_RunTo(HumanAI *ai, int dest, const Vec3d *pos, "
                                   "char replan)",
                                   ai, dest, pos, replan);
}
} // namespace AI

namespace HUMAN {
NATIVE_DECL void STOP_WEAPON_ANIM(Human *h, float time) {
    InvokeExtraNative<void>(HASH::HUMAN_STOP_WEAPON_ANIM_HANDLER,
                            "void Human_StopWeaponAnim(Human *h, float time)", h, time);
}
} // namespace HUMAN

std::size_t ExtraBindingCount() noexcept {
    return 4;
}

bool loadExtraNativeBindings(igi::NativeCatalogue &catalogue) {
    if (!catalogue.add({HASH::WEAPON_DROP_HANDLER, "Weapon_Drop", "IGI-RE", "entry",
                        "Task *Weapon_Drop(Task *w)", igi::NativeVerification::Exposed}))
        return false;
    if (!catalogue.add({HASH::HUMAN_AI_MOVE_TO_HANDLER, "HumanAI_MoveTo", "IGI-RE", "entry",
                        "char HumanAI_MoveTo(HumanAI *ai, int dest, const Vec3d *pos, char replan)",
                        igi::NativeVerification::Exposed}))
        return false;
    if (!catalogue.add({HASH::HUMAN_AI_RUN_TO_HANDLER, "HumanAI_RunTo", "IGI-RE", "entry",
                        "char HumanAI_RunTo(HumanAI *ai, int dest, const Vec3d *pos, char replan)",
                        igi::NativeVerification::Exposed}))
        return false;
    return catalogue.add({HASH::HUMAN_STOP_WEAPON_ANIM_HANDLER, "Human_StopWeaponAnim", "IGI-RE",
                          "entry", "void Human_StopWeaponAnim(Human *h, float time)",
                          igi::NativeVerification::Exposed});
}
} // namespace IGI
