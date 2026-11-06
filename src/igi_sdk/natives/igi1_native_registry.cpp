#include "igi_sdk/natives/igi1_native_registry.hpp"

#include "igi_sdk/natives/native_caller.hpp"
#include "igi_sdk/natives/native_catalogue.hpp"

#include <atomic>

namespace {
std::atomic<igi::NativeCaller*> activeCaller{nullptr};
}

namespace igi {
bool loadIgi1VerifiedNativeRegistry(NativeCatalogue& catalogue) {
    // Addresses and signatures are from IGI-RE src/ and symbols/names.tsv.
    // Only wrappers with an exact current-name/signature mapping appear here.
    return catalogue.add({0x00402820, "FramesSet", "IGI-RE", "entry",
                          "void FramesSet(int fps)", NativeVerification::Exposed}) &&
           catalogue.add({0x00405850, "Config_Read", "IGI-RE", "entry",
                          "void Config_Read(const char *path)", NativeVerification::Exposed}) &&
           catalogue.add({0x00408350, "GameMaterial_LoadParameters", "IGI-RE", "entry",
                          "void GameMaterial_LoadParameters(void)", NativeVerification::Exposed}) &&
           catalogue.add({0x00416550, "LevelQuit", "IGI-RE", "entry",
                          "void LevelQuit(void)", NativeVerification::Exposed}) &&
           catalogue.add({0x004B8410, "Script_Compile", "IGI-RE", "entry",
                          "void Script_Compile(const char *path)", NativeVerification::Exposed}) &&
           catalogue.add({0x004B8930, "Script_SetSymbolContext", "IGI-RE", "entry",
                          "void Script_SetSymbolContext(const char *name, void *context)", NativeVerification::Exposed}) &&
           catalogue.add({0x004BB270, "QvmAsm_Assemble", "IGI-RE", "entry",
                          "int QvmAsm_Assemble(const char *path, const char *source)", NativeVerification::Exposed}) &&
           catalogue.add({0x00401900, "TaskType_New", "IGI-RE", "entry",
                          "uint16_t TaskType_New(const char *name, int size, uint16_t parent, int pool_count)", NativeVerification::Exposed});
}

void bindIgi1NativeCaller(NativeCaller* caller) noexcept {
    activeCaller.store(caller, std::memory_order_release);
}

NativeCaller* igi1NativeCaller() noexcept {
    return activeCaller.load(std::memory_order_acquire);
}
} // namespace igi
