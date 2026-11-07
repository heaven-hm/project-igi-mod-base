#include "natives/mvp_natives.hpp"
#include "logging/logging.hpp"
#include "natives/igi_native_registry.hpp"
#include "natives/native_caller.hpp"
#include "natives/native_catalogue.hpp"
#include "runtime/scheduler.hpp"

#include <new>

#define IGI_ABI_EXPORT extern "C" __declspec(dllexport)

namespace {
alignas(igi::NativeCatalogue) unsigned char catalogueStorage[sizeof(igi::NativeCatalogue)];
alignas(igi::Scheduler) unsigned char schedulerStorage[sizeof(igi::Scheduler)];
alignas(igi::NativeCaller) unsigned char callerStorage[sizeof(igi::NativeCaller)];
igi::NativeCatalogue* catalogue = nullptr;
igi::Scheduler* scheduler = nullptr;
igi::NativeCaller* caller = nullptr;

struct RecordDefinition {
    std::uint32_t address;
    const char* name;
    const char* signature;
};
constexpr RecordDefinition records[] = {
    {0x00402820, "FramesSet", "void FramesSet(int fps)"},
    {0x00416110, "Game_SetRaining", "void Game_SetRaining(const char *weather)"},
    {0x00410360, "HumanPlayer_SetUnlimitedAmmo", "void HumanPlayer_SetUnlimitedAmmo(void)"},
    {0x00416010, "Game_SetDisplayMode", "void Game_SetDisplayMode(void)"},
    {0x00495e70, "Music_SetVolume", "void Music_SetVolume(float music, float master)"},
    {0x004167d0, "Game_GetEvent0", "uint8_t Game_GetEvent0(void)"},
    {0x004167e0, "Game_GetEvent1", "uint8_t Game_GetEvent1(void)"},
    {0x004167f0, "Game_GetEvent2", "uint8_t Game_GetEvent2(void)"},
    {0x00416810, "Game_GetEvent3", "uint8_t Game_GetEvent3(void)"},
    {0x00416920, "Game_GetWorld", "void *Game_GetWorld(void)"},
    {0x00416d40, "Game_Get", "Game *Game_Get(void)"},
    {0x004f0cf0, "Task_RunScript", "Task *Task_RunScript(Task *parent, const char *path, char insertHead)"},
};
}

IGI_ABI_EXPORT int igi_sdk_probe_initialize() {
    if (caller) return 1;
    catalogue = new (catalogueStorage) igi::NativeCatalogue();
    scheduler = new (schedulerStorage) igi::Scheduler();
    for (const auto& definition : records) {
        if (!catalogue->add({definition.address, definition.name, "IGI-RE test fixture", "entry",
                             definition.signature, igi::NativeVerification::Exposed})) return 0;
    }
    scheduler->bindGameThread();
    caller = new (callerStorage) igi::NativeCaller(*catalogue, *scheduler, true);
    igi::bindIgiNativeCaller(caller);
    igi::setLogLevel(igi::LogLevel::Critical);
    return 1;
}

IGI_ABI_EXPORT void igi_sdk_probe_frames_set(int fps) { IGI::MISC::FRAMES_SET(fps); }
IGI_ABI_EXPORT void igi_sdk_probe_set_raining(const char* weather) { IGI::GAME::SET_RAINING(weather); }
IGI_ABI_EXPORT void igi_sdk_probe_unlimited_ammo() { IGI::PLAYER::SET_UNLIMITED_AMMO(); }
IGI_ABI_EXPORT void igi_sdk_probe_display_mode() { IGI::GAME::SET_DISPLAY_MODE(); }
IGI_ABI_EXPORT void igi_sdk_probe_music_volume(float music, float master) { IGI::AUDIO::MUSIC_SET_VOLUME(music, master); }
IGI_ABI_EXPORT std::uint8_t igi_sdk_probe_get_event0() { return IGI::GAME::GET_EVENT0(); }
IGI_ABI_EXPORT std::uint8_t igi_sdk_probe_get_event1() { return IGI::GAME::GET_EVENT1(); }
IGI_ABI_EXPORT std::uint8_t igi_sdk_probe_get_event2() { return IGI::GAME::GET_EVENT2(); }
IGI_ABI_EXPORT std::uint8_t igi_sdk_probe_get_event3() { return IGI::GAME::GET_EVENT3(); }
IGI_ABI_EXPORT void* igi_sdk_probe_get_world() { return IGI::GAME::GET_WORLD(); }
IGI_ABI_EXPORT void* igi_sdk_probe_game_get() { return IGI::GAME::GET(); }
IGI_ABI_EXPORT void* igi_sdk_probe_run_script(void* parent, const char* path, char insertHead) {
    return IGI::TASK::RUN_SCRIPT(static_cast<IGI::Task*>(parent), path, insertHead);
}
