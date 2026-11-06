import argparse
import csv
import datetime
import itertools
import os
import random
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
GAME = Path(os.environ.get("IGI_GAME_DIR", str(Path.home() / "Downloads/IGI")))
PREFIX = Path(os.environ.get("IGI_WINEPREFIX", str(Path.home() / "igi-test-prefix")))
WINE = Path(os.environ.get("WINE_BIN", "/Applications/Wine Stable.app/Contents/Resources/wine/bin/wine"))
SERVER = WINE.with_name("wineserver")
def _detect_build_dir():
    # tools/build-win32-dll.sh creates build-win32-release / build-win32-debug.
    # Prefer an explicit override, then the release tree, then debug, then the
    # legacy bare name so this script keeps working with older layouts.
    explicit = os.environ.get("IGI_WIN32_BUILD_DIR")
    if explicit:
        return Path(explicit)
    for name in ("build-win32-release", "build-win32-debug", "build-win32"):
        candidate = ROOT / name
        if candidate.is_dir():
            return candidate
    return ROOT / "build-win32-release"


def _config_key(name, fallback):
    """Read a hotkey name from the deployed config.ini so the injected key
    matches whatever start_key/end_key the game folder is configured with."""
    try:
        for line in (GAME / "config.ini").read_text(encoding="utf-8", errors="replace").splitlines():
            line = line.strip()
            if not line or line.startswith(";") or line.startswith("#") or line.startswith("["):
                continue
            if "=" in line:
                key, value = line.split("=", 1)
                if key.strip().lower() == name:
                    return value.strip().lower()
    except OSError:
        pass
    return fallback


