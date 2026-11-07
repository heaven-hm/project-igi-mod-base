#pragma once

#include "igi_sdk/config/menu_config.hpp"
#include "igi_sdk/runtime/runtime.hpp"

#include <string_view>

bool configureBasicMenu(igi::MenuConfig& config);
void basicMenuAction(const igi::MenuConfig& config, igi::Runtime& runtime, std::size_t item, std::string_view value);

