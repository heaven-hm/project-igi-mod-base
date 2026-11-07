#pragma once

#include "config/menu_config.hpp"
#include "runtime/runtime.hpp"

#include <string_view>

bool configureBasicMenu(igi::MenuConfig &config);
void basicMenuAction(const igi::MenuConfig &config, igi::Runtime &runtime, std::size_t item,
                     std::string_view value);
