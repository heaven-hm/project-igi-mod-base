#pragma once

#include <cstdint>
#include <optional>

namespace igi {
// The retail IGI.exe keeps the params authored by humanplayer.qvm in BSS:
//   gMoveScale    0x56E1E8
//   gJumpSpeed    0x56E1EC  (DefineHumanPlayerGeneral arg 1: jump speed)
//   gJumpUp       0x56E1F0  (arg 2: upward jump velocity, i.e. jump height)
//   gHumanAirSpeed 0x56E1F4 (arg 3: air control speed)
// The qvm writes km/h values converted by kKmhToUnits * kPerFrameHour
// (0x5334A4 * 0x5334A0). Reading these globals after HumanPlayer_LoadParameters
// shows exactly what the shipped humanplayer.qsc defined, and writing them
// commits new values to the running player on the next input update.
struct HumanPlayerQvmParams {
    float moveScale = 1.0F;
    float jumpSpeedKmh = 0.0F;
    float jumpUpKmh = 0.0F;
    float airSpeedKmh = 0.0F;
    // km/h -> retail units-per-frame factor read from the live constants. 1.0
    // when the constants were unavailable, in which case the values above are
    // the raw globals.
    float kmhScale = 1.0F;
    bool live = false;
};

// Reads the live BSS block through ReadProcessMemory, so a wrong address or a
// partially torn-down module fails cleanly instead of faulting.
std::optional<HumanPlayerQvmParams> readHumanPlayerQvmParams();
// Writes the four globals back through WriteProcessMemory. The caller must pass
// values in the same units as readHumanPlayerQvmParams (km/h, or raw when
// kmhScale is 1.0). Returns false when any write failed.
bool writeHumanPlayerQvmParams(const HumanPlayerQvmParams &values);
// Re-runs LOCAL:humanplayer/humanplayer.qsc through the retail loader
// (HumanPlayer_LoadParameters at 0x4137A0), restoring the authored values.
bool reloadHumanPlayerParameters();
// True when the host is the verified 32-bit retail process.
bool humanPlayerParamsAvailable() noexcept;
} // namespace igi
