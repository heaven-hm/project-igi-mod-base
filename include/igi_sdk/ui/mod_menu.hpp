#pragma once

#include "igi_sdk/config/menu_config.hpp"
#include "igi_sdk/runtime/runtime.hpp"

#include <cstddef>
#include <string_view>

bool configureModMenu(igi::MenuConfig& config);
void runModMenuAction(const igi::MenuConfig& config, igi::Runtime& runtime, std::size_t item, std::string_view value);
