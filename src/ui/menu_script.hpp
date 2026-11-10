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
// Repairs values that live target refresh can produce at runtime (overlong or
// embedded-NUL labels, more choices than the retail list accepts, an
// out-of-range default selection, inverted input ranges, or bad geometry).
// Returns true when anything changed and appends a short description to
// `reason`. The sanitized config is what gets compiled and rendered.
bool sanitizeMenuConfigForCompile(MenuConfig &config, std::string *reason = nullptr);
} // namespace igi
