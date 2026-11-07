#!/usr/bin/env python3
import argparse
import configparser
import csv
import datetime
import os
from pathlib import Path
import re
import subprocess
import sys
import time


def level_value(value):
    match = re.fullmatch(r"(?:level)?(\d+)", value.lower())
    if not match or not 1 <= int(match.group(1)) <= 14:
        raise argparse.ArgumentTypeError("level must be 1–14 or level1–level14")
    return "level" + str(int(match.group(1)))


def configured_key(game_dir):
    config = configparser.ConfigParser(interpolation=None, strict=False)
    config.read(game_dir / "project-igi-mod-config.ini", encoding="utf-8")
    key = config.get("mod_menu", "start_key", fallback="").strip().lower()
    if not key:
        key = config.get("mod_menu", "hotkey", fallback="").strip().lower()
    if not key:
        raise ValueError("project-igi-mod-config.ini does not specify [mod_menu] start_key; use --key")
    return key


def process_ids(output):
    result = []
    for row in csv.reader(output.splitlines()):
        if (
            len(row) > 1
            and row[0].strip().lower() == "igi.exe"
            and row[1].strip().isdigit()
        ):
            result.append(int(row[1].strip()))
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Launch a selected IGI level in Wine and inject an x86 DLL."
    )
    parser.add_argument(
        "level", type=level_value, help="level number 1–14, or level1–level14"
    )
    parser.add_argument("dll", type=Path, help="path to the x86 DLL to inject")
    parser.add_argument(
        "--press-menu-key",
        action="store_true",
        help="press the configured start key after injection",
    )
    parser.add_argument("--key", help="menu key override; implies --press-menu-key")
    parser.add_argument(
        "--no-escape", action="store_true", help="do not send Escape before injection"
    )
    parser.add_argument(
        "--attempts", type=int, default=3, help="maximum launch attempts (default: 3)"
    )
    parser.add_argument(
        "--reset-wine-prefix",
        action="store_true",
        help="restart the Wine server after a failed attempt (closes every process in this prefix)",
    )
    parser.add_argument(
        "--load-wait",
        type=float,
        default=10,
        help="seconds to wait for the level (default: 10)",
    )
    parser.add_argument(
        "--startup-timeout",
        type=float,
        default=45,
        help="seconds to wait for igi.exe (default: 45)",
    )
    parser.add_argument(
        "--game-dir",
        type=Path,
        default=Path(os.environ.get("IGI_GAME_DIR", Path.home() / "Downloads/IGI")),
    )
    parser.add_argument(
        "--wine",
        type=Path,
        default=Path(
            os.environ.get(
                "WINE_BIN",
                "/Applications/Wine Stable.app/Contents/Resources/wine/bin/wine",
            )
        ),
    )
    parser.add_argument(
        "--prefix",
        type=Path,
        default=Path(
            os.environ.get(
                "IGI_WINEPREFIX",
                os.environ.get("WINEPREFIX", Path.home() / "igi-test-prefix"),
            )
        ),
    )
    parser.add_argument("--injector", type=Path, help="path to IGI-Injector.exe")
    parser.add_argument("--keypress", type=Path, help="path to IGI1-Keypress.exe")
    parser.add_argument(
        "--log-dir",
        type=Path,
        default=Path(
            os.environ.get("IGI_VALIDATION_LOG_DIR", "/private/tmp/igi-live-validation")
        ),
    )
    args = parser.parse_args(argv)
    if args.attempts < 1 or args.load_wait < 0 or args.startup_timeout <= 0:
        parser.error(
            "attempts and startup timeout must be positive; load wait must be non-negative"
        )
    game = args.game_dir.expanduser().resolve()
    dll = args.dll.expanduser().resolve()
    wine = args.wine.expanduser().resolve()
    prefix = args.prefix.expanduser().resolve()
    injector = (args.injector or game / "IGI-Injector.exe").expanduser().resolve()
    keypress = (args.keypress or game / "IGI1-Keypress.exe").expanduser().resolve()
    need_keys = not args.no_escape or args.press_menu_key or args.key is not None
    for path in (
        wine,
        game / "igi.exe",
        dll,
        injector,
        *([keypress] if need_keys else []),
    ):
        if not path.is_file():
            parser.error(f"file not found: {path}")
    if not prefix.is_dir():
        parser.error(f"Wine prefix not found: {prefix}")
    try:
        menu_key = (
            (args.key.strip().lower() if args.key else configured_key(game))
            if args.press_menu_key or args.key
            else None
        )
        supported = {f"f{number}" for number in range(3, 13)} | {
            "home",
            "end",
            "pageup",
            "pgup",
            "pagedown",
            "pgdn",
            "insert",
            "ins",
            "delete",
            "del",
        }
        if menu_key is not None and menu_key not in supported:
            raise ValueError(
                f"unsupported menu key {menu_key!r}; use --key with a supported keypress name"
            )
    except (ValueError, configparser.Error, OSError) as error:
        parser.error(str(error))
    env = dict(
        os.environ,
        WINEPREFIX=str(prefix),
        WINEDEBUG=os.environ.get("IGI_WINEDEBUG", "-all"),
    )
    explicit_overrides = "WINEDLLOVERRIDES" in env
    if not explicit_overrides:
        env["WINEDLLOVERRIDES"] = "winedbg.exe=d"
    args.log_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    report = args.log_dir / f"run-igi-{stamp}.log"

    def log(message):
        line = f"{datetime.datetime.now().isoformat(timespec='seconds')} {message}"
        print(line, flush=True)
        with report.open("a", encoding="utf-8") as output:
            output.write(line + "\n")

    def run(command, timeout=20):
        try:
            result = subprocess.run(
                [str(part) for part in command],
                cwd=game,
                env=env,
                capture_output=True,
                text=True,
                errors="replace",
                timeout=timeout,
            )
            log(
                f"command={command!r} exit={result.returncode} output={result.stdout}{result.stderr}"
            )
            return result
        except (OSError, subprocess.TimeoutExpired) as error:
            log(f"command={command!r} failed: {error}")
            return None

    def pids():
        result = run(
            [wine, "cmd", "/c", 'tasklist /FI "IMAGENAME eq igi.exe" /FO CSV /NH'], 15
        )
        if result is None or result.returncode != 0:
            raise RuntimeError("could not query Wine processes")
        return process_ids(result.stdout)

    def winpath(path):
        result = run([wine, "winepath", "-w", path])
        if result is None or result.returncode or not result.stdout.strip():
            raise RuntimeError(f"could not translate path: {path}")
        return result.stdout.strip()

    def send(pid, key):
        result = run([wine, keypress_win, "--pid", pid, key])
        return result is not None and result.returncode == 0

    def crashed(path):
        text = path.read_text(encoding="utf-8", errors="replace").lower()
        return (
            "unhandled page fault" in text
            or "unhandled exception" in text
            or "wine: assertion failed" in text
        )

    log(f"report={report} level={args.level} dll={dll} prefix={prefix}")
    try:
        existing = pids()
        if len(existing) > 1:
            log(f"FAIL multiple existing igi.exe PID(s) {existing}; refusing to choose an injection target")
            return 1
        existing_pid = existing[0] if existing else None
        injector_win = winpath(injector)
        dll_win = winpath(dll)
        keypress_win = winpath(keypress) if need_keys else None
        for attempt in range(1, args.attempts + 1):
            if not explicit_overrides:
                env["WINEDLLOVERRIDES"] = (
                    "winedbg.exe=d"
                    if attempt % 2
                    else "winedbg.exe=d;winebus.sys=d;winehid.sys=d;winemenubuilder.exe=d;"
                    "dsound=b;dmusic=b;dinput=b;ddraw=b;d3dimm=b;IGIPatch=d"
                )
            wine_log = args.log_dir / f"run-igi-{stamp}-attempt-{attempt}.wine.log"
            log(
                f"attempt={attempt}/{args.attempts} level={args.level} overrides={env.get('WINEDLLOVERRIDES', '')} wine_log={wine_log}"
            )
            owned_pid = None
            runner = None
            injected_successfully = False
            with wine_log.open("w", encoding="utf-8") as output:
                try:
                    if existing_pid is not None:
                        owned_pid = existing_pid
                        log(f"attempt={attempt} reusing existing igi.exe PID {owned_pid}; skipping level launch and Escape")
                    else:
                        runner = subprocess.Popen(
                            [str(wine), str(game / "igi.exe"), "window", args.level],
                            cwd=game,
                            env=env,
                            stdout=output,
                            stderr=subprocess.STDOUT,
                        )
                        deadline = time.monotonic() + args.startup_timeout
                        while time.monotonic() < deadline and runner.poll() is None:
                            if crashed(wine_log):
                                raise RuntimeError("Wine crash detected during startup")
                            try:
                                current = pids()
                            except RuntimeError as error:
                                log(f"attempt={attempt} process query pending: {error}")
                                time.sleep(1)
                                continue
                            if len(current) == 1:
                                owned_pid = current[0]
                                break
                            if len(current) > 1:
                                raise RuntimeError(
                                    f"multiple igi.exe processes appeared: {current}"
                                )
                            time.sleep(1)
                    if owned_pid is not None:
                        if runner is not None:
                            time.sleep(args.load_wait)
                        if (
                            (runner is None or runner.poll() is None)
                            and owned_pid in pids()
                            and not crashed(wine_log)
                        ):
                            ready = runner is None or args.no_escape or send(owned_pid, "escape")
                            if ready and runner is not None and not args.no_escape:
                                time.sleep(3)
                            if (
                                ready
                                and (runner is None or runner.poll() is None)
                                and owned_pid in pids()
                                and not crashed(wine_log)
                            ):
                                injected = run(
                                    [wine, injector_win, "-p", owned_pid, "-i", dll_win]
                                )
                                if (
                                    injected is not None
                                    and injected.returncode == 0
                                    and (runner is None or runner.poll() is None)
                                    and owned_pid in pids()
                                    and not crashed(wine_log)
                                ):
                                    injected_successfully = True
                                    if menu_key is None or send(owned_pid, menu_key):
                                        log(
                                            f"SUCCESS pid={owned_pid} injection confirmed menu_key={menu_key or 'not requested'}; game remains running"
                                        )
                                        return 0
                                    log(
                                        "menu key delivery failed after injection; stopping to avoid injecting twice"
                                    )
                                    return 1
                except (OSError, RuntimeError) as error:
                    log(f"attempt={attempt} error={error}")
                finally:
                    if not injected_successfully:
                        log(f"attempt={attempt} failed; inspect {wine_log}")
                        if args.reset_wine_prefix:
                            log(
                                f"recovery: stopping all Wine processes in prefix {prefix}"
                            )
                            run([wine.with_name("wineserver"), "-k"], 15)
                            run([wine.with_name("wineserver"), "-w"], 15)
                            existing_pid = None
            if injected_successfully:
                return 1
            if runner is not None and runner.poll() is None:
                log(
                    "recovery: launched IGI is still running but its state was not confirmed; leaving it open and stopping to avoid a duplicate launch"
                )
                return 1
            if attempt < args.attempts:
                try:
                    remaining = pids()
                except RuntimeError as error:
                    log(f"recovery: {error}; Wine state could not be verified")
                    return 1
                if remaining:
                    log(
                        f"recovery: igi.exe PID(s) {remaining} remain; refusing a second launch"
                    )
                    return 1
    except (OSError, RuntimeError) as error:
        log(f"FAIL {error}")
        return 1
    log(f"FAIL attempts exhausted; report={report}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
