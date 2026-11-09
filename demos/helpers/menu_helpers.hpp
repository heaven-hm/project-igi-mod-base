#pragma once

#include "config/menu_config.hpp"
#include "natives/mvp_natives.hpp"
#include "runtime/runtime.hpp"

#include <array>
#include <cstddef>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace igi {
struct AiScriptContext {
    IGI::ScriptValue *out;
    void *data;
    void *args;
};

struct LiveTarget {
    int taskId = -1;
    std::string name;
    // Optional world position, filled for landmark/world-object entries.
    std::array<double, 3> position{};
    bool hasPosition = false;
};

class EntityHelpers {
  public:
    explicit EntityHelpers(Runtime &runtime) : runtime_(runtime) {}
    IGI::Human *firstHuman() const;
    IGI::Human *nextHuman(IGI::Human *current) const;
    IGI::Human *firstNpc() const;
    IGI::Human *npcAt(std::size_t slot) const;
    IGI::Car *firstCar() const;
    IGI::Car *carAt(std::size_t slot) const;
    IGI::Car *nextCar(IGI::Car *current) const;
    IGI::Task *firstVehicle() const;
    IGI::Task *nextVehicle(IGI::Task *current) const;
    std::vector<LiveTarget> npcTargets() const;
    std::vector<LiveTarget> carTargets() const;
    IGI::Heli *firstHeli() const;
    std::vector<LiveTarget> heliTargets() const;
    // Named world objects (buildings, doors, props and mission landmarks) with
    // their level position, used for the predefined teleport dropdown.
    std::vector<LiveTarget> landmarkTargets() const;
    bool moveHuman(IGI::Human *human, const std::array<double, 3> &position) const;
    bool setHumanYaw(IGI::Human *human, float yaw) const;
    bool snapHumanToGround(IGI::Human *human) const;
    bool leaveHumanGround(IGI::Human *human) const;
    bool setHumanCollision(IGI::Human *human, bool enabled) const;

  private:
    Runtime &runtime_;
};

class PlayerHelpers {
  public:
    explicit PlayerHelpers(Runtime &runtime) : runtime_(runtime) {}
    std::optional<float> moveScale() const;
    std::optional<int> state() const;
    std::optional<int> team() const;
    std::optional<int> index() const;
    std::optional<bool> isDead() const;
    std::optional<bool> isCrouching() const;
    std::optional<bool> isActive() const;
    std::optional<bool> isPlayer() const;
    bool setInputEnabled(bool enabled) const;
    bool setLookEnabled(bool enabled) const;
    bool startAction(int action) const;
    bool checkDeath() const;
    bool sendPosition() const;
    bool reportStatus() const;
    bool resetDamage() const;
    bool snapToGround() const;
    bool setCollisionEnabled(bool enabled) const;
    bool restoreVisualFeedback() const;
    bool setFlagD44() const;
    bool clearFlagD44() const;
    bool setFlagD68() const;
    bool clearFlagD68() const;

  private:
    Runtime &runtime_;
};
class WeaponHelpers {
  public:
    explicit WeaponHelpers(Runtime &runtime) : runtime_(runtime) {}
    bool pickup(std::string_view weaponId, int ammo) const;
    bool enableUnlimitedAmmo() const;
    bool setAnimation(int animation) const;
    bool requestAnimation(int animation) const;
    bool cycle(int force) const;
    bool selectCategory() const;
    bool reportFlashReset() const;
    bool reportEvents() const;
    bool reportWeaponInfo() const;
    bool stopWeaponAnimation() const;
    // Adds rounds to the weapon the player is holding right now.
    bool refillCurrentWeapon(int ammo) const;
    // Fully drops the held weapon: retail drop, cleared carried slot and no
    // leftover draw animation, so the weapon cannot be cycled back to.
    bool dropCurrentWeapon() const;
    std::optional<int> hasAmmo(void *weaponTask) const;
    IGI::WeaponType *type(void *weaponTask) const;
    std::optional<unsigned> fireEvent() const;
    std::optional<unsigned> typeEvent() const;
    std::optional<bool> triggerEvent() const;
    std::optional<bool> stateEvent() const;
    std::optional<bool> moveEvent() const;

  private:
    Runtime &runtime_;
};
class CameraHelpers {
  public:
    explicit CameraHelpers(Runtime &runtime) : runtime_(runtime) {}
    bool setViewMode(int mode) const;
    bool reportInfo() const;
    std::optional<int> mode() const;
    std::optional<bool> isThirdPersonActive() const;
    bool setWeaponTarget(IGI::HumanView *view, float pitch, float yaw, float pitchSpeed,
                         float yawSpeed) const;
    bool setWeaponAngles(IGI::HumanView *view, float pitch, float yaw) const;
    bool setCamera(IGI::HumanView *view, const double *position, const IGI::Matrix *rotation) const;
    bool setTarget(IGI::HumanView *view, const int *target, bool enabled) const;
    bool setDip(IGI::HumanView *view, float dip) const;
    bool setZoomed(IGI::HumanView *view) const;
    bool eye(IGI::HumanView *view, double *out) const;
    bool forward(IGI::HumanView *view, float *out) const;
    bool matrix(IGI::HumanView *view, IGI::Matrix *out) const;

