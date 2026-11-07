#pragma once

#include "igi_sdk/config/menu_config.hpp"
#include "igi_sdk/runtime/runtime.hpp"

#include <cstddef>
#include <string_view>

namespace igi {
bool pauseForMenu(Runtime& runtime);
bool resumeFromMenu(Runtime& runtime);
bool lockHumanInput();
bool unlockHumanInput();
void executeMenuNativeAction(const MenuConfig& config, Runtime& runtime, std::size_t item,
                             std::string_view value);
bool executeMenuCommand(std::string_view commandScript);
bool changeLevel(int levelId);
bool setWeather(std::string_view weather);
bool setFps(std::string_view value);
}
