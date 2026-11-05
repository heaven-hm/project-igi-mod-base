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

bytes=$(r2 -2 -q -c 's 0x00416260; p8 7' "$binary" | tr -d '[:space:]')
[ "$bytes" = "535657e8780100" ] || {
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
r2 -2 -q -c 's 0x00416260; pd 7; s 0x00402260; pd 7; s 0x00401050; pd 7' "$binary"