  private:
    Runtime &runtime_;
};
class WorldHelpers {
  public:
    explicit WorldHelpers(Runtime &runtime) : runtime_(runtime) {}
    bool changeLevel(int levelId) const;
    bool restartMission() const;
    bool setWeather(std::string_view weather) const;
    // World clock and mission timer. Both live in the level's LevelFlow task
    // (nTick +0x68, startHour +0x6C, timeLimit +0x74).
    bool setWorldHour(float hour) const;
    bool setMissionElapsedSeconds(int seconds) const;
    bool setMissionTimeLimit(float seconds) const;
    std::optional<float> worldHour() const;
    std::optional<int> missionElapsedSeconds() const;
    bool setPaused(bool paused) const;
    std::optional<bool> isPaused() const;
    bool refreshDisplay() const;
    bool setFrameCap(int frames) const;
    bool reportLevelTick() const;
    bool reportFlow() const;
    bool reportEvents() const;
    std::optional<unsigned> event(int eventIndex) const;
    std::optional<int> flowFps() const;
    std::optional<int> flowFrames() const;
    std::optional<int> flowRenders() const;
    std::optional<int> taskCount() const;
    bool logFlowDiagnostics() const;

  private:
    Runtime &runtime_;
};
class VehicleHelpers {
  public:
    explicit VehicleHelpers(Runtime &runtime) : runtime_(runtime) {}
    bool carControlInfo(IGI::Car *car, int *out) const;
    bool carFrame(IGI::Car *car, void **out) const;
    bool carLight(IGI::Car *car, void **out) const;
    IGI::Task *carMountedGun(IGI::Car *car) const;
    bool carGunnerPose(IGI::Car *car, IGI::Vec3d *position, IGI::Matrix *matrix) const;
    std::optional<long double> carTrackFactor(IGI::Car *car, int joint) const;
    bool explodeCar(IGI::Car *car) const;
    bool resetCar(IGI::Car *car) const;
    bool resetHeli(IGI::Heli *heli) const;
    bool reportCarInfo(IGI::Car *car) const;
    bool reportHeliInfo(IGI::Heli *heli) const;
    bool resetVehicleControls(IGI::Task *vehicle) const;
    bool explodeAllCars() const;
    bool resetAllCars() const;
    // Car_Control takes a target speed in retail speed units (the AI cruises at
    // 5+); it is not a 0..1 throttle, and a negative value drives in reverse.
    bool driveCar(IGI::Car *car, float targetSpeed, bool brake = false) const;
    bool resetControls(void *vehicle) const;
    IGI::Task *heliDoor(IGI::Heli *heli) const;
    bool heliControlInfo(IGI::Heli *heli, int *out) const;
    bool heliStateSize(IGI::Heli *heli, int *out) const;
    bool heliConfigSize(IGI::Task *task, int *out) const;
    bool heliSoundInfo(IGI::Heli *heli, void **out) const;

  private:
    Runtime &runtime_;
};
class NpcHelpers {
  public:
    explicit NpcHelpers(Runtime &runtime) : runtime_(runtime) {}
    // Gives the NPC a real weapon task and puts it in the retail slot 0.
    // Soldier_EnsureWeapon alone does nothing unless the soldier already owns a
    // weapon task, so the weapon is created first through WeaponGunPickup.
    bool ensureWeapon(IGI::Human *npc, std::string_view weaponId, int ammo) const;
    bool resetDamage(IGI::Human *npc) const;
    bool snapToGround(IGI::Human *npc) const;
    bool setCollisionEnabled(IGI::Human *npc, bool enabled) const;
    bool executeSoldier(IGI::Human *npc) const;
    bool reportStatus(IGI::Human *npc) const;
    bool standDownSoldier(IGI::Human *npc) const;
    bool reportSoldierParams(IGI::Human *npc) const;
    // Walk/run the NPC towards the player through the retail HumanAI move calls.
    // A move needs a graph destination, so the nearest AI graph node to the
    // player is resolved first; the position refines the final approach.
    bool moveTowardsPlayer(IGI::Human *npc, bool run) const;
    bool setMovementMode(IGI::Human *npc, bool run) const;
    bool clearMovement(IGI::Human *npc) const;