BUILD = _detect_build_dir()
TEST_BUILD = Path(os.environ.get("IGI_TEST_BUILD_DIR", str(ROOT / "build-macos-verify")))
LOGDIR = Path(os.environ.get("IGI_VALIDATION_LOG_DIR", "/private/tmp/igi-live-validation"))
SDK_LOG = GAME / "igi_sdk.log"
START_KEY = _config_key("start_key", "f5")
END_KEY = _config_key("end_key", "f12")
ENV = dict(os.environ, WINEPREFIX=str(PREFIX), IGI_WINEPREFIX=str(PREFIX), WINEDEBUG="-all,+seh", IGI_ESCAPE_BEFORE_INJECT="true", IGI_ESCAPE_SETTLE_SECONDS="3")
# The mod menu exists only once the generated screen is attached in game.
MENU_ATTACHED = "internal menu screen: attached"
MENU_FAILED = "internal menu screen: Task_RunScript failed"
GAME_ROOT_MISSING = "internal menu screen: game root task unavailable"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--attempts", type=int, default=100, help="maximum attempts; 0 allows unlimited retries")
    parser.add_argument("--once", action="store_true", default=True, help="stop after the first successful attempt")
    args = parser.parse_args()
    if args.attempts < 0:
        parser.error("--attempts must be non-negative")
    LOGDIR.mkdir(parents=True, exist_ok=True)
    report = LOGDIR / ("report-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + ".log")

    def log(message):
        line = f"{datetime.datetime.now().isoformat(timespec='seconds')} {message}"
        print(line, flush=True)
        with report.open("a", encoding="utf-8") as output:
            output.write(line + "\n")

    def run(command, timeout=60, cwd=None):
        try:
            result = subprocess.run([str(x) for x in command], cwd=cwd, env=ENV, capture_output=True, text=True, errors="replace", timeout=timeout)
            log(f"command={command!r} exit={result.returncode}\n{result.stdout}{result.stderr}")
            return result
        except subprocess.TimeoutExpired as error:
            log(f"timeout command={command!r} after={timeout}s output={error.stdout!r} error={error.stderr!r}")
            return None

    def cleanup():
        run([SERVER, "-k"], 15)
        run([SERVER, "-w"], 15)

    def pid():
        result = run([WINE, "cmd", "/c", 'tasklist /FI "IMAGENAME eq igi.exe" /FO CSV /NH'], 15)
        if result and result.returncode == 0:
            for row in csv.reader(result.stdout.splitlines()):
                if len(row) > 1 and row[0].lower() == "igi.exe" and row[1].isdigit():
                    return row[1]
        return None

    def sdk_delta(offset):
        try:
            with SDK_LOG.open("rb") as source:
                source.seek(offset)
                return source.read().decode("utf-8", "replace")
        except OSError:
            return ""

    log(f"report={report}")
    for path in (WINE, SERVER, GAME / "igi.exe", GAME / "run-igi-wine.sh"):
        if not path.exists():
            log(f"FAILURE missing={path}")
            return 1
    for name in ("dinput.dll", "msvfw32.dll", "D3D8.dll", "D3D9.dll", "D3DImm.dll", "DDraw.dll", "IGI-Natives-SDK-IGI1.dll", "IGIPatch.dll", "freeglut.dll", "glew32.dll", "igi-probe.dll", "libgcc_s_sjlj-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll"):
        proxy = GAME / name
        if proxy.exists():
            cleanup()
            backup = GAME / ("disabled-proxies-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
            backup.mkdir(exist_ok=True)
            shutil.move(str(proxy), str(backup / proxy.name))
            log(f"moved local proxy={proxy} recoverable_backup={backup / proxy.name}")
    for command in (["ctest", "--test-dir", TEST_BUILD, "-R", "^igi_demo_natives_unicorn$", "--output-on-failure"],
                    ["cmake", "--build", BUILD, "--parallel", "--target", "igi_menu_manager_internal", "igi1_injector", "igi1_keypress"]):
        result = run(command, 180)
        if not result or result.returncode:
            log("FAILURE emulator or build failed")
            return 1
    for source, dest in (("IGI-MenuManager-Internal.dll", "IGI-MenuManager-Internal.dll"), ("IGI1-Injector.exe", "IGI-Injector.exe"), ("IGI1-Keypress.exe", "IGI1-Keypress.exe")):
        shutil.copy2(BUILD / source, GAME / dest)
    levels = []
    for attempt in itertools.count(1):
        if args.attempts and attempt > args.attempts:
            break
        if not levels:
            levels = random.sample(range(1, 15), 14)
        level = f"level{levels.pop()}"
        log(f"attempt={attempt}/{args.attempts} level={level}")
        if attempt % 2 == 0:
            ENV["WINEDLLOVERRIDES"] = "winebus.sys=d;winehid.sys=d;winemenubuilder.exe=d;dsound=b;dmusic=b;dinput=b;ddraw=b;d3dimm=b;IGIPatch=d"
            ENV.pop("DISPLAY", None)
            log("profile=existing-igi-launcher-overrides")
        else:
            ENV.pop("WINEDLLOVERRIDES", None)
            log("profile=default")
        cleanup()
        offset = SDK_LOG.stat().st_size if SDK_LOG.exists() else 0
        runner_log = LOGDIR / f"{report.stem}-attempt-{attempt}.log"
        with runner_log.open("w", encoding="utf-8") as output:
            runner = subprocess.Popen([str(GAME / "run-igi-wine.sh"), str(GAME / "IGI-MenuManager-Internal.dll"), level, "true"], cwd=GAME, env=ENV, stdout=output, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 120
            target = None
            sent = False
            toggle_sent = False
            while time.monotonic() < deadline:
                output.flush()
                text = runner_log.read_text(encoding="utf-8", errors="replace")
                delta = sdk_delta(offset)
                if sent and MENU_ATTACHED in delta:
                    if not toggle_sent:
                        keypath = "Z:" + str(GAME / "IGI1-Keypress.exe").replace("/", "\\\\")
                        # Close with the configured end key, then reopen with the
                        # configured start key. Both come from config.ini so the
                        # validation follows whatever the game folder specifies.
                        dismissed = run([WINE, keypath, "--pid", target, END_KEY], 15)
                        time.sleep(1)
                        reopened = run([WINE, keypath, "--pid", target, START_KEY], 15)
                        toggle_sent = bool(dismissed and not dismissed.returncode and reopened and not reopened.returncode)
                        if not toggle_sent:
                            log(f"attempt={attempt} {END_KEY} close / {START_KEY} reopen sequence failed pid={target}")
                            break
                        log(f"attempt={attempt} configured {END_KEY} close then {START_KEY} reopen sent to verify menu toggle pid={target}")
                    elif delta.count(MENU_ATTACHED) >= 2:
                        log(f"SUCCESS pid={target} DLL injection, {START_KEY} menu open, {END_KEY} close and {START_KEY} reopen with both menu attachments confirmed\n{delta}")
                        return 0
                if "Unhandled page fault" in text or "Unhandled exception" in text:
                    log(f"attempt={attempt} crash detected")
                    time.sleep(5)
                    break
                if sent and (MENU_FAILED in delta or GAME_ROOT_MISSING in delta):
                    log(f"attempt={attempt} menu attach failed\n{delta}")
                    break
                if "internal DLL attached" in delta and not sent:
                    target = pid()
                    if target:
                        keypath = "Z:" + str(GAME / "IGI1-Keypress.exe").replace("/", "\\")
                        hotkey = run([WINE, keypath, "--pid", target, START_KEY], 15)
                        sent = bool(hotkey and not hotkey.returncode)
                        if not sent:
                            log(f"attempt={attempt} keypress failed pid={target}")
                            break
                        log(f"attempt={attempt} F5 sent after runner-confirmed pre-injection Escape pid={target}")
                if runner.poll() is not None:
                    final_delta = sdk_delta(offset)
                    if sent and toggle_sent and final_delta.count(MENU_ATTACHED) >= 2:
                        log(f"SUCCESS pid={target} Escape-before-inject, DLL injection, initial F5, dismissed-screen F5 reopen and both menu attachments confirmed\n{final_delta}")
                        return 0
                    log(f"attempt={attempt} runner exited={runner.returncode}")
                    break
                time.sleep(1)
            log(f"attempt={attempt} failed runner_log={runner_log}\n{sdk_delta(offset)}")
            cleanup()
            try:
                runner.wait(timeout=10)
            except subprocess.TimeoutExpired:
                runner.terminate()
                runner.wait(timeout=5)
    log(f"FAILURE attempts exhausted report={report}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
