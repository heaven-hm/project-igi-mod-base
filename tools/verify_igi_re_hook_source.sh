#!/bin/sh
set -eu

igi_re_root=${1:-${IGI_RE_ROOT:-}}
[ -n "$igi_re_root" ] || { echo "usage: $0 /path/to/IGI-RE (or set IGI_RE_ROOT)" >&2; exit 2; }
game="$igi_re_root/src/game/game.c"
flow="$igi_re_root/src/flow/flow.c"
flow_header="$igi_re_root/src/flow/flow.h"
jump="$igi_re_root/src/flow/jump.c"

for file in "$game" "$flow" "$flow_header" "$jump"; do
    [ -f "$file" ] || {
        echo "missing IGI-RE source file: $file" >&2
        exit 1
    }
done

grep -Fq 'TaskType_SetHandler2(gGameType, 1, Game_RunHandler);' "$game"
grep -Fq 'Script_RegisterConstant("GAME_FREQUENCY", 30);' "$game"
grep -Fq 'void Game_RunHandler(Game *g)' "$game"
grep -Fq 'HOOK(0x416260, Game_RunHandler);' "$game"
grep -Fq 'void Flow_RunHandler(Flow *flow)' "$flow"
grep -Fq 'HOOK(0x402260, Flow_RunHandler);' "$flow"
grep -Fq 'void Flow_RunHandler(Flow *flow);' "$flow_header"
grep -Fq 'typedef void (*GameRunHandler)(void *task);' "$jump"
grep -Fq 'run = gTaskRunHandlers[*(uint16_t *)((char *)Flow_Get() + 0x1C)];' "$jump"
grep -Fq 'run(Flow_Get());' "$jump"
grep -Fq 'HOOK(0x401050, Game_Update);' "$jump"

echo "verified IGI-RE source registration and signatures"
echo "verified Game_RunHandler cadence source: GAME_FREQUENCY=30"
echo "verified Game_Update dispatches the active task-slot-1 handler"
