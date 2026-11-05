#pragma once

#include <cstdint>

namespace igi::igi1 {
inline constexpr std::uint32_t imageBase = 0x00400000;
inline constexpr std::uint32_t gameRunHandler = 0x00416260;
// Per-frame text draw: `mov 0x4(%esp),%eax; push %eax; call Text_Render`.
inline constexpr std::uint32_t textDraw = 0x004B6E60;
inline constexpr std::uint32_t flowRunHandler = 0x00402260;
inline constexpr std::uint32_t gameUpdate = 0x00401050;
inline constexpr std::uint32_t expectedImageSize = 0x00B00000;
inline constexpr char executableSha256[] = "dea34892d417715873ea61d7b2ba5556655f1ad65385104348146f9679915a01";
}
