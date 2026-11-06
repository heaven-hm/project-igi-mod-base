#pragma once

#include <iosfwd>
#include <cstdint>
#include <string>
#include <vector>

namespace igi {
struct MenuConfig;
// Generate a standalone IGI QSC menu resource. Callback expressions must be
// registered with IGI's script runtime before the resource is loaded.
bool writeIgi1MenuScript(std::ostream& output, const MenuConfig& config);
bool writeIgi1MenuScreenScript(std::ostream& output, const MenuConfig& config);
bool compileIgi1MenuScript(const MenuConfig& config, std::vector<std::uint8_t>& qvm, std::string& error);
bool compileIgi1MenuScreenScript(const MenuConfig& config, std::vector<std::uint8_t>& qvm, std::string& error);
}
