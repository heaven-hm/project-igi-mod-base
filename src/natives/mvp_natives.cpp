// Generated typed calls from IGI-RE; implementations stay in .cpp.
#include "natives/mvp_natives.hpp"
#include "natives/igi_native_registry.hpp"
#include "natives/native_caller.hpp"
#include "logging/logging.hpp"
#include <type_traits>
#include <utility>
#define NATIVE_INVOKE InvokeNative
namespace IGI {
namespace {
template <class R, class... A>
R InvokeNative(std::uint32_t hash, const char *signature, A... args) {
    auto *caller = igi::igiNativeCaller();
    if (!caller || !caller->scheduler().isGameThread()) {
        igi::log(igi::LogLevel::Warning, "native refused: runtime/game thread unavailable");
        if constexpr (!std::is_void_v<R>)
            return R{};
        else
            return;
    }
    std::uintptr_t address = 0;
    const auto name = caller->nameForAddress(hash);
    if (caller->resolveByAddress(hash, signature, address) != igi::NativeCallStatus::Success) {
        if constexpr (!std::is_void_v<R>)
            return R{};
        else
            return;
    }
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    using Function = R(__cdecl *)(A...);
    const auto function = reinterpret_cast<Function>(address);
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
namespace MISC {
NATIVE_DECL void FRAMES_SET(int fps) {
    return NATIVE_INVOKE<Void>(HASH::FRAMES_SET, "void FramesSet(int fps)", fps);
}
NATIVE_DECL void LOAD_PARAMETERS() {
    return NATIVE_INVOKE<Void>(HASH::GAME_MATERIAL_LOAD_PARAMETERS,
                               "void GameMaterial_LoadParameters(void)");
}
NATIVE_DECL StatusMessage *STATUS_MESSAGE_SHOW(void *owner, const char *text, const char *portrait,
                                               const char *sound) {
    if (!owner) {
        owner = static_cast<void *>(LEVEL::GET_DYNAMIC());
    }
    if (!owner)
        return nullptr;
    return NATIVE_INVOKE<StatusMessage *>(HASH::STATUS_MESSAGE_SHOW,
                                          "StatusMessage *StatusMessageShow(void *owner, const "
                                          "char *text, const char *portrait, const char *sound)",
                                          owner, text, portrait, sound);
}
} // namespace MISC
namespace GAME {
NATIVE_DECL void SET_RAINING(const char *weather) {
    return NATIVE_INVOKE<Void>(HASH::GAME_SET_RAINING, "void Game_SetRaining(const char *weather)",
                               weather);
}
NATIVE_DECL void SET_DISPLAY_MODE() {
    return NATIVE_INVOKE<Void>(HASH::GAME_SET_DISPLAY_MODE, "void Game_SetDisplayMode(void)");
}
NATIVE_DECL void RESTART() {
    return NATIVE_INVOKE<Void>(HASH::GAME_RESTART, "void LevelRestart(void)");
}
NATIVE_DECL uint8_t GET_EVENT0() {
    return NATIVE_INVOKE<uint8_t>(HASH::GAME_GET_EVENT0, "uint8_t Game_GetEvent0(void)");
}
NATIVE_DECL uint8_t GET_EVENT1() {
    return NATIVE_INVOKE<uint8_t>(HASH::GAME_GET_EVENT1, "uint8_t Game_GetEvent1(void)");
}
NATIVE_DECL uint8_t GET_EVENT2() {
    return NATIVE_INVOKE<uint8_t>(HASH::GAME_GET_EVENT2, "uint8_t Game_GetEvent2(void)");
}
NATIVE_DECL uint8_t GET_EVENT3() {
    return NATIVE_INVOKE<uint8_t>(HASH::GAME_GET_EVENT3, "uint8_t Game_GetEvent3(void)");
}
NATIVE_DECL void *GET_WORLD() {
    return NATIVE_INVOKE<void *>(HASH::GAME_GET_WORLD, "void *Game_GetWorld(void)");
}
NATIVE_DECL Game *GET() {
    return NATIVE_INVOKE<Game *>(HASH::GAME_GET, "Game *Game_Get(void)");
}
NATIVE_DECL Task *GET_PLAYER(int i) {
    return NATIVE_INVOKE<Task *>(HASH::GAME_GET_PLAYER, "Task *Game_GetPlayer(int i)", i);
}
NATIVE_DECL char IS_PAUSED() {
    return NATIVE_INVOKE<char>(HASH::GAME_IS_PAUSED, "char Game_IsPaused(void)");
}
NATIVE_DECL void SET_PAUSED(char on) {
    return NATIVE_INVOKE<Void>(HASH::GAME_SET_PAUSED, "void Game_SetPaused(char on)", on);
}
NATIVE_DECL ScriptValue *SET_MISSION(ScriptValue *out, void *data, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::GAME_SET_MISSION,
        "ScriptValue *Game_SetMission(ScriptValue *out, void *data, void *args)", out, data, args);
}
NATIVE_DECL ScriptValue *SET_MUSIC_VOLUME(ScriptValue *out, void *data, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::GAME_SET_MUSIC_VOLUME,
        "ScriptValue *Game_SetMusicVolume(ScriptValue *out, void *data, void *args)", out, data,
        args);
}
NATIVE_DECL ScriptValue *SET_SFX_VOLUME(ScriptValue *out, void *data, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::GAME_SET_SFX_VOLUME,
        "ScriptValue *Game_SetSFXVolume(ScriptValue *out, void *data, void *args)", out, data,
        args);
}
NATIVE_DECL uint8_t GET_EVENT4() {
    return NATIVE_INVOKE<uint8_t>(HASH::GAME_GET_EVENT4, "uint8_t Game_GetEvent4(void)");
}
NATIVE_DECL uint8_t GET_EVENT6() {
    return NATIVE_INVOKE<uint8_t>(HASH::GAME_GET_EVENT6, "uint8_t Game_GetEvent6(void)");
}
NATIVE_DECL uint8_t GET_EVENT7() {
    return NATIVE_INVOKE<uint8_t>(HASH::GAME_GET_EVENT7, "uint8_t Game_GetEvent7(void)");
}
NATIVE_DECL uint8_t GET_EVENT9() {
    return NATIVE_INVOKE<uint8_t>(HASH::GAME_GET_EVENT9, "uint8_t Game_GetEvent9(void)");
}
NATIVE_DECL uint8_t GET_EVENT8() {
    return NATIVE_INVOKE<uint8_t>(HASH::GAME_GET_EVENT8, "uint8_t Game_GetEvent8(void)");
}
NATIVE_DECL uint8_t GET_EVENT11() {
    return NATIVE_INVOKE<uint8_t>(HASH::GAME_GET_EVENT11, "uint8_t Game_GetEvent11(void)");
}
NATIVE_DECL uint8_t GET_EVENT12() {
    return NATIVE_INVOKE<uint8_t>(HASH::GAME_GET_EVENT12, "uint8_t Game_GetEvent12(void)");
}
NATIVE_DECL uint8_t GET_EVENT13() {
    return NATIVE_INVOKE<uint8_t>(HASH::GAME_GET_EVENT13, "uint8_t Game_GetEvent13(void)");
}
NATIVE_DECL uint8_t GET_EVENT14() {
    return NATIVE_INVOKE<uint8_t>(HASH::GAME_GET_EVENT14, "uint8_t Game_GetEvent14(void)");
}
NATIVE_DECL void *GET_TASK_LIST0() {
    return NATIVE_INVOKE<void *>(HASH::GAME_GET_TASK_LIST0, "void *Game_GetTaskList0(void)");
}
NATIVE_DECL void *GET_TASK_LIST1() {
    return NATIVE_INVOKE<void *>(HASH::GAME_GET_TASK_LIST1, "void *Game_GetTaskList1(void)");
}
NATIVE_DECL void *GET_TASK_LIST2() {
    return NATIVE_INVOKE<void *>(HASH::GAME_GET_TASK_LIST2, "void *Game_GetTaskList2(void)");
}
NATIVE_DECL void *GET_TASK_LIST3() {
    return NATIVE_INVOKE<void *>(HASH::GAME_GET_TASK_LIST3, "void *Game_GetTaskList3(void)");
}
NATIVE_DECL void *GET_TASK_LIST4() {
    return NATIVE_INVOKE<void *>(HASH::GAME_GET_TASK_LIST4, "void *Game_GetTaskList4(void)");
}
} // namespace GAME
namespace PLAYER {
NATIVE_DECL void PICK_UP_WEAPON(Human *h, int *msg) {
    return NATIVE_INVOKE<Void>(HASH::WEAPON_GUN_PICKUP, "void WeaponGunPickup(Human *h, int *msg)",
                               h, msg);
}
NATIVE_DECL void SET_UNLIMITED_AMMO() {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_PLAYER_SET_UNLIMITED_AMMO,
                               "void HumanPlayer_SetUnlimitedAmmo(void)");
}
NATIVE_DECL void SET_FLAG_D44(HumanPlayer *h) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_PLAYER_SET_FLAG_D44,
                               "void HumanPlayer_SetFlagD44(HumanPlayer *h)", h);
}
NATIVE_DECL void SET_FLAG_D68(HumanPlayer *h) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_PLAYER_SET_FLAG_D68,
                               "void HumanPlayer_SetFlagD68(HumanPlayer *h)", h);
}
NATIVE_DECL void SET_WEAPON_ANIM(HumanPlayer *h, int anim) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_PLAYER_SET_WEAPON_ANIM,
                               "void HumanPlayer_SetWeaponAnim(HumanPlayer *h, int anim)", h, anim);
}
NATIVE_DECL uint16_t GET_TYPE() {
    return NATIVE_INVOKE<uint16_t>(HASH::HUMAN_PLAYER_GET_TYPE,
                                   "uint16_t HumanPlayer_GetType(void)");
}
NATIVE_DECL float GET_MOVE_SCALE() {
    return NATIVE_INVOKE<float>(HASH::HUMAN_PLAYER_GET_MOVE_SCALE,
                                "float HumanPlayer_GetMoveScale(void)");
}
NATIVE_DECL void CHECK_DEATH(HumanPlayer *p) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_PLAYER_CHECK_DEATH,
                               "void HumanPlayer_CheckDeath(HumanPlayer *p)", p);
}
NATIVE_DECL void PICKLOCK_NOISE(void *obj, HumanPlayer *h) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_PLAYER_PICKLOCK_NOISE,
                               "void HumanPlayer_PicklockNoise(void *obj, HumanPlayer *h)", obj, h);
}
NATIVE_DECL void CLEAR_FLAG_D44(HumanPlayer *h) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_PLAYER_CLEAR_FLAG_D44,
                               "void HumanPlayer_ClearFlagD44(HumanPlayer *h)", h);
}
NATIVE_DECL void CLEAR_FLAG_D68(HumanPlayer *h) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_PLAYER_CLEAR_FLAG_D68,
                               "void HumanPlayer_ClearFlagD68(HumanPlayer *h)", h);
}
NATIVE_DECL void START_ACTION(HumanPlayer *h, int action) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_PLAYER_START_ACTION,
                               "void HumanPlayer_StartAction(HumanPlayer *h, int action)", h,
                               action);
}
NATIVE_DECL void CYCLE_WEAPON(HumanPlayer *h, int force) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_PLAYER_CYCLE_WEAPON,
                               "void HumanPlayer_CycleWeapon(HumanPlayer *h, int force)", h, force);
}
NATIVE_DECL void SEND_POSITION(HumanPlayer *h) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_PLAYER_SEND_POSITION,
                               "void HumanPlayer_SendPosition(HumanPlayer *h)", h);
}
NATIVE_DECL void SELECT_WEAPON_CATEGORY(HumanPlayer *h) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_PLAYER_SELECT_WEAPON_CATEGORY,
                               "void HumanPlayer_SelectWeaponCategory(HumanPlayer *h)", h);
}
NATIVE_DECL void FADE_HIT_FLASH(HumanPlayer *h) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_PLAYER_FADE_HIT_FLASH,
                               "void HumanPlayer_FadeHitFlash(HumanPlayer *h)", h);
}
NATIVE_DECL void FADE_FLASH(HumanPlayer *h) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_PLAYER_FADE_FLASH,
                               "void HumanPlayer_FadeFlash(HumanPlayer *h)", h);
}
NATIVE_DECL void LOAD_PARAMETERS() {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_PLAYER_LOAD_PARAMETERS,
                               "void HumanPlayer_LoadParameters(void)");
}
NATIVE_DECL void REQUEST_WEAPON_ANIM(HumanPlayer *h, int anim) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_PLAYER_REQUEST_WEAPON_ANIM,
                               "void HumanPlayer_RequestWeaponAnim(HumanPlayer *h, int anim)", h,
                               anim);
}
} // namespace PLAYER
namespace HUMAN {
NATIVE_DECL ScriptValue *GET_CUT_SCENE_DELTA_TIME(ScriptValue *ret) {
    return NATIVE_INVOKE<ScriptValue *>(HASH::HUMAN_GET_CUT_SCENE_DELTA_TIME,
                                        "ScriptValue *Human_GetCutSceneDeltaTime(ScriptValue *ret)",
                                        ret);
}
NATIVE_DECL uint16_t GET_TYPE() {
    return NATIVE_INVOKE<uint16_t>(HASH::HUMAN_GET_TYPE, "uint16_t Human_GetType(void)");
}
NATIVE_DECL uint8_t GET_EVENT0() {
    return NATIVE_INVOKE<uint8_t>(HASH::HUMAN_GET_EVENT0, "uint8_t Human_GetEvent0(void)");
}
NATIVE_DECL uint8_t GET_EVENT1() {
    return NATIVE_INVOKE<uint8_t>(HASH::HUMAN_GET_EVENT1, "uint8_t Human_GetEvent1(void)");
}
NATIVE_DECL uint8_t GET_GUNSHOT_EVENT() {
    return NATIVE_INVOKE<uint8_t>(HASH::HUMAN_GET_GUNSHOT_EVENT,
                                  "uint8_t Human_GetGunshotEvent(void)");
}
NATIVE_DECL uint8_t GET_PUNCH_EVENT() {
    return NATIVE_INVOKE<uint8_t>(HASH::HUMAN_GET_PUNCH_EVENT, "uint8_t Human_GetPunchEvent(void)");
}
NATIVE_DECL void *GET_PARAM_TABLE() {
    return NATIVE_INVOKE<void *>(HASH::HUMAN_GET_PARAM_TABLE, "void *Human_GetParamTable(void)");
}
NATIVE_DECL void *GET_LIST() {
    return NATIVE_INVOKE<void *>(HASH::HUMAN_GET_LIST, "void *Human_GetList(void)");
}
NATIVE_DECL void SET_AI(Human *h, HumanAI *ai) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_SET_AI, "void Human_SetAI(Human *h, HumanAI *ai)", h,
                               ai);
}
NATIVE_DECL void RESET_DAMAGE(Human *h) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_RESET_DAMAGE, "void Human_ResetDamage(Human *h)", h);
}
NATIVE_DECL void GET_CAMERA_INFO(Human *h, HumanCameraInfo *info) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_GET_CAMERA_INFO,
                               "void Human_GetHumanCameraInfoHandler(Human *h, HumanCameraInfo "
                               "*info)",
                               h, info);
}
NATIVE_DECL int GET_STATE(Human *h) {
    return NATIVE_INVOKE<int>(HASH::HUMAN_GET_STATE, "int Human_GetState(Human *h)", h);
}
NATIVE_DECL int GET_INDEX(Human *h) {
    return NATIVE_INVOKE<int>(HASH::HUMAN_GET_INDEX, "int Human_GetIndex(Human *h)", h);
}
NATIVE_DECL int GET_TEAM(Human *h) {
    return NATIVE_INVOKE<int>(HASH::HUMAN_GET_TEAM, "int Human_GetTeam(Human *h)", h);
}
NATIVE_DECL float *GET_MATRIX(Human *h) {
    return NATIVE_INVOKE<float *>(HASH::HUMAN_GET_MATRIX, "float *Human_GetMatrix(Human *h)", h);
}
NATIVE_DECL char IS_PLAYER(Human *h) {
    return NATIVE_INVOKE<char>(HASH::HUMAN_IS_PLAYER, "char Human_IsPlayer(Human *h)", h);
}
NATIVE_DECL char IS_DEAD(Human *h) {
    return NATIVE_INVOKE<char>(HASH::HUMAN_IS_DEAD, "char Human_IsDead(Human *h)", h);
}
NATIVE_DECL char IS_CROUCHING(Human *h) {
    return NATIVE_INVOKE<char>(HASH::HUMAN_IS_CROUCHING, "char Human_IsCrouching(Human *h)", h);
}
NATIVE_DECL char IS_ACTIVE(Human *h) {
    return NATIVE_INVOKE<char>(HASH::HUMAN_IS_ACTIVE, "char Human_IsActive(Human *h)", h);
}
NATIVE_DECL void LOCK_INPUT(Human *h) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_LOCK_INPUT, "void Human_LockInput(Human *h)", h);
}
NATIVE_DECL void UNLOCK_INPUT(Human *h) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_UNLOCK_INPUT, "void Human_UnlockInput(Human *h)", h);
}
NATIVE_DECL void ENABLE_LOOK(Human *h) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_ENABLE_LOOK, "void Human_EnableLook(Human *h)", h);
}
NATIVE_DECL void DISABLE_LOOK(Human *h) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_DISABLE_LOOK, "void Human_DisableLook(Human *h)", h);
}
NATIVE_DECL void VIEW_CAM(Human *h, int mode) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_VIEW_CAM, "void HumanViewCam(Human *h, int mode)", h,
                               mode);
}
} // namespace HUMAN
namespace SOLDIER {
NATIVE_DECL void GET_PARAMS(Human *h, void **out) {
    return NATIVE_INVOKE<Void>(HASH::SOLDIER_GET_PARAMS,
                               "void Soldier_GetParams(Human *h, void **out)", h, out);
}
NATIVE_DECL uint16_t GET_TYPE() {
    return NATIVE_INVOKE<uint16_t>(HASH::SOLDIER_GET_TYPE, "uint16_t Soldier_GetType(void)");
}
NATIVE_DECL void GET_DEATH_VELOCITY(double *out, Human *h, int model, int anim) {
    return NATIVE_INVOKE<Void>(
        HASH::SOLDIER_GET_DEATH_VELOCITY,
        "void Soldier_GetDeathVelocity(double *out, Human *h, int model, int anim)", out, h, model,
        anim);
}
NATIVE_DECL void FOOTSTEP(Soldier *h) {
    return NATIVE_INVOKE<Void>(HASH::SOLDIER_FOOTSTEP, "void Soldier_Footstep(Soldier *h)", h);
}
NATIVE_DECL void ENSURE_WEAPON(Human *h) {
    return NATIVE_INVOKE<Void>(HASH::SOLDIER_ENSURE_WEAPON, "void Soldier_EnsureWeapon(Human *h)",
                               h);
}
NATIVE_DECL char TURN_TO_HIT(Human *h, double *dir) {
    return NATIVE_INVOKE<char>(HASH::SOLDIER_TURN_TO_HIT,
                               "char Soldier_TurnToHit(Human *h, double *dir)", h, dir);
}
NATIVE_DECL void STAND_ENTER(SoldierSM *sm, Soldier *h) {
    return NATIVE_INVOKE<Void>(HASH::SOLDIER_STAND_ENTER,
                               "void Soldier_StandEnter(SoldierSM *sm, Soldier *h)", sm, h);
}
NATIVE_DECL void STATE1_ENTER(SoldierSM *sm, Soldier *h) {
    return NATIVE_INVOKE<Void>(HASH::SOLDIER_STATE1_ENTER,
                               "void Soldier_State1Enter(SoldierSM *sm, Soldier *h)", sm, h);
}
NATIVE_DECL void STATE17_ENTER(SoldierSM *sm, Soldier *h) {
    return NATIVE_INVOKE<Void>(HASH::SOLDIER_STATE17_ENTER,
                               "void Soldier_State17Enter(SoldierSM *sm, Soldier *h)", sm, h);
}
NATIVE_DECL void STATE18_ENTER(SoldierSM *sm, Soldier *h) {
    return NATIVE_INVOKE<Void>(HASH::SOLDIER_STATE18_ENTER,
                               "void Soldier_State18Enter(SoldierSM *sm, Soldier *h)", sm, h);
}
NATIVE_DECL void STATE18_UPDATE(SoldierSM *sm, Soldier *h) {
    return NATIVE_INVOKE<Void>(HASH::SOLDIER_STATE18_UPDATE,
                               "void Soldier_State18Update(SoldierSM *sm, Soldier *h)", sm, h);
}
NATIVE_DECL void STATE2_ENTER(SoldierSM *sm, Soldier *h) {
    return NATIVE_INVOKE<Void>(HASH::SOLDIER_STATE2_ENTER,
                               "void Soldier_State2Enter(SoldierSM *sm, Soldier *h)", sm, h);
}
NATIVE_DECL void STATE2_UPDATE(SoldierSM *sm, Soldier *h) {
    return NATIVE_INVOKE<Void>(HASH::SOLDIER_STATE2_UPDATE,
                               "void Soldier_State2Update(SoldierSM *sm, Soldier *h)", sm, h);
}
NATIVE_DECL void STATE3_ENTER(SoldierSM *sm, Soldier *h) {
    return NATIVE_INVOKE<Void>(HASH::SOLDIER_STATE3_ENTER,
                               "void Soldier_State3Enter(SoldierSM *sm, Soldier *h)", sm, h);
}
NATIVE_DECL void STATE4_ENTER(SoldierSM *sm, Soldier *h) {
    return NATIVE_INVOKE<Void>(HASH::SOLDIER_STATE4_ENTER,
                               "void Soldier_State4Enter(SoldierSM *sm, Soldier *h)", sm, h);
}
NATIVE_DECL void STATE5_ENTER(SoldierSM *sm, Soldier *h) {
    return NATIVE_INVOKE<Void>(HASH::SOLDIER_STATE5_ENTER,
                               "void Soldier_State5Enter(SoldierSM *sm, Soldier *h)", sm, h);
}
NATIVE_DECL void STATE6_ENTER(SoldierSM *sm, Soldier *h) {
    return NATIVE_INVOKE<Void>(HASH::SOLDIER_STATE6_ENTER,
                               "void Soldier_State6Enter(SoldierSM *sm, Soldier *h)", sm, h);
}
} // namespace SOLDIER
namespace WEAPON {
NATIVE_DECL int HAS_AMMO(void *task) {
    return NATIVE_INVOKE<int>(HASH::WEAPON_HAS_AMMO, "int Weapon_HasAmmo(void *task)", task);
}
NATIVE_DECL uint8_t GET_FIRE_EVENT() {
    return NATIVE_INVOKE<uint8_t>(HASH::WEAPON_GET_FIRE_EVENT, "uint8_t Weapon_GetFireEvent(void)");
}
NATIVE_DECL uint8_t GET_TYPE_EVENT() {
    return NATIVE_INVOKE<uint8_t>(HASH::WEAPON_GET_TYPE_EVENT, "uint8_t Weapon_GetTypeEvent(void)");
}
NATIVE_DECL uint8_t GET_UNUSED_POINTS_EVENT() {
    return NATIVE_INVOKE<uint8_t>(HASH::WEAPON_GET_UNUSED_POINTS_EVENT,
                                  "uint8_t Weapon_GetUnusedPointsEvent(void)");
}
NATIVE_DECL WeaponType *WEAPON_GET_TYPE(void *task) {
    return NATIVE_INVOKE<WeaponType *>(HASH::WEAPON_GET_TYPE,
                                       "WeaponType *Weapon_GetType(void *task)", task);
}
NATIVE_DECL uint16_t GUN_GET_TYPE() {
    return NATIVE_INVOKE<uint16_t>(HASH::GUN_GET_TYPE, "uint16_t Gun_GetType(void)");
}
NATIVE_DECL uint8_t GET_EVENT0() {
    return NATIVE_INVOKE<uint8_t>(HASH::GUN_GET_EVENT0, "uint8_t Gun_GetEvent0(void)");
}
NATIVE_DECL uint8_t GET_EVENT1() {
    return NATIVE_INVOKE<uint8_t>(HASH::GUN_GET_EVENT1, "uint8_t Gun_GetEvent1(void)");
}
NATIVE_DECL uint8_t GET_EVENT2() {
    return NATIVE_INVOKE<uint8_t>(HASH::GUN_GET_EVENT2, "uint8_t Gun_GetEvent2(void)");
}
NATIVE_DECL uint8_t GUN_GET_EVENT3() {
    return NATIVE_INVOKE<uint8_t>(HASH::GUN_GET_EVENT3, "uint8_t Gun_GetEvent3(void)");
}
NATIVE_DECL uint8_t GET_EVENT4() {
    return NATIVE_INVOKE<uint8_t>(HASH::GUN_GET_EVENT4, "uint8_t Gun_GetEvent4(void)");
}
NATIVE_DECL void GET_AMMO_TEXT_POS(float *x, float *y, Gun *g, Hud *hud) {
    return NATIVE_INVOKE<Void>(HASH::GUN_GET_AMMO_TEXT_POS,
                               "void Gun_GetAmmoTextPos(float *x, float *y, Gun *g, Hud *hud)", x,
                               y, g, hud);
}
NATIVE_DECL char GET_SLOTS_EVENT() {
    return NATIVE_INVOKE<char>(HASH::WEAPON_GET_SLOTS_EVENT, "char Weapon_GetSlotsEvent(void)");
}
NATIVE_DECL char GET_STATE_EVENT() {
    return NATIVE_INVOKE<char>(HASH::WEAPON_GET_STATE_EVENT, "char Weapon_GetStateEvent(void)");
}
NATIVE_DECL char GET_MOVE_EVENT() {
    return NATIVE_INVOKE<char>(HASH::WEAPON_GET_MOVE_EVENT, "char Weapon_GetMoveEvent(void)");
}
NATIVE_DECL char GET_TRIGGER_EVENT() {
    return NATIVE_INVOKE<char>(HASH::WEAPON_GET_TRIGGER_EVENT, "char Weapon_GetTriggerEvent(void)");
}
NATIVE_DECL char WEAPON_GET_EVENT3() {
    return NATIVE_INVOKE<char>(HASH::WEAPON_GET_EVENT3, "char Weapon_GetEvent3(void)");
}
} // namespace WEAPON
namespace CAMERA {
NATIVE_DECL void SET_WEAPON_TARGET(HumanView *v, float pitch, float yaw, float pitchSpeed,
                                   float yawSpeed) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_VIEW_SET_WEAPON_TARGET,
                               "void HumanView_SetWeaponTarget(HumanView *v, float pitch, float "
                               "yaw, float pitchSpeed, float yawSpeed)",
                               v, pitch, yaw, pitchSpeed, yawSpeed);
}
NATIVE_DECL void SET_WEAPON_ANGLES(HumanView *v, float pitch, float yaw) {
    return NATIVE_INVOKE<Void>(
        HASH::HUMAN_VIEW_SET_WEAPON_ANGLES,
        "void HumanView_SetWeaponAngles(HumanView *v, float pitch, float yaw)", v, pitch, yaw);
}
NATIVE_DECL void GET_EYE(double *out, HumanView *v) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_VIEW_GET_EYE,
                               "void HumanView_GetEye(double *out, HumanView *v)", out, v);
}
NATIVE_DECL void GET_MATRIX(Matrix *out, HumanView *v) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_VIEW_GET_MATRIX,
                               "void HumanView_GetMatrix(Matrix *out, HumanView *v)", out, v);
}
NATIVE_DECL void GET_FORWARD(float *out, HumanView *v) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_VIEW_GET_FORWARD,
                               "void HumanView_GetForward(float *out, HumanView *v)", out, v);
}
NATIVE_DECL void SET_CAMERA(HumanView *v, const double *pos, const Matrix *rot) {
    return NATIVE_INVOKE<Void>(
        HASH::HUMAN_VIEW_SET_CAMERA,
        "void HumanView_SetCamera(HumanView *v, const double *pos, const Matrix *rot)", v, pos,
        rot);
}
NATIVE_DECL void SET_TARGET(HumanView *v, const int *target, char flag) {
    return NATIVE_INVOKE<Void>(
        HASH::HUMAN_VIEW_SET_TARGET,
        "void HumanView_SetTarget(HumanView *v, const int *target, char flag)", v, target, flag);
}
NATIVE_DECL void SET_DIP(HumanView *v, float dip) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_VIEW_SET_DIP,
                               "void HumanView_SetDip(HumanView *v, float dip)", v, dip);
}
NATIVE_DECL void SET_ZOOMED(HumanView *v) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_VIEW_SET_ZOOMED,
                               "void HumanView_SetZoomed(HumanView *v)", v);
}
NATIVE_DECL int HAS_KILLER(HumanCamera *c) {
    return NATIVE_INVOKE<int>(HASH::HUMAN_CAMERA_HAS_KILLER,
                              "int HumanCamera_HasKiller(HumanCamera *c)", c);
}
NATIVE_DECL void FIND_HUMAN(HumanCamera *c) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_CAMERA_FIND_HUMAN,
                               "void HumanCamera_FindHuman(HumanCamera *c)", c);
}
NATIVE_DECL char IS_BLOCKED(HumanCamera *c, Task *h, Vec3d *to, Vec3d *from) {
    return NATIVE_INVOKE<char>(
        HASH::HUMAN_CAMERA_IS_BLOCKED,
        "char HumanCamera_IsBlocked(HumanCamera *c, Task *h, Vec3d *to, Vec3d *from)", c, h, to,
        from);
}
NATIVE_DECL uint8_t GET_EVENT() {
    return NATIVE_INVOKE<uint8_t>(HASH::HUMAN_CAMERA_GET_EVENT,
                                  "uint8_t HumanCamera_GetEvent(void)");
}
NATIVE_DECL void SET_HUMAN(HumanCamera *c, Task *h) {
    return NATIVE_INVOKE<Void>(HASH::HUMAN_CAMERA_SET_HUMAN,
                               "void HumanCamera_SetHuman(HumanCamera *c, Task *h)", c, h);
}
NATIVE_DECL uint8_t GET_CHANGED_EVENT() {
    return NATIVE_INVOKE<uint8_t>(HASH::CAMERA_GET_CHANGED_EVENT,
                                  "uint8_t Camera_GetChangedEvent(void)");
}
NATIVE_DECL QTaskList *GET_LISTENERS() {
    return NATIVE_INVOKE<QTaskList *>(HASH::CAMERA_GET_LISTENERS,
                                      "QTaskList *Camera_GetListeners(void)");
}
NATIVE_DECL void SET(Camera *cam, const Vec3d *pos, const Matrix *axes, float fov, float aspect,
                     float priority, int frame, void *owner, int unk58, int unk5C) {
    return NATIVE_INVOKE<Void>(
        HASH::CAMERA_SET,
        "void Camera_Set(Camera *cam, const Vec3d *pos, const Matrix *axes, float fov, float "
        "aspect, float priority, int frame, void *owner, int unk58, int unk5C)",
        cam, pos, axes, fov, aspect, priority, frame, owner, unk58, unk5C);
}
} // namespace CAMERA
namespace VEHICLE {
NATIVE_DECL void GET_DEF_SIZE(void *unused, int *size) {
    return NATIVE_INVOKE<Void>(HASH::CAR_GET_DEF_SIZE,
                               "void Car_GetDefSize(void *unused, int *size)", unused, size);
}
NATIVE_DECL void CAR_GET_PARAM_TABLE(Car *o, void **out) {
    return NATIVE_INVOKE<Void>(HASH::CAR_GET_PARAM_TABLE,
                               "void Car_GetParamTable(Car *o, void **out)", o, out);
}
NATIVE_DECL void CAR_GET_SOUND_PARAMS(Car *c, void **out) {
    return NATIVE_INVOKE<Void>(HASH::CAR_GET_SOUND_PARAMS,
                               "void Car_GetSoundParams(Car *c, void **out)", c, out);
}
NATIVE_DECL void CAR_GET_CONTROL_INFO(Car *c, int *arg) {
    return NATIVE_INVOKE<Void>(HASH::CAR_GET_CONTROL_INFO,
                               "void Car_GetControlInfo(Car *c, int *arg)", c, arg);
}
NATIVE_DECL void GET_FRAME(Car *c, void **out) {
    return NATIVE_INVOKE<Void>(HASH::CAR_GET_FRAME, "void Car_GetFrame(Car *c, void **out)", c,
                               out);
}
NATIVE_DECL void GET_LIGHT(Car *c, void **out) {
    return NATIVE_INVOKE<Void>(HASH::CAR_GET_LIGHT, "void Car_GetLight(Car *c, void **out)", c,
                               out);
}
NATIVE_DECL uint16_t GET_TYPE() {
    return NATIVE_INVOKE<uint16_t>(HASH::CAR_GET_TYPE, "uint16_t Car_GetType(void)");
}
NATIVE_DECL Task *GET_MOUNTED_GUN(Car *c) {
    return NATIVE_INVOKE<Task *>(HASH::CAR_GET_MOUNTED_GUN, "Task *Car_GetMountedGun(Car *c)", c);
}
NATIVE_DECL void GET_GUNNER_POS(Car *c, Vec3d *pos, Matrix *m) {
    return NATIVE_INVOKE<Void>(HASH::CAR_GET_GUNNER_POS,
                               "void Car_GetGunnerPos(Car *c, Vec3d *pos, Matrix *m)", c, pos, m);
}
NATIVE_DECL long double GET_TRACK_FACTOR(Car *c, int joint) {
    return NATIVE_INVOKE<long double>(
        HASH::CAR_GET_TRACK_FACTOR, "long double Car_GetTrackFactor(Car *c, int joint)", c, joint);
}
NATIVE_DECL void CAR_EXPLODE(Car *car) {
    return NATIVE_INVOKE<Void>(HASH::CAR_EXPLODE, "void Car_Explode(Car *c)", car);
}
NATIVE_DECL Task *FIND_DOOR(Heli *h) {
    return NATIVE_INVOKE<Task *>(HASH::HELI_FIND_DOOR, "Task *Heli_FindDoor(Heli *h)", h);
}
NATIVE_DECL void HELI_GET_PARAM_TABLE(Heli *h, void **out) {
    return NATIVE_INVOKE<Void>(HASH::HELI_GET_PARAM_TABLE,
                               "void Heli_GetParamTable(Heli *h, void **out)", h, out);
}
NATIVE_DECL void HELI_GET_SOUND_PARAMS(Heli *h, void **out) {
    return NATIVE_INVOKE<Void>(HASH::HELI_GET_SOUND_PARAMS,
                               "void Heli_GetSoundParams(Heli *h, void **out)", h, out);
}
NATIVE_DECL void GET_STATE_SIZE(Heli *h, int *out) {
    return NATIVE_INVOKE<Void>(HASH::HELI_GET_STATE_SIZE,
                               "void Heli_GetStateSize(Heli *h, int *out)", h, out);
}
NATIVE_DECL void HELI_GET_CONTROL_INFO(Heli *h, int *arg) {
    return NATIVE_INVOKE<Void>(HASH::HELI_GET_CONTROL_INFO,
                               "void Heli_GetControlInfo(Heli *h, int *arg)", h, arg);
}
NATIVE_DECL void GET_CONFIG_SIZE(Task *t, int *out) {
    return NATIVE_INVOKE<Void>(HASH::HELI_GET_CONFIG_SIZE,
                               "void Heli_GetConfigSize(Task *t, int *out)", t, out);
}
NATIVE_DECL void GET_SOUND_INFO(Heli *h, void **out) {
    return NATIVE_INVOKE<Void>(HASH::HELI_GET_SOUND_INFO,
                               "void Heli_GetSoundInfo(Heli *h, void **out)", h, out);
}
NATIVE_DECL void RESET_CONTROLS(void *vehicle) {
    return NATIVE_INVOKE<Void>(HASH::VEHICLE_RESET_CONTROLS,
                               "void Vehicle_ResetControls(Vehicle *v)", vehicle);
}
} // namespace VEHICLE
namespace GRAPH {
NATIVE_DECL void *GET_PARAM_TABLE() {
    return NATIVE_INVOKE<void *>(HASH::GRAPH_GET_PARAM_TABLE, "void *Graph_GetParamTable(void)");
}
NATIVE_DECL uint16_t GET_TYPE() {
    return NATIVE_INVOKE<uint16_t>(HASH::GRAPH_GET_TYPE, "uint16_t Graph_GetType(void)");
}
NATIVE_DECL uint8_t GET_LOAD_EVENT() {
    return NATIVE_INVOKE<uint8_t>(HASH::GRAPH_GET_LOAD_EVENT, "uint8_t Graph_GetLoadEvent(void)");
}
NATIVE_DECL uint8_t GET_SAVE_EVENT() {
    return NATIVE_INVOKE<uint8_t>(HASH::GRAPH_GET_SAVE_EVENT, "uint8_t Graph_GetSaveEvent(void)");
}
NATIVE_DECL uint8_t GET_NODE_LOAD_EVENT() {
    return NATIVE_INVOKE<uint8_t>(HASH::GRAPH_GET_NODE_LOAD_EVENT,
                                  "uint8_t Graph_GetNodeLoadEvent(void)");
}
NATIVE_DECL uint8_t GET_UNUSED_EVENT0() {
    return NATIVE_INVOKE<uint8_t>(HASH::GRAPH_GET_UNUSED_EVENT0,
                                  "uint8_t Graph_GetUnusedEvent0(void)");
}
NATIVE_DECL uint8_t GET_NODE_ADD_EVENT() {
    return NATIVE_INVOKE<uint8_t>(HASH::GRAPH_GET_NODE_ADD_EVENT,
                                  "uint8_t Graph_GetNodeAddEvent(void)");
}
NATIVE_DECL uint8_t GET_UNUSED_EVENT1() {
    return NATIVE_INVOKE<uint8_t>(HASH::GRAPH_GET_UNUSED_EVENT1,
                                  "uint8_t Graph_GetUnusedEvent1(void)");
}
NATIVE_DECL uint8_t GET_ALLOC_EVENT() {
    return NATIVE_INVOKE<uint8_t>(HASH::GRAPH_GET_ALLOC_EVENT, "uint8_t Graph_GetAllocEvent(void)");
}
NATIVE_DECL Graph *FIND(int id) {
    return NATIVE_INVOKE<Graph *>(HASH::GRAPH_FIND, "Graph *Graph_Find(int id)", id);
}
NATIVE_DECL void GET_WORLD_POS(Vec3d *out, Graph *g) {
    return NATIVE_INVOKE<Void>(HASH::GRAPH_GET_WORLD_POS,
                               "void Graph_GetWorldPos(Vec3d *out, Graph *g)", out, g);
}
NATIVE_DECL void SET_WORLD_POS(Graph *g, const Vec3d *pos) {
    return NATIVE_INVOKE<Void>(HASH::GRAPH_SET_WORLD_POS,
                               "void Graph_SetWorldPos(Graph *g, const Vec3d *pos)", g, pos);
}
NATIVE_DECL void SET_NODE_SIZE(uint16_t type, int size) {
    return NATIVE_INVOKE<Void>(HASH::GRAPH_SET_NODE_SIZE,
                               "void Graph_SetNodeSize(uint16_t type, int size)", type, size);
}
NATIVE_DECL int GET_NODE_SIZE(Graph *g) {
    return NATIVE_INVOKE<int>(HASH::GRAPH_GET_NODE_SIZE, "int Graph_GetNodeSize(Graph *g)", g);
}
NATIVE_DECL GraphNode *GET_NODE(Graph *g, int id) {
    return NATIVE_INVOKE<GraphNode *>(HASH::GRAPH_GET_NODE,
                                      "GraphNode *Graph_GetNode(Graph *g, int id)", g, id);
}
NATIVE_DECL void GET_NODE_POS(Vec3d *out, GraphNode *node) {
    return NATIVE_INVOKE<Void>(HASH::GRAPH_GET_NODE_POS,
                               "void Graph_GetNodePos(Vec3d *out, GraphNode *node)", out, node);
}
NATIVE_DECL void GET_NODE_MATRIX(Matrix *out, GraphNode *node, char rotate) {
    return NATIVE_INVOKE<Void>(
        HASH::GRAPH_GET_NODE_MATRIX,
        "void Graph_GetNodeMatrix(Matrix *out, GraphNode *node, char rotate)", out, node, rotate);
}
} // namespace GRAPH
namespace AI {
NATIVE_DECL ScriptValue *REMOVE_ALARM_ACTIONS(ScriptValue *out, void *data, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::AI_FUNCTION_REMOVE_ALARM_ACTIONS,
        "ScriptValue *AIFunction_RemoveAlarmActions(ScriptValue *out, void *data, void *args)", out,
        data, args);
}
NATIVE_DECL ScriptValue *SET_VIEW_LENGTH(ScriptValue *out, void *data, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::AI_FUNCTION_SET_VIEW_LENGTH,
        "ScriptValue *AIFunction_SetViewLength(ScriptValue *out, void *data, void *args)", out,
        data, args);
}
NATIVE_DECL ScriptValue *SET_ALARM_VIEW_LENGTH(ScriptValue *out, void *data, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::AI_FUNCTION_SET_ALARM_VIEW_LENGTH,
        "ScriptValue *AIFunction_SetAlarmViewLength(ScriptValue *out, void *data, void *args)", out,
        data, args);
}
NATIVE_DECL ScriptValue *SET_VIEW_ALPHA(ScriptValue *out, void *data, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::AI_FUNCTION_SET_VIEW_ALPHA,
        "ScriptValue *AIFunction_SetViewAlpha(ScriptValue *out, void *data, void *args)", out, data,
        args);
}
NATIVE_DECL ScriptValue *SET_VIEW_GAMMA(ScriptValue *out, void *data, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::AI_FUNCTION_SET_VIEW_GAMMA,
        "ScriptValue *AIFunction_SetViewGamma(ScriptValue *out, void *data, void *args)", out, data,
        args);
}
NATIVE_DECL ScriptValue *SET_SECONDARY_VIEW_LENGTH(ScriptValue *out, void *data, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::AI_FUNCTION_SET_SECONDARY_VIEW_LENGTH,
        "ScriptValue *AIFunction_SetSecondaryViewLength(ScriptValue *out, void *data, void *args)",
        out, data, args);
}
NATIVE_DECL ScriptValue *SET_SECONDARY_ALARM_VIEW_LENGTH(ScriptValue *out, void *data, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::AI_FUNCTION_SET_SECONDARY_ALARM_VIEW_LENGTH,
        "ScriptValue *AIFunction_SetSecondaryAlarmViewLength(ScriptValue *out, void *data, void "
        "*args)",
        out, data, args);
}
NATIVE_DECL ScriptValue *SET_SECONDARY_VIEW_ALPHA(ScriptValue *out, void *data, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::AI_FUNCTION_SET_SECONDARY_VIEW_ALPHA,
        "ScriptValue *AIFunction_SetSecondaryViewAlpha(ScriptValue *out, void *data, void *args)",
        out, data, args);
}
NATIVE_DECL ScriptValue *SET_SECONDARY_VIEW_GAMMA(ScriptValue *out, void *data, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::AI_FUNCTION_SET_SECONDARY_VIEW_GAMMA,
        "ScriptValue *AIFunction_SetSecondaryViewGamma(ScriptValue *out, void *data, void *args)",
        out, data, args);
}
NATIVE_DECL ScriptValue *GET_CURRENT_EVENT_TYPE(ScriptValue *out, void *data, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::AI_FUNCTION_GET_CURRENT_EVENT_TYPE,
        "ScriptValue *AIFunction_GetCurrentEventType(ScriptValue *out, void *data, void *args)",
        out, data, args);
}
NATIVE_DECL ScriptValue *GET_RANDOM_VALUE(ScriptValue *out, void *data, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::AI_FUNCTION_GET_RANDOM_VALUE,
        "ScriptValue *AIFunction_GetRandomValue(ScriptValue *out, void *data, void *args)", out,
        data, args);
}
NATIVE_DECL ScriptValue *SET_EVENT_PRIORITY(ScriptValue *out, void *data, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::AI_FUNCTION_SET_EVENT_PRIORITY,
        "ScriptValue *AIFunction_SetEventPriority(ScriptValue *out, void *data, void *args)", out,
        data, args);
}
NATIVE_DECL ScriptValue *SET_INVULNERABILITY(ScriptValue *out, void *data, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::AI_FUNCTION_SET_INVULNERABILITY,
        "ScriptValue *AIFunction_SetInvulnerability(ScriptValue *out, void *data, void *args)", out,
        data, args);
}
NATIVE_DECL ScriptValue *SET_INSTANT_DEATH(ScriptValue *out, void *data, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::AI_FUNCTION_SET_INSTANT_DEATH,
        "ScriptValue *AIFunction_SetInstantDeath(ScriptValue *out, void *data, void *args)", out,
        data, args);
}
NATIVE_DECL ScriptValue *SET_DEATH_ANIMATION(ScriptValue *out, void *data, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::AI_FUNCTION_SET_DEATH_ANIMATION,
        "ScriptValue *AIFunction_SetDeathAnimation(ScriptValue *out, void *data, void *args)", out,
        data, args);
}
NATIVE_DECL ScriptValue *SET_ALARM_TRIGGER_ID(ScriptValue *out, void *data, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::AI_FUNCTION_SET_ALARM_TRIGGER_ID,
        "ScriptValue *AIFunction_SetAlarmTriggerID(ScriptValue *out, void *data, void *args)", out,
        data, args);
}
NATIVE_DECL ScriptValue *SET_ALARM_CONTROL_ID(ScriptValue *out, void *data, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::AI_FUNCTION_SET_ALARM_CONTROL_ID,
        "ScriptValue *AIFunction_SetAlarmControlID(ScriptValue *out, void *data, void *args)", out,
        data, args);
}
NATIVE_DECL ScriptValue *SET_ALARM_ACCESS(ScriptValue *out, void *data, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::AI_FUNCTION_SET_ALARM_ACCESS,
        "ScriptValue *AIFunction_SetAlarmAccess(ScriptValue *out, void *data, void *args)", out,
        data, args);
}
NATIVE_DECL ScriptValue *SET_GUNNER_ID(ScriptValue *out, void *data, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::AI_FUNCTION_SET_GUNNER_ID,
        "ScriptValue *AIFunction_SetGunnerID(ScriptValue *out, void *data, void *args)", out, data,
        args);
}
NATIVE_DECL ScriptValue *SET_ANIMATION_INTERVAL(ScriptValue *out, void *data, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::AI_FUNCTION_SET_ANIMATION_INTERVAL,
        "ScriptValue *AIFunction_SetAnimationInterval(ScriptValue *out, void *data, void *args)",
        out, data, args);
}
} // namespace AI
namespace AUDIO {
NATIVE_DECL void MUSIC_SET_VOLUME(float music, float master) {
    return NATIVE_INVOKE<Void>(HASH::MUSIC_SET_VOLUME,
                               "void Music_SetVolume(float music, float master)", music, master);
}
NATIVE_DECL char IS_READY() {
    return NATIVE_INVOKE<char>(HASH::SOUND_IS_READY, "char Sound_IsReady(void)");
}
NATIVE_DECL void SET_SFX_VOLUME(float v) {
    return NATIVE_INVOKE<Void>(HASH::MUSIC_SET_SFX_VOLUME, "void Music_SetSfxVolume(float v)", v);
}
NATIVE_DECL Sound *FIND(int key1, int key2) {
    return NATIVE_INVOKE<Sound *>(HASH::SOUND_FIND, "Sound *Sound_Find(int key1, int key2)", key1,
                                  key2);
}
NATIVE_DECL long double GET_DURATION(int key1, int key2) {
    return NATIVE_INVOKE<long double>(
        HASH::SOUND_GET_DURATION, "long double Sound_GetDuration(int key1, int key2)", key1, key2);
}
NATIVE_DECL void SET_LISTENER(const float *m, const double *pos, const double *vel) {
    return NATIVE_INVOKE<Void>(
        HASH::SOUND_SET_LISTENER,
        "void Sound_SetListener(const float *m, const double *pos, const double *vel)", m, pos,
        vel);
}
NATIVE_DECL void GET_NAME(char *out, const char *name) {
    return NATIVE_INVOKE<Void>(HASH::SOUND_GET_NAME,
                               "void Sound_GetName(char *out, const char *name)", out, name);
}
NATIVE_DECL int HASH_NAME(char *key) {
    return NATIVE_INVOKE<int>(HASH::SOUND_HASH_NAME, "int Sound_HashName(char *key)", key);
}
NATIVE_DECL int HASH_BUCKET(char *key) {
    return NATIVE_INVOKE<int>(HASH::SOUND_HASH_BUCKET, "int Sound_HashBucket(char *key)", key);
}
NATIVE_DECL int GET_VOICE_COUNT() {
    return NATIVE_INVOKE<int>(HASH::SOUND_GET_VOICE_COUNT, "int Sound_GetVoiceCount(void)");
}
NATIVE_DECL void SET_RATE(int rate) {
    return NATIVE_INVOKE<Void>(HASH::SOUND_SET_RATE, "void Sound_SetRate(int rate)", rate);
}
NATIVE_DECL int GET_RATE() {
    return NATIVE_INVOKE<int>(HASH::SOUND_GET_RATE, "int Sound_GetRate(void)");
}
NATIVE_DECL void SET_UNUSED_BYTE(char v) {
    return NATIVE_INVOKE<Void>(HASH::SOUND_SET_UNUSED_BYTE, "void Sound_SetUnusedByte(char v)", v);
}
NATIVE_DECL uint8_t GET_LIST_EVENT() {
    return NATIVE_INVOKE<uint8_t>(HASH::SOUND_GET_LIST_EVENT, "uint8_t Sound_GetListEvent(void)");
}
NATIVE_DECL uint8_t GET_INFO_EVENT() {
    return NATIVE_INVOKE<uint8_t>(HASH::SOUND_GET_INFO_EVENT, "uint8_t Sound_GetInfoEvent(void)");
}
NATIVE_DECL uint8_t GET_FIND_EVENT() {
    return NATIVE_INVOKE<uint8_t>(HASH::SOUND_GET_FIND_EVENT, "uint8_t Sound_GetFindEvent(void)");
}
NATIVE_DECL uint8_t GET_NAME_EVENT() {
    return NATIVE_INVOKE<uint8_t>(HASH::SOUND_GET_NAME_EVENT, "uint8_t Sound_GetNameEvent(void)");
}
NATIVE_DECL uint8_t GET_SET_POS_EVENT() {
    return NATIVE_INVOKE<uint8_t>(HASH::SOUND_GET_SET_POS_EVENT,
                                  "uint8_t Sound_GetSetPosEvent(void)");
}
} // namespace AUDIO
namespace WEATHER {
NATIVE_DECL RainCell *FIND_CELL(RainObj *r, const Vec3d *p) {
    return NATIVE_INVOKE<RainCell *>(
        HASH::RAIN_OBJ_FIND_CELL, "RainCell *RainObj_FindCell(RainObj *r, const Vec3d *p)", r, p);
}
NATIVE_DECL int IS_CAMERA_OUTSIDE(RainObj *r) {
    return NATIVE_INVOKE<int>(HASH::RAIN_OBJ_IS_CAMERA_OUTSIDE,
                              "int RainObj_IsCameraOutside(RainObj *r)", r);
}
NATIVE_DECL int IS_CAMERA_CELL_VISIBLE(RainObj *r) {
    return NATIVE_INVOKE<int>(HASH::RAIN_OBJ_IS_CAMERA_CELL_VISIBLE,
                              "int RainObj_IsCameraCellVisible(RainObj *r)", r);
}
NATIVE_DECL void SET_CALLBACK(void *fn) {
    return NATIVE_INVOKE<Void>(HASH::RAIN_DYN_CUBE_OBJ_SET_CALLBACK,
                               "void RainDynCubeObj_SetCallback(void *fn)", fn);
}
NATIVE_DECL long double IS_RAINING_AT(const Vec3d *pos) {
    return NATIVE_INVOKE<long double>(HASH::RAIN_DYN_CUBE_OBJ_IS_RAINING_AT,
                                      "long double RainDynCubeObj_IsRainingAt(const Vec3d *pos)",
                                      pos);
}
NATIVE_DECL void GET_HITS(void **hits, int *counts, int *n) {
    return NATIVE_INVOKE<Void>(HASH::RAIN_DYN_CUBE_OBJ_GET_HITS,
                               "void RainDynCubeObj_GetHits(void **hits, int *counts, int *n)",
                               hits, counts, n);
}
NATIVE_DECL void ADD_DROP(RainObj *r, const Vec3f *pos, const Vec3f *vel, int unused) {
    return NATIVE_INVOKE<Void>(
        HASH::RAIN_OBJ_ADD_DROP,
        "void RainObj_AddDrop(RainObj *r, const Vec3f *pos, const Vec3f *vel, int unused)", r, pos,
        vel, unused);
}
NATIVE_DECL void SPAWN_DROP(RainObj *r, const Vec3f *pos, const Vec3f *dir, float spread,
                            float speed, int unk) {
    return NATIVE_INVOKE<Void>(HASH::RAIN_OBJ_SPAWN_DROP,
                               "void RainObj_SpawnDrop(RainObj *r, const Vec3f *pos, const Vec3f "
                               "*dir, float spread, float speed, int unk)",
                               r, pos, dir, spread, speed, unk);
}
NATIVE_DECL void PLACE_CELLS(RainObj *r) {
    return NATIVE_INVOKE<Void>(HASH::RAIN_OBJ_PLACE_CELLS, "void RainObj_PlaceCells(RainObj *r)",
                               r);
}
NATIVE_DECL void SIMULATE(RainObj *r) {
    return NATIVE_INVOKE<Void>(HASH::RAIN_OBJ_SIMULATE, "void RainObj_Simulate(RainObj *r)", r);
}
NATIVE_DECL void MOVE_DROPS(RainObj *r) {
    return NATIVE_INVOKE<Void>(HASH::RAIN_OBJ_MOVE_DROPS, "void RainObj_MoveDrops(RainObj *r)", r);
}
NATIVE_DECL void REMOVE_DROP(RainObj *r, int i) {
    return NATIVE_INVOKE<Void>(HASH::RAIN_OBJ_REMOVE_DROP,
                               "void RainObj_RemoveDrop(RainObj *r, int i)", r, i);
}
NATIVE_DECL void WRAP_CELL(RainObj *r, RainCell *c, float size) {
    return NATIVE_INVOKE<Void>(HASH::RAIN_OBJ_WRAP_CELL,
                               "void RainObj_WrapCell(RainObj *r, RainCell *c, float size)", r, c,
                               size);
}
NATIVE_DECL void CHECK_CELL(RainObj *r, RainCell *c) {
    return NATIVE_INVOKE<Void>(HASH::RAIN_OBJ_CHECK_CELL,
                               "void RainObj_CheckCell(RainObj *r, RainCell *c)", r, c);
}
NATIVE_DECL void DRAW(RainPrim *prim) {
    return NATIVE_INVOKE<Void>(HASH::RAIN_OBJ_DRAW, "void RainObj_Draw(RainPrim *prim)", prim);
}
NATIVE_DECL void QUEUE_DRAW(RainObj *r) {
    return NATIVE_INVOKE<Void>(HASH::RAIN_OBJ_QUEUE_DRAW, "void RainObj_QueueDraw(RainObj *r)", r);
}
NATIVE_DECL void UPDATE_CELLS(RainObj *r) {
    return NATIVE_INVOKE<Void>(HASH::RAIN_OBJ_UPDATE_CELLS, "void RainObj_UpdateCells(RainObj *r)",
                               r);
}
} // namespace WEATHER
namespace LEVEL {
NATIVE_DECL void SET_MODELS(void *models) {
    return NATIVE_INVOKE<Void>(HASH::LEVEL_SET_MODELS, "void Level_SetModels(void *models)",
                               models);
}
NATIVE_DECL Task *GET_STATIC() {
    return NATIVE_INVOKE<Task *>(HASH::LEVEL_GET_STATIC, "Task *Level_GetStatic(void)");
}
NATIVE_DECL Task *GET_DYNAMIC() {
    return NATIVE_INVOKE<Task *>(HASH::LEVEL_GET_DYNAMIC, "Task *Level_GetDynamic(void)");
}
NATIVE_DECL int GET_TICK() {
    return NATIVE_INVOKE<int>(HASH::LEVEL_GET_TICK, "int Level_GetTick(void)");
}
NATIVE_DECL void SET_TASK_ID(int id, Task *task) {
    return NATIVE_INVOKE<Void>(HASH::LEVEL_SET_TASK_ID, "void Level_SetTaskId(int id, Task *task)",
                               id, task);
}
NATIVE_DECL char GET_DEBUG_INFO_EVENT() {
    return NATIVE_INVOKE<char>(HASH::LEVEL_GET_DEBUG_INFO_EVENT,
                               "char Level_GetDebugInfoEvent(void)");
}
NATIVE_DECL char GET_NAME_EVENT() {
    return NATIVE_INVOKE<char>(HASH::LEVEL_GET_NAME_EVENT, "char Level_GetNameEvent(void)");
}
NATIVE_DECL int GET_ACTIVATE_EVENT() {
    return NATIVE_INVOKE<int>(HASH::LEVEL_GET_ACTIVATE_EVENT, "int Level_GetActivateEvent(void)");
}
NATIVE_DECL int GET_DEACTIVATE_EVENT() {
    return NATIVE_INVOKE<int>(HASH::LEVEL_GET_DEACTIVATE_EVENT,
                              "int Level_GetDeactivateEvent(void)");
}
NATIVE_DECL int GET_UNUSED_NEW_TASK_EVENT() {
    return NATIVE_INVOKE<int>(HASH::LEVEL_GET_UNUSED_NEW_TASK_EVENT,
                              "int Level_GetUnusedNewTaskEvent(void)");
}
NATIVE_DECL Level *GET() {
    return NATIVE_INVOKE<Level *>(HASH::LEVEL_GET, "Level *Level_Get(void)");
}
NATIVE_DECL void LEVEL_QUIT() {
    return NATIVE_INVOKE<Void>(HASH::LEVEL_QUIT, "void LevelQuit(void)");
}
NATIVE_DECL ScriptValue *NEW_TASK(ScriptValue *out, Task *parent, ScriptArgs *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::LEVEL_NEW_TASK,
        "ScriptValue *Level_NewTask(ScriptValue *out, Task *parent, ScriptArgs *args)", out, parent,
        args);
}
NATIVE_DECL Task *NEW(Task *parent) {
    return NATIVE_INVOKE<Task *>(HASH::LEVEL_NEW, "Task *Level_New(Task *parent)", parent);
}
NATIVE_DECL void LOAD(const char *path, int fps) {
    return NATIVE_INVOKE<Void>(HASH::LEVEL_LOAD, "void Level_Load(const char *path, int fps)", path,
                               fps);
}
NATIVE_DECL void CLEAR() {
    return NATIVE_INVOKE<Void>(HASH::LEVEL_CLEAR, "void Level_Clear(void)");
}
NATIVE_DECL void CLEAR_TASK_ID(int id) {
    return NATIVE_INVOKE<Void>(HASH::LEVEL_CLEAR_TASK_ID, "void Level_ClearTaskId(int id)", id);
}
} // namespace LEVEL
namespace MENU {
NATIVE_DECL ScriptValue *SET_LANGUAGE(ScriptValue *out, MenuManager *mm, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::MENU_MANAGER_SET_LANGUAGE,
        "ScriptValue *MenuManager_SetLanguage(ScriptValue *out, MenuManager *mm, void *args)", out,
        mm, args);
}
NATIVE_DECL ScriptValue *SET_ENABLED(ScriptValue *out, MenuManager *mm, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::MENU_MANAGER_SET_ENABLED,
        "ScriptValue *MenuManager_SetEnabled(ScriptValue *out, MenuManager *mm, void *args)", out,
        mm, args);
}
NATIVE_DECL void MENU_MANAGER_GET_PARAMS(void *task, void **out) {
    return NATIVE_INVOKE<Void>(HASH::MENU_MANAGER_GET_PARAMS,
                               "void MenuManager_GetParams(void *task, void **out)", task, out);
}
NATIVE_DECL void SET_FOCUS(MenuManager *mm, int i) {
    return NATIVE_INVOKE<Void>(HASH::MENU_MANAGER_SET_FOCUS,
                               "void MenuManager_SetFocus(MenuManager *mm, int i)", mm, i);
}
NATIVE_DECL uint16_t GET_TYPE() {
    return NATIVE_INVOKE<uint16_t>(HASH::MENU_MANAGER_GET_TYPE,
                                   "uint16_t MenuManager_GetType(void)");
}
NATIVE_DECL Text *FIND_PIC(MenuManager *mm, const char *name) {
    return NATIVE_INVOKE<Text *>(HASH::MENU_MANAGER_FIND_PIC,
                                 "Text *MenuManager_FindPic(MenuManager *mm, const char *name)", mm,
                                 name);
}
NATIVE_DECL Sprite *FIND_SPRITE(MenuManager *mm, const char *name) {
    return NATIVE_INVOKE<Sprite *>(
        HASH::MENU_MANAGER_FIND_SPRITE,
        "Sprite *MenuManager_FindSprite(MenuManager *mm, const char *name)", mm, name);
}
NATIVE_DECL Sprite *FIND_FONT(MenuManager *mm, const char *name) {
    return NATIVE_INVOKE<Sprite *>(
        HASH::MENU_MANAGER_FIND_FONT,
        "Sprite *MenuManager_FindFont(MenuManager *mm, const char *name)", mm, name);
}
NATIVE_DECL const char *GET_STRING(MenuManager *mm, const char *name) {
    return NATIVE_INVOKE<const char *>(
        HASH::MENU_MANAGER_GET_STRING,
        "const char *MenuManager_GetString(MenuManager *mm, const char *name)", mm, name);
}
NATIVE_DECL MenuInput *GET_INPUT(MenuManager *mm) {
    return NATIVE_INVOKE<MenuInput *>(HASH::MENU_MANAGER_GET_INPUT,
                                      "MenuInput *MenuManager_GetInput(MenuManager *mm)", mm);
}
NATIVE_DECL int GET_FADE(MenuManager *mm) {
    return NATIVE_INVOKE<int>(HASH::MENU_MANAGER_GET_FADE,
                              "int MenuManager_GetFade(MenuManager *mm)", mm);
}
NATIVE_DECL Task *GET_BACKGROUND_FX(MenuManager *mm) {
    return NATIVE_INVOKE<Task *>(HASH::MENU_MANAGER_GET_BACKGROUND_FX,
                                 "Task *MenuManager_GetBackgroundFX(MenuManager *mm)", mm);
}
NATIVE_DECL void TOGGLE_BOX_GET_PARAMS(void *task, void **out) {
    return NATIVE_INVOKE<Void>(HASH::TOGGLE_BOX_GET_PARAMS,
                               "void ToggleBox_GetParams(void *task, void **out)", task, out);
}
NATIVE_DECL void LIST_BOX_GET_PARAMS(void *task, void **out) {
    return NATIVE_INVOKE<Void>(HASH::LIST_BOX_GET_PARAMS,
                               "void ListBox_GetParams(void *task, void **out)", task, out);
}
NATIVE_DECL ScriptValue *REQUEST_SCREEN(ScriptValue *out, MenuManager *mm, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::MENU_MANAGER_REQUEST_SCREEN,
        "ScriptValue *MenuManager_RequestScreen(ScriptValue *out, MenuManager *mm, void *args)",
        out, mm, args);
}
NATIVE_DECL void APPLY_LANGUAGE(MenuManager *mm) {
    return NATIVE_INVOKE<Void>(HASH::MENU_MANAGER_APPLY_LANGUAGE,
                               "void MenuManager_ApplyLanguage(MenuManager *mm)", mm);
}
NATIVE_DECL void REFRESH(MenuManager *mm, int all) {
    return NATIVE_INVOKE<Void>(HASH::MENU_MANAGER_REFRESH,
                               "void MenuManager_Refresh(MenuManager *mm, int all)", mm, all);
}
} // namespace MENU
namespace SCRIPT {
NATIVE_DECL void SET_SYMBOL_CONTEXT(const char *name, void *context) {
    return NATIVE_INVOKE<Void>(HASH::SCRIPT_SET_SYMBOL_CONTEXT,
                               "void Script_SetSymbolContext(const char *name, void *context)",
                               name, context);
}
NATIVE_DECL void SET_WARNING_LEVEL(int level) {
    return NATIVE_INVOKE<Void>(HASH::SCRIPT_SET_WARNING_LEVEL,
                               "void Script_SetWarningLevel(int level)", level);
}
NATIVE_DECL void SET_MAX_ARGS(int count) {
    return NATIVE_INVOKE<Void>(HASH::SCRIPT_SET_MAX_ARGS, "void Script_SetMaxArgs(int count)",
                               count);
}
NATIVE_DECL void SET_LINE_SIZE(int size) {
    return NATIVE_INVOKE<Void>(HASH::SCRIPT_SET_LINE_SIZE, "void Script_SetLineSize(int size)",
                               size);
}
NATIVE_DECL void SET_BREAK_ENABLED(int on) {
    return NATIVE_INVOKE<Void>(HASH::SCRIPT_SET_BREAK_ENABLED,
                               "void Script_SetBreakEnabled(int on)", on);
}
NATIVE_DECL int GET_ARG_INT(ScriptArgs *args, int i) {
    return NATIVE_INVOKE<int>(HASH::SCRIPT_GET_ARG_INT,
                              "int Script_GetArgInt(ScriptArgs *args, int i)", args, i);
}
NATIVE_DECL long double GET_ARG_REAL(ScriptArgs *args, int i) {
    return NATIVE_INVOKE<long double>(HASH::SCRIPT_GET_ARG_REAL,
                                      "long double Script_GetArgReal(ScriptArgs *args, int i)",
                                      args, i);
}
NATIVE_DECL char *GET_ARG_STRING(ScriptArgs *args, int i, char *buf, int size) {
    return NATIVE_INVOKE<char *>(
        HASH::SCRIPT_GET_ARG_STRING,
        "char *Script_GetArgString(ScriptArgs *args, int i, char *buf, int size)", args, i, buf,
        size);
}
NATIVE_DECL int GET_ARG_LINE(ScriptArgs *args, int i) {
    return NATIVE_INVOKE<int>(HASH::SCRIPT_GET_ARG_LINE,
                              "int Script_GetArgLine(ScriptArgs *args, int i)", args, i);
}
NATIVE_DECL int HAS_ARG(ScriptArgs *args, int i) {
    return NATIVE_INVOKE<int>(HASH::SCRIPT_HAS_ARG, "int Script_HasArg(ScriptArgs *args, int i)",
                              args, i);
}
NATIVE_DECL ScriptValue *SET_WARNING_LEVEL_CMD(ScriptValue *out, void *data, ScriptArgs *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::SCRIPT_SET_WARNING_LEVEL_CMD,
        "ScriptValue *Script_SetWarningLevelCmd(ScriptValue *out, void *data, ScriptArgs *args)",
        out, data, args);
}
NATIVE_DECL const char *FIND_SYMBOL(const char *name, ScriptSymbol **out) {
    return NATIVE_INVOKE<const char *>(
        HASH::SCRIPT_FIND_SYMBOL,
        "const char *Script_FindSymbol(const char *name, ScriptSymbol **out)", name, out);
}
NATIVE_DECL SymbolTable *GET_SYMBOL_TABLE() {
    return NATIVE_INVOKE<SymbolTable *>(HASH::SCRIPT_GET_SYMBOL_TABLE,
                                        "SymbolTable *Script_GetSymbolTable(void)");
}
NATIVE_DECL int EVAL_CONST(const char *expr) {
    return NATIVE_INVOKE<int>(HASH::SCRIPT_EVAL_CONST, "int Script_EvalConst(const char *expr)",
                              expr);
}
NATIVE_DECL ScriptValue *INT_VALUE(ScriptValue *out, int *var) {
    return NATIVE_INVOKE<ScriptValue *>(HASH::SCRIPT_INT_VALUE,
                                        "ScriptValue *Script_IntValue(ScriptValue *out, int *var)",
                                        out, var);
}
NATIVE_DECL ScriptValue *BOOL8_VALUE(ScriptValue *out, char *var) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::SCRIPT_BOOL8_VALUE, "ScriptValue *Script_Bool8Value(ScriptValue *out, char *var)",
        out, var);
}
} // namespace SCRIPT
namespace TASK {
NATIVE_DECL Task *RUN_SCRIPT(Task *parent, const char *path, char insertHead) {
    return NATIVE_INVOKE<Task *>(
        HASH::TASK_RUN_SCRIPT,
        "Task *Task_RunScript(Task *parent, const char *path, char insertHead)", parent, path,
        insertHead);
}
NATIVE_DECL Task *FIND_BY_ID(int id) {
    return NATIVE_INVOKE<Task *>(HASH::TASK_FIND_BY_ID, "Task *Task_FindById(int id)", id);
}
NATIVE_DECL void DELETE_TASK(Task *task) {
    NATIVE_INVOKE<void>(HASH::TASK_DELETE, "void Task_Delete(Task *task)", task);
}
NATIVE_DECL uint16_t GET_MASK(uint16_t type) {
    return NATIVE_INVOKE<uint16_t>(HASH::TASK_TYPE_GET_MASK,
                                   "uint16_t TaskType_GetMask(uint16_t type)", type);
}
NATIVE_DECL uint16_t GET_ROOT_TYPE() {
    return NATIVE_INVOKE<uint16_t>(HASH::TASK_GET_ROOT_TYPE, "uint16_t Task_GetRootType(void)");
}
NATIVE_DECL int GET_COUNT() {
    return NATIVE_INVOKE<int>(HASH::TASK_GET_COUNT, "int Task_GetCount(void)");
}
NATIVE_DECL uint16_t FIND(const char *name) {
    return NATIVE_INVOKE<uint16_t>(HASH::TASK_TYPE_FIND, "uint16_t TaskType_Find(const char *name)",
                                   name);
}
NATIVE_DECL const char *GET_NAME(uint16_t type) {
    return NATIVE_INVOKE<const char *>(HASH::TASK_TYPE_GET_NAME,
                                       "const char *TaskType_GetName(uint16_t type)", type);
}
NATIVE_DECL Task *FIND_CHILD(Task *task, uint16_t type) {
    return NATIVE_INVOKE<Task *>(HASH::TASK_FIND_CHILD,
                                 "Task *Task_FindChild(Task *task, uint16_t type)", task, type);
}
NATIVE_DECL Task *FIND_LAST_CHILD(Task *task, uint16_t type) {
    return NATIVE_INVOKE<Task *>(HASH::TASK_FIND_LAST_CHILD,
                                 "Task *Task_FindLastChild(Task *task, uint16_t type)", task, type);
}
NATIVE_DECL Task *FIND_NEXT(Task *task, uint16_t type) {
    return NATIVE_INVOKE<Task *>(HASH::TASK_FIND_NEXT,
                                 "Task *Task_FindNext(Task *task, uint16_t type)", task, type);
}
NATIVE_DECL Task *FIND_PREV(Task *task, uint16_t type) {
    return NATIVE_INVOKE<Task *>(HASH::TASK_FIND_PREV,
                                 "Task *Task_FindPrev(Task *task, uint16_t type)", task, type);
}
NATIVE_DECL Task *FIND_ANCESTOR(Task *task, uint16_t type) {
    return NATIVE_INVOKE<Task *>(HASH::TASK_FIND_ANCESTOR,
                                 "Task *Task_FindAncestor(Task *task, uint16_t type)", task, type);
}
NATIVE_DECL char IS_A(uint16_t type, uint16_t base) {
    return NATIVE_INVOKE<char>(HASH::TASK_TYPE_IS_A,
                               "char TaskType_IsA(uint16_t type, uint16_t base)", type, base);
}
NATIVE_DECL void SET_UNUSED_ROOT(int value) {
    return NATIVE_INVOKE<Void>(HASH::TASK_SET_UNUSED_ROOT, "void Task_SetUnusedRoot(int value)",
                               value);
}
NATIVE_DECL void SET_GLOBAL_FLAGS(uint32_t flags) {
    return NATIVE_INVOKE<Void>(HASH::TASK_SET_GLOBAL_FLAGS,
                               "void Task_SetGlobalFlags(uint32_t flags)", flags);
}
NATIVE_DECL void SET_FLAGS(uint16_t type, uint32_t flags) {
    return NATIVE_INVOKE<Void>(HASH::TASK_TYPE_SET_FLAGS,
                               "void TaskType_SetFlags(uint16_t type, uint32_t flags)", type,
                               flags);
}
NATIVE_DECL int IS_USED(int type) {
    return NATIVE_INVOKE<int>(HASH::TASK_TYPE_IS_USED, "int TaskType_IsUsed(int type)", type);
}
NATIVE_DECL void *FIND_WEAPON(void *task, int id) {
    return NATIVE_INVOKE<void *>(HASH::TASK_FIND_WEAPON,
                                 "void *Task_FindWeapon(void *task, int id)", task, id);
}
} // namespace TASK
namespace CONFIG {
NATIVE_DECL ScriptValue *IS_GERMANY(ScriptValue *out) {
    return NATIVE_INVOKE<ScriptValue *>(HASH::CONFIG_IS_GERMANY,
                                        "ScriptValue *Config_IsGermany(ScriptValue *out)", out);
}
NATIVE_DECL ScriptValue *GET_CONTENT_CONTROL_PASSWORD(ScriptValue *out) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::CONFIG_GET_CONTENT_CONTROL_PASSWORD,
        "ScriptValue *Config_GetContentControlPassword(ScriptValue *out)", out);
}
NATIVE_DECL ScriptValue *IS_CONTENT_CONTROL_PASSWORD_ENABLED(ScriptValue *out) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::CONFIG_IS_CONTENT_CONTROL_PASSWORD_ENABLED,
        "ScriptValue *Config_IsContentControlPasswordEnabled(ScriptValue *out)", out);
}
NATIVE_DECL GraphicOptions *GET_GRAPHIC_OPTIONS() {
    return NATIVE_INVOKE<GraphicOptions *>(HASH::CONFIG_GET_GRAPHIC_OPTIONS,
                                           "GraphicOptions *Config_GetGraphicOptions(void)");
}
NATIVE_DECL SoundOptions *GET_SOUND_OPTIONS() {
    return NATIVE_INVOKE<SoundOptions *>(HASH::CONFIG_GET_SOUND_OPTIONS,
                                         "SoundOptions *Config_GetSoundOptions(void)");
}
NATIVE_DECL ScriptValue *GET_ACTIVE_PLAYER_PROFILE_INDEX(ScriptValue *out) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::CONFIG_GET_ACTIVE_PLAYER_PROFILE_INDEX,
        "ScriptValue *Config_GetActivePlayerProfileIndex(ScriptValue *out)", out);
}
NATIVE_DECL ScriptValue *SET_ACTIVE_PLAYER_PROFILE_INDEX(ScriptValue *out, void *data, void *args) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::CONFIG_SET_ACTIVE_PLAYER_PROFILE_INDEX,
        "ScriptValue *Config_SetActivePlayerProfileIndex(ScriptValue *out, void *data, void *args)",
        out, data, args);
}
NATIVE_DECL ScriptValue *GET_NUMBER_OF_PLAYER_PROFILES(ScriptValue *out) {
    return NATIVE_INVOKE<ScriptValue *>(
        HASH::CONFIG_GET_NUMBER_OF_PLAYER_PROFILES,
        "ScriptValue *Config_GetNumberOfPlayerProfiles(ScriptValue *out)", out);
}
NATIVE_DECL InputRemap *GET_DEFAULT_KEYS() {
    return NATIVE_INVOKE<InputRemap *>(HASH::CONFIG_GET_DEFAULT_KEYS,
                                       "InputRemap *Config_GetDefaultKeys(void)");
}
NATIVE_DECL void GET_KEY(int i, int *value, const char **name) {
    return NATIVE_INVOKE<Void>(HASH::CONFIG_GET_KEY,
                               "void Config_GetKey(int i, int *value, const char **name)", i, value,
                               name);
}
NATIVE_DECL int GET_KEY_COUNT() {
    return NATIVE_INVOKE<int>(HASH::CONFIG_GET_KEY_COUNT, "int Config_GetKeyCount(void)");
}
NATIVE_DECL void GET_ACTION(int i, int *value, const char **name) {
    return NATIVE_INVOKE<Void>(HASH::CONFIG_GET_ACTION,
                               "void Config_GetAction(int i, int *value, const char **name)", i,
                               value, name);
}
NATIVE_DECL int GET_ACTION_COUNT() {
    return NATIVE_INVOKE<int>(HASH::CONFIG_GET_ACTION_COUNT, "int Config_GetActionCount(void)");
}
NATIVE_DECL int GET_LANGUAGE_COUNT() {
    return NATIVE_INVOKE<int>(HASH::CONFIG_GET_LANGUAGE_COUNT, "int Config_GetLanguageCount(void)");
}
NATIVE_DECL const char *GET_LANGUAGE_NAME(int language) {
    return NATIVE_INVOKE<const char *>(HASH::CONFIG_GET_LANGUAGE_NAME,
                                       "const char *Config_GetLanguageName(int language)",
                                       language);
}
NATIVE_DECL const char *GET_DIFFICULTY_NAME(int difficulty) {
    return NATIVE_INVOKE<const char *>(HASH::CONFIG_GET_DIFFICULTY_NAME,
                                       "const char *Config_GetDifficultyName(int difficulty)",
                                       difficulty);
}
} // namespace CONFIG
namespace OTHER {
NATIVE_DECL void TASK_EVENT_SET(uint16_t type, uint8_t index) {
    return NATIVE_INVOKE<Void>(HASH::TASK_EVENT_SET,
                               "void TaskEvent_Set(uint16_t type, uint8_t index)", type, index);
}
NATIVE_DECL void TASK_EVENT_SET2(uint16_t type, int index) {
    return NATIVE_INVOKE<Void>(HASH::TASK_EVENT_SET2,
                               "void TaskEvent_Set2(uint16_t type, int index)", type, index);
}
NATIVE_DECL uint16_t FLOW_GET_TYPE() {
    return NATIVE_INVOKE<uint16_t>(HASH::FLOW_GET_TYPE, "uint16_t Flow_GetType(void)");
}
NATIVE_DECL int FLOW_GET_FPS() {
    return NATIVE_INVOKE<int>(HASH::FLOW_GET_FPS, "int Flow_GetFps(void)");
}
NATIVE_DECL Flow *FLOW_GET() {
    return NATIVE_INVOKE<Flow *>(HASH::FLOW_GET, "Flow *Flow_Get(void)");
}
NATIVE_DECL void *FLOW_GET_SCREEN() {
    return NATIVE_INVOKE<void *>(HASH::FLOW_GET_SCREEN, "void *Flow_GetScreen(void)");
}
NATIVE_DECL void FLOW_SET_REQUEST(int event) {
    return NATIVE_INVOKE<Void>(HASH::FLOW_SET_REQUEST, "void Flow_SetRequest(int event)", event);
}
NATIVE_DECL int FLOW_GET_FRAMES() {
    return NATIVE_INVOKE<int>(HASH::FLOW_GET_FRAMES, "int Flow_GetFrames(void)");
}
NATIVE_DECL int FLOW_GET_RENDERS() {
    return NATIVE_INVOKE<int>(HASH::FLOW_GET_RENDERS, "int Flow_GetRenders(void)");
}
NATIVE_DECL InputRemap *PROFILE_GET_KEYS() {
    return NATIVE_INVOKE<InputRemap *>(HASH::PROFILE_GET_KEYS, "InputRemap *Profile_GetKeys(void)");
}
NATIVE_DECL int PROFILE_GET_KEY_COUNT() {
    return NATIVE_INVOKE<int>(HASH::PROFILE_GET_KEY_COUNT, "int Profile_GetKeyCount(void)");
}
NATIVE_DECL void PROFILE_SET_KEY_COUNT(int count) {
    return NATIVE_INVOKE<Void>(HASH::PROFILE_SET_KEY_COUNT, "void Profile_SetKeyCount(int count)",
                               count);
}
NATIVE_DECL PlayerProfile *PROFILE_GET_ACTIVE() {
    return NATIVE_INVOKE<PlayerProfile *>(HASH::PROFILE_GET_ACTIVE,
                                          "PlayerProfile *Profile_GetActive(void)");
}
NATIVE_DECL float PROFILE_GET_SFX_VOLUME() {
    return NATIVE_INVOKE<float>(HASH::PROFILE_GET_SFX_VOLUME, "float Profile_GetSfxVolume(void)");
}
NATIVE_DECL float PROFILE_GET_MUSIC_VOLUME() {
    return NATIVE_INVOKE<float>(HASH::PROFILE_GET_MUSIC_VOLUME,
                                "float Profile_GetMusicVolume(void)");
}
NATIVE_DECL int PROFILE_GET_LANGUAGE() {
    return NATIVE_INVOKE<int>(HASH::PROFILE_GET_LANGUAGE, "int Profile_GetLanguage(void)");
}
} // namespace OTHER
std::size_t NativeBindingCount() noexcept {
    return 318;
}
} // namespace IGI
#undef NATIVE_INVOKE
