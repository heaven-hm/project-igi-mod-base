#!/bin/sh
set -eu

binary=${1:-${IGI_EXE:-}}
[ -n "$binary" ] || { echo "usage: $0 /path/to/igi.exe (or set IGI_EXE)" >&2; exit 2; }
expected_hash=dea34892d417715873ea61d7b2ba5556655f1ad65385104348146f9679915a01
objdump=${IGI_OBJDUMP:-i686-w64-mingw32-objdump}

command -v "$objdump" >/dev/null 2>&1 || {
    echo "missing PE i386 objdump; set IGI_OBJDUMP" >&2
    exit 1
}
[ -f "$binary" ] || {
    echo "missing target executable: $binary" >&2
    exit 1
}

actual_hash=$(shasum -a 256 "$binary" | awk '{print $1}')
[ "$actual_hash" = "$expected_hash" ] || {
    echo "hash mismatch: $actual_hash" >&2
    exit 1
}
echo "verified SHA-256: $actual_hash"

game_run=$($objdump -D -Mintel --start-address=0x00416260 --stop-address=0x00416270 "$binary")
flow_run=$($objdump -D -Mintel --start-address=0x00402260 --stop-address=0x00402270 "$binary")
game_update=$($objdump -D -Mintel --start-address=0x00401050 --stop-address=0x00401090 "$binary")

# The runtime displaces exactly these eight bytes and relocates the call at +3.
printf '%s\n' "$game_run" | grep -Eq '416260:.*push +ebx' &&
printf '%s\n' "$game_run" | grep -Eq '416261:.*push +esi' &&
printf '%s\n' "$game_run" | grep -Eq '416262:.*push +edi' &&
printf '%s\n' "$game_run" | grep -Eq '416263:.*call +0x4163e0' || {
    echo "Game_RunHandler body/prologue mismatch" >&2
    exit 1
}

printf '%s\n' "$flow_run" | grep -Eq '402260:.*sub +esp,0x8' &&
printf '%s\n' "$flow_run" | grep -Eq '402263:.*push +ebx' &&
printf '%s\n' "$flow_run" | grep -Eq '402264:.*push +ebp' || {
    echo "Flow_RunHandler body/prologue mismatch" >&2
    exit 1
}

# Game_Update selects the active task handler and executes it indirectly. It
# must remain an investigation reference, not the SDK hook target.
printf '%s\n' "$game_update" | grep -Eq '401072:.*mov +esi,DWORD PTR \[ecx\*4\+0xa970e0\]' &&
printf '%s\n' "$game_update" | grep -Eq '40107f:.*call +esi' &&
printf '%s\n' "$game_update" | grep -Eq '401084:.*pop +esi' &&
printf '%s\n' "$game_update" | grep -Eq '401086:.*ret' || {
    echo "Game_Update task dispatch/calling-convention mismatch" >&2
    exit 1
}

echo "verified Game_RunHandler body and cdecl-preserving return path"
echo "verified Flow_RunHandler as secondary flow-lifecycle evidence"
echo "verified Game_Update indirect task dispatch; it is not selected as a hook"
