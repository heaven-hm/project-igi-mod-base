import configparser
from pathlib import Path
import sys


def log_filename(game_dir):
    config = configparser.ConfigParser(interpolation=None, strict=False)
    config.read(Path(game_dir) / "project-igi-mod-config.ini", encoding="utf-8")
    name = config.get("mod_menu", "log_file", fallback="project-igi-mod.log").strip()
    if not name or name in (".", "..") or any(c in name for c in "/\\:\0"):
        return "project-igi-mod.log"
    return name


if __name__ == "__main__":
    print(log_filename(sys.argv[1]))
