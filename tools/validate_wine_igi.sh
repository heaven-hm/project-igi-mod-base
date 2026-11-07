#!/usr/bin/env bash
# Rebuild and exercise the Windows x86 injected-menu path against a local Wine
# IGI install.  It is intentionally bounded: every failure preserves its logs
# and screenshots, then retries from a clean test prefix up to --attempts.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
game_dir=${IGI_GAME_DIR:-"$HOME/Downloads/IGI"}
prefix=${WINEPREFIX:-"$HOME/igi-test-prefix"}
wine_bin=${WINE_BIN:-"/Applications/Wine Stable.app/Contents/Resources/wine/bin/wine"}
build_win32=${IGI_WIN32_BUILD_DIR:-"$root/build-win32"}
build_macos=${IGI_MACOS_BUILD_DIR:-"$root/build-macos-verify"}
attempts=5

while (($#)); do
    case "$1" in
        --attempts) attempts=$2; shift 2 ;;
        -h|--help)
            echo "usage: $0 [--attempts N]"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

[[ -x "$wine_bin" ]] || { echo "Wine not found: $wine_bin" >&2; exit 2; }
[[ -f "$game_dir/igi.exe" ]] || { echo "IGI executable not found: $game_dir/igi.exe" >&2; exit 2; }
[[ -d "$prefix" ]] || { echo "Wine prefix not found: $prefix" >&2; exit 2; }

wine() { WINEPREFIX="$prefix" WINEDEBUG=-all "$wine_bin" "$@"; }
game_running() {
    wine cmd /c 'tasklist /FI "IMAGENAME eq igi.exe" /FO CSV /NH' 2>/dev/null |
        grep -qi '^"igi.exe"'
}
game_pid() {
    wine cmd /c 'tasklist /FI "IMAGENAME eq igi.exe" /FO CSV /NH' 2>/dev/null |
        awk -F ',' 'tolower($1) == "\"igi.exe\"" {gsub(/\"/, "", $2); print $2; exit}'
}
sdk_log="$game_dir/$(python3 "$root/tools/igi_log_config.py" "$game_dir")"
artifact_dir="${TMPDIR:-/tmp}/igi-live-validation"
mkdir -p "$artifact_dir"
keypress_win=""

echo "==> x86 emulator probe"
ctest --test-dir "$build_macos" -R '^igi_demo_natives_unicorn$' --output-on-failure

echo "==> Windows x86 build"
cmake --build "$build_win32" --parallel --target igi_basic_mod_menu igi1_injector igi1_keypress
install -m 755 "$build_win32/igi-basic-mod-internal.dll" "$game_dir/igi-basic-mod-internal.dll"
install -m 755 "$build_win32/IGI1-Injector.exe" "$game_dir/IGI-Injector.exe"
install -m 755 "$build_win32/IGI1-Keypress.exe" "$game_dir/IGI1-Keypress.exe"

for ((attempt = 1; attempt <= attempts; ++attempt)); do
    echo "==> Wine live attempt $attempt/$attempts"
    WINEPREFIX="$prefix" "$(dirname "$wine_bin")/wineserver" -k || true
    for _ in {1..10}; do
        game_running || break
        sleep 1
    done
    if game_running; then
        echo "attempt $attempt: stale igi.exe survived Wine-prefix shutdown" >&2
        continue
    fi
    rm -f "$artifact_dir/attempt-$attempt.log"
    (
        cd "$game_dir"
        # The supplied runner owns level launch, Windows PID discovery and DLL
        # injection. Its complete stdout/stderr is retained per attempt.
        IGI_SDK_AUTOSTART=1 ./run-igi-wine.sh "$game_dir/igi-basic-mod-internal.dll" level5 true
    ) >"$artifact_dir/attempt-$attempt.log" 2>&1 &
    runner_pid=$!

    ready=0
    for _ in {1..20}; do
        if game_running; then ready=1; break; fi
        sleep 1
    done
    if (( ! ready )); then
        echo "attempt $attempt: igi.exe was not running" >&2
        tail -n 80 "$artifact_dir/attempt-$attempt.log" || true
        wait "$runner_pid" || true
        continue
    fi
    win_pid=$(game_pid)
    [[ "$win_pid" =~ ^[0-9]+$ ]] || { echo "attempt $attempt: could not read igi.exe PID" >&2; continue; }
    echo "attempt $attempt: igi.exe Windows PID $win_pid"
    for _ in {1..20}; do
        grep -q 'injected into pid' "$artifact_dir/attempt-$attempt.log" && break
        sleep 1
    done
    if ! grep -q 'injected into pid' "$artifact_dir/attempt-$attempt.log"; then
        echo "attempt $attempt: runner did not confirm injection" >&2
        tail -n 120 "$artifact_dir/attempt-$attempt.log" || true
        WINEPREFIX="$prefix" "$(dirname "$wine_bin")/wineserver" -k || true
        wait "$runner_pid" || true
        continue
    fi

    /usr/sbin/screencapture -x "$artifact_dir/attempt-$attempt-before.png" || true
    keypress_win=$(wine winepath -w "$game_dir/IGI1-Keypress.exe" | tr -d '\r')
    [[ -n "$keypress_win" ]] || { echo "attempt $attempt: cannot translate key helper path" >&2; continue; }
    wine "$keypress_win" --pid "$win_pid" escape
    sleep 1
    wine "$keypress_win" --pid "$win_pid" f5
    sleep 5
    /usr/sbin/screencapture -x "$artifact_dir/attempt-$attempt-menu.png" || true

    if game_running && grep -q 'internal validation auto-start enabled' "$sdk_log" &&
       grep -q 'IGI 1 Display_AddFrameCallback scheduler hook installed' "$sdk_log" &&
       grep -q 'internal menu screen: attached and requested' "$sdk_log"; then
        echo "PASS: game stayed live and demo mod-menu screen attached (screen_id=990)."
        wine "$keypress_win" --pid "$win_pid" end
        sleep 2
        game_running || { echo "failure: game quit during End-key shutdown" >&2; exit 1; }
        echo "SUCCESS: x86 emulator, game launch, runner injection, native readiness, menu attach and End-key health check passed. Evidence: $artifact_dir"
        exit 0
    fi

    echo "attempt $attempt failed: game/menu health check did not pass; retrying from a clean Wine-prefix process state" >&2
    tail -n 120 "$sdk_log" || true
    tail -n 120 "$artifact_dir/attempt-$attempt.log" || true
    # A Wine crash dialog can leave the game and launcher waiting forever.
    # Preserve evidence first, then terminate only this test prefix so retry
    # cannot block on a stuck dialog or stale igi.exe.
    WINEPREFIX="$prefix" "$(dirname "$wine_bin")/wineserver" -k || true
    wait "$runner_pid" || true
done

echo "FAILURE: live validation exhausted $attempts attempts; evidence is in $artifact_dir" >&2
exit 1
