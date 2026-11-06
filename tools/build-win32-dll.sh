#!/usr/bin/env bash
#
# Cross-build the Windows x86 (Win32) SDK artefacts from macOS or Linux.
#
# The Windows targets must be produced by a 32-bit mingw-w64 toolchain. CMake
# caches the first compiler it sees, so a tree that was configured with the host
# compiler (AppleClang/gcc) keeps failing with "ld: library 'System' not found".
# This script therefore sets C, C++ and RC compilers explicitly and discards a
# build tree whose cache still points at the host tools.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
configuration=Debug
target=igi_menu_manager_internal
clean=0

while (($#)); do
    case "$1" in
        --debug) configuration=Debug ;;
        --release) configuration=Release ;;
        --target) shift; target=${1:?missing target name} ;;
        --clean) clean=1 ;;
        --help)
            printf 'usage: %s [--debug|--release] [--target cmake-target] [--clean]\n' "$0"
            exit 0
            ;;
        *) printf 'unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
    shift
done

build_dir="$root/build-win32-${configuration,,}"

c_compiler=${IGI_WIN32_CC:-i686-w64-mingw32-gcc}
cxx_compiler=${IGI_WIN32_CXX:-i686-w64-mingw32-g++}
rc_compiler=${IGI_WIN32_RC:-i686-w64-mingw32-windres}

for tool in "$c_compiler" "$cxx_compiler" "$rc_compiler"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'error: missing 32-bit mingw-w64 tool: %s\n' "$tool" >&2
        printf 'hint: brew install mingw-w64\n' >&2
        exit 1
    fi
done

if ((clean)); then
    rm -rf "$build_dir"
fi

# A cache created with the host compiler cannot be converted in place; CMake
# would keep using AppleClang/gcc and fail to link a Windows PE image.
if [[ -f "$build_dir/CMakeCache.txt" ]]; then
    cached_c=$(sed -n 's/^CMAKE_C_COMPILER:[^=]*=//p' "$build_dir/CMakeCache.txt" | head -1)
    cached_cxx=$(sed -n 's/^CMAKE_CXX_COMPILER:[^=]*=//p' "$build_dir/CMakeCache.txt" | head -1)
    if [[ "$cached_c" != *mingw* || "$cached_cxx" != *mingw* ]]; then
        printf 'discarding stale build tree (host compiler): %s\n' "$build_dir"
        rm -rf "$build_dir"
    fi
fi

cmake -S "$root" -B "$build_dir" \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_SYSTEM_PROCESSOR=x86 \
    -DCMAKE_C_COMPILER="$c_compiler" \
    -DCMAKE_CXX_COMPILER="$cxx_compiler" \
    -DCMAKE_RC_COMPILER="$rc_compiler" \
    -DCMAKE_BUILD_TYPE="$configuration"
cmake --build "$build_dir" --parallel --target "$target"
printf 'built: %s\n' "$build_dir"
