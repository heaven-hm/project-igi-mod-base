#pragma once
#include "config/menu_config.hpp"
#include "runtime/runtime.hpp"
#include <string_view>

bool configureSimpleMenu(igi::MenuConfig &config);
void simpleMenuAction(const igi::MenuConfig &config, igi::Runtime &runtime, std::size_t item,
                      std::string_view value);