  private:
    Runtime &runtime_;
};
class AiHelpers {
  public:
    explicit AiHelpers(Runtime &runtime) : runtime_(runtime) {}
    bool removeAlarmActions(const AiScriptContext &c) const;
    bool setViewLength(const AiScriptContext &c) const;
    bool setAlarmViewLength(const AiScriptContext &c) const;
    bool setViewAlpha(const AiScriptContext &c) const;
    bool setViewGamma(const AiScriptContext &c) const;
    bool setSecondaryViewLength(const AiScriptContext &c) const;
    bool setSecondaryAlarmViewLength(const AiScriptContext &c) const;
    bool setSecondaryViewAlpha(const AiScriptContext &c) const;
    bool setSecondaryViewGamma(const AiScriptContext &c) const;
    bool setEventPriority(const AiScriptContext &c) const;
    bool setInvulnerability(const AiScriptContext &c) const;
    bool setInstantDeath(const AiScriptContext &c) const;
    bool setDeathAnimation(const AiScriptContext &c) const;
    bool setAlarmTriggerId(const AiScriptContext &c) const;
    bool setAlarmControlId(const AiScriptContext &c) const;
    bool setAlarmAccess(const AiScriptContext &c) const;
    bool setGunnerId(const AiScriptContext &c) const;
    bool setAnimationInterval(const AiScriptContext &c) const;
    bool currentEventType(const AiScriptContext &c) const;
    bool randomValue(const AiScriptContext &c) const;

  private:
    Runtime &runtime_;
};
class AudioHelpers {
  public:
    explicit AudioHelpers(Runtime &runtime) : runtime_(runtime) {}
    bool setMusicVolume(float volume) const;
    bool setSfxVolume(float volume) const;
    bool setRate(int rate) const;
    bool reportInfo() const;

  private:
    Runtime &runtime_;
};

void executeMenuNativeAction(const MenuConfig &config, Runtime &runtime, std::size_t item,
                             std::string_view value);
bool refreshLiveTargetOptions(MenuConfig &config, Runtime &runtime);
// The live task currently selected in the menu, re-validated at call time. The
// demo adapter uses this for actions that need the menu's weapon table too.
IGI::Human *selectedNpc(Runtime &runtime);
IGI::Car *selectedCar(Runtime &runtime);
IGI::Heli *selectedHeli(Runtime &runtime);

bool setWeather(Runtime &runtime, std::string_view weather);
bool setFps(Runtime &runtime, std::string_view value);
bool enableUnlimitedAmmo(Runtime &runtime);
bool pickupPlayerWeapon(Runtime &runtime, std::string_view weaponId, int ammo);
bool cyclePlayerWeapon(Runtime &runtime, int force);
bool requestPlayerWeaponAnimation(Runtime &runtime, int animationId);
bool restorePlayerVisualFeedback(Runtime &runtime);
bool setPlayerInputEnabled(Runtime &runtime, bool enabled);
bool setPlayerLookEnabled(Runtime &runtime, bool enabled);
bool setMusicVolume(Runtime &runtime, float volume);
bool setSfxVolume(Runtime &runtime, float volume);
bool logFlowDiagnostics(Runtime &runtime);
bool logPlayerState(Runtime &runtime);
bool setPlayerThirdPersonView(Runtime &runtime, bool active);
bool setThirdPersonCrosshairEnabled(Runtime &runtime, bool enabled);
// True when the mod crosshair should be drawn: the toggle is on, the camera is
// in third person and the equipped weapon uses the crosshair sight type.
bool thirdPersonCrosshairActive(Runtime &runtime);
// Screen position of the player's aim ray for the current third-person camera.
// Returns false when the rendered camera does not belong to this player.
bool thirdPersonCrosshairScreenPosition(Runtime &runtime, int width, int height, float &x,
                                        float &y);
// Free camera built on the retail camera-override path (HumanView_SetCamera):
// the view follows a pose the mod owns, so WASD flies the camera around without
// moving the player. Enabling it locks human input; disabling restores it.
bool setFreecamEnabled(Runtime &runtime, bool enabled);
bool freecamEnabled();
// Per-frame free camera update. Call from a displayed-frame callback.
bool updateFreecam(Runtime &runtime);
// Parses a "Name [#taskId]" menu choice into its task id (-1 when not a target).
int parseTargetId(std::string_view value);
// Moves the live player next to the selected NPC/car/heli target.
bool teleportPlayerToTarget(Runtime &runtime, int targetTaskId);
// Current player body position in retail world units. Used to prefill the
// teleport X/Y/Z inputs so the menu never starts at an unset (0,0,0).
bool readPlayerPosition(Runtime &runtime, std::array<double, 3> &position);
// Moves the live player to a typed world position.
bool teleportPlayerToPosition(Runtime &runtime, const std::array<double, 3> &position);
// Retail-sprite draw for the QVM menu host (no Dear ImGui there).
bool renderThirdPersonCrosshair(Runtime &runtime);
bool changeLevel(Runtime &runtime, int levelId);
} // namespace igi
