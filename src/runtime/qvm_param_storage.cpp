#include "runtime/qvm_param_storage.hpp"

#include "natives/mvp_natives.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace igi {
namespace {
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
// Absolute addresses from IGI-RE addr.c for the non-ASLR retail igi.exe; they
// already include the 0x00400000 image base.
constexpr std::uintptr_t kMoveScale = 0x56E1E8;
constexpr std::uintptr_t kJumpSpeed = 0x56E1EC;
constexpr std::uintptr_t kJumpUp = 0x56E1F0;
constexpr std::uintptr_t kAirSpeed = 0x56E1F4;
constexpr std::uintptr_t kMaxDamage = 0x56E204;
constexpr std::uintptr_t kFenceDamage = 0x56E208;
constexpr std::uintptr_t kPerFrameHour = 0x5334A0;
constexpr std::uintptr_t kKmhToUnits = 0x5334A4;

bool readFloat(std::uintptr_t address, float &out) noexcept {
    SIZE_T read = 0;
    return ::ReadProcessMemory(::GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &out,
                               sizeof(out), &read) != FALSE &&
           read == sizeof(out);
}

bool writeFloat(std::uintptr_t address, float value) noexcept {
    SIZE_T written = 0;
    return ::WriteProcessMemory(::GetCurrentProcess(), reinterpret_cast<LPVOID>(address), &value,
                                sizeof(value), &written) != FALSE &&
           written == sizeof(value);
}
#endif
} // namespace

bool humanPlayerParamsAvailable() noexcept {
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    return ::GetModuleHandleA(nullptr) != nullptr;
#else
    return false;
#endif
}

std::optional<HumanPlayerQvmParams> readHumanPlayerQvmParams() {
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    if (!humanPlayerParamsAvailable())
        return std::nullopt;
    HumanPlayerQvmParams values;
    float rawMove = 0.0F;
    float rawJump = 0.0F;
    float rawUp = 0.0F;
    float rawAir = 0.0F;
    float rawDamage = 0.0F;
    float rawFence = 0.0F;
    if (!readFloat(kMoveScale, rawMove) || !readFloat(kJumpSpeed, rawJump) ||
        !readFloat(kJumpUp, rawUp) || !readFloat(kAirSpeed, rawAir) ||
        !readFloat(kMaxDamage, rawDamage) || !readFloat(kFenceDamage, rawFence)) {
        return std::nullopt;
    }
    float perFrameHour = 0.0F;
    float kmhToUnits = 0.0F;
    // The conversion constants are static data next to the globals. When either
    // is missing, fall back to the raw globals instead of inventing a scale.
    if (readFloat(kPerFrameHour, perFrameHour) && readFloat(kKmhToUnits, kmhToUnits) &&
        perFrameHour != 0.0F && kmhToUnits != 0.0F) {
        values.kmhScale = perFrameHour * kmhToUnits;
    }
    const auto toKmh = [&values](float raw) {
        return values.kmhScale != 0.0F ? raw / values.kmhScale : raw;
    };
    values.moveScale = rawMove;
    values.jumpSpeedKmh = toKmh(rawJump);
    values.jumpUpKmh = toKmh(rawUp);
    values.airSpeedKmh = toKmh(rawAir);
    values.maxDamage = rawDamage;
    values.fenceDamage = rawFence;
    values.live = true;
    return values;
#else
    return std::nullopt;
#endif
}

bool writeHumanPlayerQvmParams(const HumanPlayerQvmParams &values) {
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    if (!humanPlayerParamsAvailable() || !values.live)
        return false;
    const auto toRaw = [&values](float kmh) {
        return values.kmhScale != 0.0F ? kmh * values.kmhScale : kmh;
    };
    // Movement scale and jump/air speeds first. When one write fails, stop so a
    // half-applied physics change cannot leave the player in a mixed state.
    if (!writeFloat(kMoveScale, values.moveScale) ||
        !writeFloat(kJumpSpeed, toRaw(values.jumpSpeedKmh)) ||
        !writeFloat(kJumpUp, toRaw(values.jumpUpKmh)) ||
        !writeFloat(kAirSpeed, toRaw(values.airSpeedKmh)) ||
        !writeFloat(kMaxDamage, values.maxDamage)) {
        return false;
    }
    return writeFloat(kFenceDamage, values.fenceDamage);
#else
    (void)values;
    return false;
#endif
}

bool reloadHumanPlayerParameters() {
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    if (!humanPlayerParamsAvailable())
        return false;
    IGI::PLAYER::LOAD_PARAMETERS();
    return true;
#else
    return false;
#endif
}
} // namespace igi
