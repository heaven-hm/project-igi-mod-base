#pragma once

#include "config/menu_config.hpp"
#include "natives/mvp_natives.hpp"
#include "runtime/runtime.hpp"

#include <cstddef>
#include <optional>
#include <string_view>

namespace igi {
struct AiScriptContext {
    IGI::ScriptValue *out;
    void *data;
    void *args;
};

class EntityHelpers {
  public:
    explicit EntityHelpers(Runtime &runtime) : runtime_(runtime) {}
    IGI::Human *firstHuman() const;
    IGI::Human *nextHuman(IGI::Human *current) const;
    IGI::Human *firstNpc() const;
    IGI::Car *firstCar() const;
    IGI::Car *nextCar(IGI::Car *current) const;

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
    bool resetDamage() const;
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
    bool setPaused(bool paused) const;
    std::optional<bool> isPaused() const;
    bool refreshDisplay() const;
    bool setFrameCap(int frames) const;
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
    bool ensureWeapon(IGI::Human *npc) const;
    bool resetDamage(IGI::Human *npc) const;

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

  private:
    Runtime &runtime_;
};

void executeMenuNativeAction(const MenuConfig &config, Runtime &runtime, std::size_t item,
                             std::string_view value);
} // namespace igi
