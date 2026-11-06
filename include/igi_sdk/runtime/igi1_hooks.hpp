#pragma once

#include <cstdint>

namespace igi::igi1 {
enum class TickHook { DisplayAddFrameCallback, ScreenDraw, DisplayEndFrame, TextDraw };

inline constexpr std::uint32_t imageBase = 0x00400000;
inline constexpr std::uint32_t gameRunHandler = 0x00416260;
// The top-level IGI game loop.  This is the verified, stable point used by
// project-igi-natives to drain game-thread work; MinHook owns instruction
// relocation and trampoline creation for this entry point.
inline constexpr std::uint32_t gameMainLoop = 0x0048F530;
// Per-frame display callback registration: pushes a DisplayCallback onto
// gDisplayFrameCallbacks, which Display_RunCallbacks runs once per displayed
// frame (at src/win/display.c:357, before Display_EndFrame at 364).
inline constexpr std::uint32_t displayAddFrameCallback = 0x004916C0;
inline constexpr std::uint32_t displayNextFrameCallbacks = 0x00491D0F;

// Screen-level frame: Display_NextFrame, child draw events, then Display_EndFrame.
inline constexpr std::uint32_t screenDraw = 0x004E7400;
// Per-frame end of display: `call Scene_DrawAll; mov gDisplayEndFrameCallbacks; ...`
inline constexpr std::uint32_t displayEndFrame = 0x00491D20;
// Per-frame text draw: `mov 0x4(%esp),%eax; push %eax; call Text_Render`.
inline constexpr std::uint32_t textDraw = 0x004B6E60;
inline constexpr std::uint32_t flowRunHandler = 0x00402260;
inline constexpr std::uint32_t gameUpdate = 0x00401050;
inline constexpr std::uint32_t expectedImageSize = 0x00B00000;
inline constexpr char executableSha256[] = "dea34892d417715873ea61d7b2ba5556655f1ad65385104348146f9679915a01";
}
