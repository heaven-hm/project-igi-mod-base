#pragma once

// Hand-maintained IGI 1 bindings for natives recovered from IGI-RE after the
// generated binding set was frozen. Each entry is verified against the IGI-RE
// declaration catalogue (address + exact signature) before the game thread may
// call it, exactly like the generated bindings in mvp_natives.hpp.
//
// Keep this file next to mvp_natives.hpp rather than editing the generated files.

#include "natives/mvp_natives.hpp"
#include "natives/native_api.hpp"
#include "natives/native_catalogue.hpp"

#include <cstdint>

namespace IGI {
namespace HASH {
// Weapon_Drop: detaches a weapon task and returns it to the level (0x004F2390).
inline constexpr std::uint32_t WEAPON_DROP_HANDLER = 0x004f2390;
// HumanAI_MoveTo / HumanAI_RunTo: walk/run the AI along its graph (0x004518E0,
// 0x004522A0). `dest` is a graph node id; `pos` refines the destination.
inline constexpr std::uint32_t HUMAN_AI_MOVE_TO_HANDLER = 0x004518e0;
inline constexpr std::uint32_t HUMAN_AI_RUN_TO_HANDLER = 0x004522a0;
} // namespace HASH

namespace WEAPON {
// Returns the dropped weapon task, or null when the drop was rejected.
NATIVE_DECL Task *DROP(Task *w);
} // namespace WEAPON

namespace AI {
// Both return non-zero once the AI has finished the move, matching retail use.
NATIVE_DECL char MOVE_TO(HumanAI *ai, int dest, const Vec3d *pos, char replan);
NATIVE_DECL char RUN_TO(HumanAI *ai, int dest, const Vec3d *pos, char replan);
} // namespace AI

// Number of bindings contributed by this unit; the registry total includes them.
std::size_t ExtraBindingCount() noexcept;
// Registers these bindings in the verified native catalogue.
bool loadExtraNativeBindings(igi::NativeCatalogue &catalogue);
} // namespace IGI
