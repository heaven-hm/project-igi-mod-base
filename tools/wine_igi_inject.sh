#!/usr/bin/env sh
# Inject the SDK DLL into an IGI process running under Wine.
#
#   tools/wine_igi_inject.sh <dll> [exe-name] [injector.exe]
#
# WINE_BIN and WINEPREFIX can be overridden from the environment.
set -eu

dll=${1:-}
exe_name=${2:-igi.exe}
injector=${3:-}

if [ -n "${WINE_BIN:-}" ]; then
    wine_bin=$WINE_BIN
elif [ "$(uname -s)" = Darwin ]; then
    wine_bin="/Applications/Wine Stable.app/Contents/Resources/wine/bin/wine"
else
    wine_bin=$(command -v wine || true)
fi
export WINEPREFIX=${WINEPREFIX:-$HOME/igi-test-prefix}

if [ -z "$dll" ]; then
    printf 'usage: %s <dll> [exe-name] [injector.exe]\n' "$0" >&2
    exit 2
fi
if [ ! -f "$dll" ]; then
    printf 'error: missing DLL: %s\n' "$dll" >&2
    exit 2
fi
if [ -z "$wine_bin" ] || [ ! -x "$wine_bin" ]; then
    printf 'error: wine not found at %s (set WINE_BIN)\n' "$wine_bin" >&2
    exit 2
fi

dll_abs=$(cd "$(dirname "$dll")" && pwd)/$(basename "$dll")

if [ -z "$injector" ]; then
    for candidate in \
        "$(dirname "$0")/IGI-Injector.exe" \
        "${TMPDIR:-/tmp}/igi-sdk-win32-debug/IGI-Injector.exe" \
        "${TMPDIR:-/tmp}/igi-sdk-win32/IGI-Injector.exe"
    do
        if [ -f "$candidate" ]; then injector=$candidate; break; fi
    done
fi
if [ -z "$injector" ] || [ ! -f "$injector" ]; then
    printf 'error: injector not found; build it with: cmake --build <dir> --target igi_injector\n' >&2
    exit 2
fi

injector_abs=$(cd "$(dirname "$injector")" && pwd)/$(basename "$injector")
windows_dll=$("$wine_bin" winepath -w "$dll_abs" 2>/dev/null | tr -d '\r')
windows_injector=$("$wine_bin" winepath -w "$injector_abs" 2>/dev/null | tr -d '\r')

printf 'injecting %s into %s (WINEPREFIX=%s)\n' "$windows_dll" "$exe_name" "$WINEPREFIX"
exec "$wine_bin" "$windows_injector" -n "$exe_name" -i "$windows_dll" -v
