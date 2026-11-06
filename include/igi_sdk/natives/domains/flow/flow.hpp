#pragma once

#include "igi_sdk/natives/native_caller.hpp"

namespace igi::natives::flow {

// IGI-RE: FramesSet(int fps), 0x00402820, cdecl.
// Queues work for the verified Game_RunHandler thread.
void FRAMES_SET(int frames);
TaskId FRAMES_SET_QUEUED(NativeCaller& caller, int frames);

} // namespace igi::natives::flow
