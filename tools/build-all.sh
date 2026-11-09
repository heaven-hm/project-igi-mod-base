#!/usr/bin/env bash
#
# Build all Project I.G.I. Mod Menu DLLs (Win32) and deploy to IGI_PATH (.env).
#
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
configuration=Release
clean=0
cli_dest=""

while (($#)); do
    case "$1" in
        --debug) configuration=Debug ;;
        --release) configuration=Release ;;
        --clean) clean=1 ;;
        --dest|--path|--igi-path) shift; cli_dest=${1:?missing destination path} ;;
        --help|-h)
            printf 'usage: %s [--debug|--release] [--clean] [--dest /path/to/game]\n' "$0"
            printf '\nReads IGI_PATH, IGI_GAME, or IGI_EXE from .env if present.\n'
            exit 0
            ;;
        *) printf 'unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
    shift
done

# Load environment configuration from .env if it exists
if [[ -f "$root/.env" ]]; then
    set -a
    . "$root/.env"
    set +a
fi

# Resolve destination game directory from CLI flag, .env, or environment variables
game_dir="$cli_dest"
if [[ -z "$game_dir" ]]; then
    game_dir="${IGI_PATH:-${IGI_GAME:-}}"
fi
if [[ -z "$game_dir" && -n "${IGI_EXE:-}" ]]; then
    game_dir=$(dirname "$IGI_EXE")
fi

build_dir="$root/build-win32-${configuration,,}"

c_compiler=${IGI_WIN32_CC:-i686-w64-mingw32-gcc}
cxx_compiler=${IGI_WIN32_CXX:-i686-w64-mingw32-g++}
rc_compiler=${IGI_WIN32_RC:-i686-w64-mingw32-windres}

# Strictly enforce 32-bit (x86) and forbid 64-bit (x86_64) toolchains
if [[ "$c_compiler" == *x86_64* || "$cxx_compiler" == *x86_64* ]]; then
    printf 'error: 64-bit compiler rejected: %s\n' "$cxx_compiler" >&2
    printf 'Project I.G.I. (igi.exe) is 32-bit x86 only; 64-bit builds are strictly forbidden.\n' >&2
    exit 1
fi

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

# Detect stale build tree or moved paths
if [[ -f "$build_dir/CMakeCache.txt" ]]; then
    cached_c=$(sed -n 's/^CMAKE_C_COMPILER:[^=]*=//p' "$build_dir/CMakeCache.txt" | head -1)
    cached_cxx=$(sed -n 's/^CMAKE_CXX_COMPILER:[^=]*=//p' "$build_dir/CMakeCache.txt" | head -1)
    cached_home=$(sed -n 's/^CMAKE_HOME_DIRECTORY:[^=]*=//p' "$build_dir/CMakeCache.txt" | head -1)
    cached_bdir=$(sed -n 's/^CMAKE_CACHEFILE_DIR:[^=]*=//p' "$build_dir/CMakeCache.txt" | head -1)
    if [[ "$cached_c" != *mingw* || "$cached_cxx" != *mingw* || "$cached_home" != "$root" || "$cached_bdir" != "$build_dir" ]]; then
        printf 'discarding stale build tree: %s\n' "$build_dir"
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

targets=(
    igi_basic_mod_menu
    igi_simple_mod_menu
    igi_advanced_mod_menu
    igi_heaven_mod_menu
    igi_basic_mod_menu_external
    igi_simple_mod_menu_external
    igi_advanced_mod_menu_external
    igi_heaven_mod_menu_external
)

printf '\nBuilding %d mod menu targets in %s mode...\n' "${#targets[@]}" "$configuration"
cmake --build "$build_dir" --parallel --target "${targets[@]}"

# Stage all artifacts in local binaries/
mkdir -p "$root/binaries"
cp -v "$build_dir"/igi-*-mod-*.dll "$root/binaries/"

# Copy to resolved game directory
if [[ -n "$game_dir" && -d "$game_dir" ]]; then
    printf '\nDeploying DLLs to game directory: %s\n' "$game_dir"
    cp -v "$build_dir"/igi-*-mod-*.dll "$game_dir/"
    printf '\nSuccessfully built and deployed all DLLs to %s\n' "$game_dir"
elif [[ -n "$game_dir" ]]; then
    printf '\nWarning: Destination path "%s" does not exist; deployed to binaries/ only.\n' "$game_dir" >&2
else
    printf '\nNotice: No IGI_PATH defined in .env or environment; artifacts deployed to binaries/ only.\n'
    printf 'hint: add IGI_PATH=/path/to/igi/game to your .env file.\n'
fi
