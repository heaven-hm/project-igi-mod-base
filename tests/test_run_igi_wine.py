import contextlib
import importlib.util
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("launcher", Path(__file__).resolve().parents[1] / "tools" / "run_igi_wine.py")
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)
log_spec = importlib.util.spec_from_file_location("igi_log_config", Path(__file__).resolve().parents[1] / "tools" / "igi_log_config.py")
log_config = importlib.util.module_from_spec(log_spec)
log_spec.loader.exec_module(log_config)


class LauncherTest(unittest.TestCase):
    def test_configured_log_filename(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config = root / "project-igi-mod-config.ini"
            self.assertEqual(log_config.log_filename(root), "project-igi-mod.log")
            config.write_text("[mod_menu]\nlog_file=MyMod.log\n")
            self.assertEqual(log_config.log_filename(root), "MyMod.log")
            config.write_text("[mod_menu]\nlog_file=../bad.log\n")
            self.assertEqual(log_config.log_filename(root), "project-igi-mod.log")

    def test_existing_game_is_injected_without_launch_or_escape(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("wine", "igi.exe", "IGI-Injector.exe", "IGI1-Keypress.exe", "mod.dll"):
                (root / name).touch()
            state = {"launches": 0, "injections": 0, "keys": []}

            def run(command, **kwargs):
                if "tasklist /FI" in " ".join(command):
                    return subprocess.CompletedProcess(command, 0, '"igi.exe","321","Console"\n', "")
                if "winepath" in command:
                    return subprocess.CompletedProcess(command, 0, "Z:\\dummy\n", "")
                if "-i" in command:
                    state["injections"] += 1
                    return subprocess.CompletedProcess(command, 0, "", "")
                if "--pid" in command:
                    state["keys"].append(command[-1])
                    return subprocess.CompletedProcess(command, 0, "", "")
                raise AssertionError(command)

            args = ["5", str(root / "mod.dll"), "--game-dir", str(root), "--prefix", str(root),
                    "--wine", str(root / "wine"), "--log-dir", str(root / "logs"),
                    "--attempts", "1", "--press-menu-key", "--key", "F5"]
            with patch.object(launcher.subprocess, "Popen", side_effect=AssertionError("must not launch")), \
                 patch.object(launcher.subprocess, "run", run), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(launcher.main(args), 0)
            self.assertEqual(state["launches"], 0)
            self.assertEqual(state["injections"], 1)
            self.assertEqual(state["keys"], ["f5"])

    def test_retry_and_one_success(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("wine", "igi.exe", "IGI-Injector.exe", "IGI1-Keypress.exe", "mod.dll"):
                (root / name).touch()
            (root / "project-igi-mod-config.ini").write_text("[mod_menu]\nstart_key=F7\n")
            state = {"running": False, "launches": [], "injections": 0, "keys": [], "query_timeouts": 0}

            class Process:
                def __init__(self, command, **kwargs):
                    state["running"] = True
                    state["launches"].append(command[-1])
                    self.alive = True
                def poll(self): return None if self.alive else 0
                def terminate(self):
                    raise AssertionError("launcher must not terminate a game after a query timeout")
                def wait(self, timeout=None): return 0

            def run(command, **kwargs):
                if "tasklist /FI" in " ".join(command):
                    if state["running"] and len(state["launches"]) == 1 and state["query_timeouts"] == 0:
                        state["query_timeouts"] += 1
                        raise subprocess.TimeoutExpired(command, 15)
                    text = '"igi.exe","321","Console"\n' if state["running"] else ''
                    return subprocess.CompletedProcess(command, 0, text, "")
                if "winepath" in command:
                    return subprocess.CompletedProcess(command, 0, "Z:\\dummy\n", "")
                if "taskkill" in command:
                    state["running"] = False
                    return subprocess.CompletedProcess(command, 0, "", "")
                if "-i" in command:
                    state["injections"] += 1
                    return subprocess.CompletedProcess(command, 0, "", "")
                if "--pid" in command:
                    state["keys"].append(command[-1])
                    return subprocess.CompletedProcess(command, 0, "", "")
                raise AssertionError(command)

            args = ["5", str(root / "mod.dll"), "--game-dir", str(root), "--prefix", str(root),
                    "--wine", str(root / "wine"), "--log-dir", str(root / "logs"),
                    "--attempts", "3", "--load-wait", "0", "--press-menu-key"]
            with patch.object(launcher.subprocess, "Popen", Process), patch.object(launcher.subprocess, "run", run), \
                 patch.object(launcher.time, "sleep"), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(launcher.main(args), 0)
            self.assertEqual(state["launches"], ["level5"])
            self.assertEqual(state["query_timeouts"], 1)
            self.assertEqual(state["injections"], 1)
            self.assertEqual(state["keys"], ["escape", "f7"])


if __name__ == "__main__":
    unittest.main()
