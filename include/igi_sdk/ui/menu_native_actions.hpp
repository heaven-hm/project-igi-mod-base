#pragma once

#include "igi_sdk/config/menu_config.hpp"
#include "igi_sdk/runtime/runtime.hpp"

#include <cstddef>
#include <string_view>

namespace igi {
bool pauseForMenu(Runtime& runtime);
bool resumeFromMenu(Runtime& runtime);
void executeMenuNativeAction(const MenuConfig& config, Runtime& runtime, std::size_t item,
                             std::string_view value);
}
