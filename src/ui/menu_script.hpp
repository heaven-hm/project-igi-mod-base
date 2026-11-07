#pragma once

#include <iosfwd>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace igi {
struct MenuConfig;
// Generate a standalone IGI QSC menu resource. Callback expressions must be
// registered with IGI's script runtime before the resource is loaded.
bool writeIgiMenuScript(std::ostream &output, const MenuConfig &config);
bool writeIgiMenuScreenScript(std::ostream &output, const MenuConfig &config);
bool compileIgiMenuScript(const MenuConfig &config, std::vector<std::uint8_t> &qvm,
                          std::string &error);
bool compileIgiMenuScreenScript(const MenuConfig &config, std::vector<std::uint8_t> &qvm,
                                std::string &error);
bool compileIgiMenuCommandScript(std::string_view command, std::vector<std::uint8_t> &qvm,
                                 std::string &error);
} // namespace igi
