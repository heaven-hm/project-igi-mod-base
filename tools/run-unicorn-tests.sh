#!/usr/bin/env bash
#
# Run every Unicorn-emulator verification directly - no ctest, no host-native
# test binaries. Executes the real retail igi.exe image (and the mod DLL) inside
# IGI-RE's Unicorn emulator and checks the retail routines the menu depends on.
#
# Usage:
#   tools/run-unicorn-tests.sh [--release|--debug]
#
# Environment:
#   IGI_EXE  path to the supported retail 32-bit igi.exe (or set in .env)
#   IGI_DLL  optional explicit DLL; defaults to the Heaven internal build
#
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
configuration=Release
while (($#)); do
    case "$1" in
        --debug) configuration=Debug ;;
        --release) configuration=Release ;;
        --help|-h)
            printf 'usage: %s [--release|--debug]\n' "$0"
            exit 0 ;;
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
harness="$root/tools/igi_runtime_harness.py"
python_bin=${IGI_HARNESS_PYTHON:-/usr/bin/python3}
arch_prefix=""
if [[ "$(uname -s)" == "Darwin" && "$(uname -m)" == "arm64" ]]; then
    arch_prefix="arch -x86_64"
fi

if [[ -z "${IGI_EXE:-}" ]]; then
    printf 'error: set IGI_EXE to the supported retail igi.exe (or add it to .env)\n' >&2
    exit 1
fi
if [[ ! -f "$IGI_EXE" ]]; then
    printf 'error: IGI_EXE does not exist: %s\n' "$IGI_EXE" >&2
    exit 1
fi

export IGI_EXE
export IGI_DLL="${IGI_DLL:-$build_dir/igi-heaven-mod-internal.dll}"
if [[ ! -f "$IGI_DLL" ]]; then
    printf 'error: mod DLL not found: %s (run tools/build-all.sh first)\n' "$IGI_DLL" >&2
    exit 1
fi

# IGI-RE's emulator module; build it from the checkout when missing.
re_root=$(python3 - "$root" <<'PY'
import sys
from pathlib import Path
sys.path.insert(0, str(Path(sys.argv[1]) / "tools"))
import importlib.util
spec = importlib.util.spec_from_file_location("h", Path(sys.argv[1]) / "tools" / "igi_runtime_harness.py")
m = importlib.util.module_from_spec(spec)
import os
os.environ.setdefault("IGI_HARNESS_NO_RUN", "1")
spec.loader.exec_module(m)
print(m.discover_re_root())
PY
)
if [[ ! -f "$re_root/tests/emu.py" ]]; then
    printf 'error: IGI-RE emulator not found at %s/tests/emu.py\n' "$re_root" >&2
    exit 1
fi

run_case() {
    local name=$1
    shift
    printf '\n=== Unicorn: %s ===\n' "$name"
    if [[ -n "$arch_prefix" ]]; then
        $arch_prefix "$python_bin" "$harness" "$@"
    else
        "$python_bin" "$harness" "$@"
    fi
}

failures=0
run_case "frame dispatch" --frames 20 || failures=$((failures + 1))
run_case "heaven retail flows" --verify-heaven-flows || failures=$((failures + 1))
run_case "humanplayer.qvm physics" --verify-human-physics || failures=$((failures + 1))
run_case "menu helpers cleanup" --verify-menu-helpers || failures=$((failures + 1))

printf '\n=== Unicorn test suite: %s ===\n' \
    "$([[ $failures -eq 0 ]] && echo 'ALL PASSED' || echo "$failures case(s) FAILED")"
exit $((failures > 0))
