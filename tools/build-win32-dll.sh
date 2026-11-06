#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
configuration=Debug
target=igi_menu_manager_internal

while (($#)); do
    case "$1" in
        --debug) configuration=Debug ;;
        --release) configuration=Release ;;
        --target) shift; target=${1:?missing target name} ;;
        --help)
            printf 'usage: %s [--debug|--release] [--target cmake-target]\n' "$0"
            exit 0
            ;;
        *) printf 'unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
    shift
done

build_dir="$root/build-win32-${configuration,,}"
compiler=${IGI_WIN32_CXX:-i686-w64-mingw32-g++}
cmake -S "$root" -B "$build_dir" -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_CXX_COMPILER="$compiler" -DCMAKE_BUILD_TYPE="$configuration"
cmake --build "$build_dir" --parallel --target "$target"
printf 'built: %s\n' "$build_dir"
