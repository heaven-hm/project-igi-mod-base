#pragma once

#include "config/menu_config.hpp"
#include "runtime/runtime.hpp"

#include <cstddef>
#include <string_view>

bool configureModMenu(igi::MenuConfig &config);
void runModMenu(const igi::MenuConfig &config, igi::Runtime &runtime, std::size_t item,
                std::string_view value);
