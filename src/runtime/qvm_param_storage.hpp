#pragma once

#include <cstdint>
#include <optional>

namespace igi {
// Params authored by DefineHumanPlayerGeneral in humanplayer.qvm, stored by the
// retail game in BSS. Addresses are absolute for the non-ASLR igi.exe:
//   gMoveScale      0x56E1E8  arg 0  player movement scale (1.0 = normal)
//   gJumpSpeed      0x56E1EC  arg 1  horizontal jump speed (km/h)
//   gJumpUp         0x56E1F0  arg 2  upward jump velocity / height (km/h)
//   gHumanAirSpeed  0x56E1F4  arg 3  air control speed (km/h)
//   gMaxDamage      0x56E204  arg 7  damage scale (retail 3.0)
//   gFenceDamage    0x56E208  arg 8  fence/electric damage scale
// The lean/peek params (args 4..6 at 0x56E1F8..0x56E200) are deliberately not
// exposed. The per-stance move/turn speed tables come after arg 8.
struct HumanPlayerQvmParams {
    float moveScale = 1.0F;
    float jumpSpeedKmh = 0.0F;
    float jumpUpKmh = 0.0F;
    float airSpeedKmh = 0.0F;
    float maxDamage = 0.0F;
    float fenceDamage = 0.0F;
    // km/h -> retail units-per-frame factor read from the live constants
    // (0x5334A4 * 0x5334A0). 1.0 when the constants were unavailable, in which
    // case the jump/air fields already hold the raw globals.
    float kmhScale = 1.0F;
    bool live = false;
};

// Reads the live BSS block through ReadProcessMemory, so a wrong address or a
// partially torn-down module fails cleanly instead of faulting.
std::optional<HumanPlayerQvmParams> readHumanPlayerQvmParams();
// Writes the six editable globals back through WriteProcessMemory. All values
// are in the same units readHumanPlayerQvmParams returned (unitless scales and
// km/h speeds). Returns false when any write failed.
bool writeHumanPlayerQvmParams(const HumanPlayerQvmParams &values);
// Re-runs LOCAL:humanplayer/humanplayer.qsc through the retail loader
// (HumanPlayer_LoadParameters at 0x4137A0), restoring the authored values.
bool reloadHumanPlayerParameters();
// True when the host is the verified 32-bit retail process.
bool humanPlayerParamsAvailable() noexcept;
} // namespace igi
