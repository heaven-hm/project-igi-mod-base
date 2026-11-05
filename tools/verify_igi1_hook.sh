#!/bin/sh
set -eu

binary=${1:-/Users/haseeb-mir/Downloads/IGI/IGI.EXE}
expected_hash=dea34892d417715873ea61d7b2ba5556655f1ad65385104348146f9679915a01
actual_hash=$(shasum -a 256 "$binary" | awk '{print $1}')
[ "$actual_hash" = "$expected_hash" ] || {
    echo "hash mismatch: $actual_hash" >&2
    exit 1
}

echo "verified SHA-256: $actual_hash"

# The hook displaces 8 bytes: push ebx/esi/edi plus the 5-byte call at +3.
bytes=$(r2 -2 -q -c 's 0x00416260; p8 8' "$binary" | tr -d '[:space:]')
[ "$bytes" = "535657e878010000" ] || {
    echo "Game_RunHandler prologue mismatch: $bytes" >&2
    exit 1
}

bytes=$(r2 -2 -q -c 's 0x00402260; p8 7' "$binary" | tr -d '[:space:]')
[ "$bytes" = "83ec0853555657" ] || {
    echo "Flow_RunHandler prologue mismatch: $bytes" >&2
    exit 1
}

bytes=$(r2 -2 -q -c 's 0x00401050; p8 7' "$binary" | tr -d '[:space:]')
[ "$bytes" = "558bec566a0068" ] || {
    echo "Game_Update prologue mismatch: $bytes" >&2
    exit 1
}

echo "verified Game_RunHandler, Flow_RunHandler, and Game_Update prologues"

metadata=$(r2 -2 -q -e scr.color=0 -c 'aaa; af @ 0x00416260; af @ 0x00402260; afij @ 0x00416260; afij @ 0x00402260; axt 0x00416260; axt 0x00402260; axt 0x00401050' "$binary")
printf '%s\n' "$metadata" | grep -Eq '"calltype":"cdecl".*"nargs":1' || {
    echo "expected cdecl one-argument metadata for Game_RunHandler/Flow_RunHandler" >&2
    exit 1
}
printf '%s\n' "$metadata" | grep -q '0x415632.*fcn\.00416260' || {
    echo "Game_RunHandler task-table registration xref missing" >&2
    exit 1
}
printf '%s\n' "$metadata" | grep -q '0x401fef.*fcn\.00402260' || {
    echo "Flow_RunHandler task-table registration xref missing" >&2
    exit 1
}
printf '%s\n' "$metadata" | grep -q '0x48f81b.*fcn\.00401050' || {
    echo "Game_Update outer call-flow xref missing" >&2
    exit 1
}

update_body=$(r2 -2 -q -e scr.color=0 -c 'aaa; pdf @ 0x00401050' "$binary")
printf '%s\n' "$update_body" | grep -Eq 'call +esi' || {
    echo "Game_Update indirect task-handler dispatch was not found" >&2
    exit 1
}

echo "verified cdecl arguments and task-table call flow"
r2 -2 -q -c 's 0x00416260; pd 7; s 0x00402260; pd 7; s 0x00401050; pd 7' "$binary"
