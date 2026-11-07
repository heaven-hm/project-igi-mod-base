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
target=igi_basic_mod_menu
clean=0
demo=
verify=0
deploy=0

while (($#)); do
    case "$1" in
        --debug) configuration=Debug ;;
        --release) configuration=Release ;;
        --target) shift; target=${1:?missing target name} ;;
        --demo) shift; demo=${1:?missing demo source path} ;;
        --verify) verify=1 ;;
        --deploy) deploy=1 ;;
        --clean) clean=1 ;;
        --help)
            printf 'usage: %s [--debug|--release] [--target cmake-target] [--demo source.cpp] [--verify] [--deploy] [--clean]\n' "$0"
            exit 0
            ;;
        *) printf 'unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
    shift
done

if [[ -f "$root/.env" ]]; then
    set -a
    . "$root/.env"
    set +a
fi

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

case "$target" in
    igi_advanced_mod_menu) artifact=${IGI_DLL_NAME:-igi-advanced-mod-internal.dll} ;;
    igi_simple_mod_menu) artifact=${IGI_DLL_NAME:-igi-simple-mod-internal.dll} ;;
    igi_basic_mod_menu) artifact=${IGI_DLL_NAME:-igi-basic-mod-internal.dll} ;;
    igi_advanced_mod_menu_external) artifact=${IGI_DLL_NAME:-igi-advanced-mod-external.dll} ;;
    igi_simple_mod_menu_external) artifact=${IGI_DLL_NAME:-igi-simple-mod-external.dll} ;;
    igi_basic_mod_menu_external) artifact=${IGI_DLL_NAME:-igi-basic-mod-external.dll} ;;
    igi_injector) artifact=IGI-Injector.exe ;;
    igi_keypress) artifact=IGI-Keypress.exe ;;
    igi_natives_inject_test) artifact=${IGI_DLL_NAME:-igi-natives-inject-test.dll} ;;
    *) artifact=${IGI_DLL_NAME:-} ;;
esac

if [[ -n "$artifact" && -f "$build_dir/$artifact" ]]; then
    mkdir -p "$root/binaries"
    cp "$build_dir/$artifact" "$root/binaries/$artifact"
    printf 'copied artifact to binaries: %s\n' "$root/binaries/$artifact"
fi

if ((verify)); then
    if [[ -z ${IGI_EXE:-} ]]; then
        printf 'error: --verify requires IGI_EXE=/path/to/igi.exe (or .env)\n' >&2
        exit 2
    fi
    if [[ -z "$demo" ]]; then
        case "$target" in
            igi_advanced_mod_menu) demo=demos/internal/advanced-mod/advanced_menu.cpp ;;
            igi_simple_mod_menu) demo=demos/internal/simple-mod/simple_menu.cpp ;;
            igi_basic_mod_menu) demo=demos/internal/basic-mod/basic_menu.cpp ;;
            igi_advanced_mod_menu_external) demo=demos/external/advanced-mod/advanced_menu.cpp ;;
            igi_simple_mod_menu_external) demo=demos/external/simple-mod/simple_menu.cpp ;;
            igi_basic_mod_menu_external) demo=demos/external/basic-mod/basic_menu.cpp ;;
            igi_natives_inject_test) demo=demos/status-message/status_message_demo.cpp ;;
            *) printf 'error: --demo is required for target %s\n' "$target" >&2; exit 2 ;;
        esac
    fi
    [[ -f "$root/$demo" ]] || { printf 'error: demo not found: %s\n' "$demo" >&2; exit 2; }
    [[ -n "$artifact" && -f "$build_dir/$artifact" ]] || { printf 'error: built DLL not found; set IGI_DLL_NAME for target %s\n' "$target" >&2; exit 1; }
    python3 "$root/tools/igi_runtime_harness.py" --path "$demo" --exe "$IGI_EXE" --dll "$build_dir/$artifact"
    python3 "$root/tools/igi_runtime_harness.py" --verify-menu-helpers --exe "$IGI_EXE" --dll "$build_dir/$artifact"
fi

if ((deploy)); then
    game_dir=${IGI_GAME:-}
    if [[ -z "$game_dir" && -n ${IGI_EXE:-} ]]; then game_dir=$(dirname "$IGI_EXE"); fi
    [[ -n "$game_dir" && -d "$game_dir" ]] || { printf 'error: --deploy requires IGI_GAME=/path/to/game or IGI_EXE=/path/to/igi.exe\n' >&2; exit 2; }
    [[ -n "$artifact" && -f "$build_dir/$artifact" ]] || { printf 'error: built DLL not found; set IGI_DLL_NAME for target %s\n' "$target" >&2; exit 1; }
    cp "$build_dir/$artifact" "$game_dir/$artifact"
    printf 'deployed: %s\n' "$game_dir/$artifact"
fi
printf 'built: %s\n' "$build_dir"
