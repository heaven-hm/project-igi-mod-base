#pragma once

#include "config/menu_config.hpp"
#include "runtime/runtime.hpp"

#include <cstddef>
#include <string_view>

namespace igi {
bool requireLiveGameThread(Runtime &runtime);
void executeMenuNativeAction(const MenuConfig &config, Runtime &runtime, std::size_t item,
                             std::string_view value);
bool executeMenuCommand(std::string_view commandScript);
bool changeLevel(Runtime &runtime, int levelId);
bool refreshLiveMenu(Runtime &runtime, std::string_view effect);
bool setWeather(Runtime &runtime, std::string_view weather);
bool setFps(Runtime &runtime, std::string_view value);
void setThirdPersonView(bool active);
bool isThirdPersonActive();
void renderThirdPersonCrosshair();
} // namespace igi
