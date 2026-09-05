"""Split the configured example matrix without duplicating its environment list."""

import configparser
import subprocess
import sys
from pathlib import Path


def environments(group):
    config = configparser.ConfigParser(interpolation=None)
    config.read(Path(__file__).resolve().parents[1] / "tests/examples/platformio.ini")
    names = [name.strip() for name in config["platformio"]["default_envs"].split(",")]
    legacy = {
        "single_esp8266",
        "single_esp32",
        "multiple_esp8266",
        "multiple_esp32",
        "ethernet_esp8266",
    }
    if group == "legacy":
        return [name for name in names if name in legacy]
    if group not in {"query_esp8266", "query_esp32"}:
        raise ValueError("Unknown example group")
    return [name for name in names if name not in legacy and name.endswith(group[5:])]


if __name__ == "__main__":
    selected = environments(sys.argv[1])
    if not selected:
        raise SystemExit("No environments selected")
    subprocess.run(
        ["pio", "run", "-d", "tests/examples"] + [arg for name in selected for arg in ("-e", name)],
        check=True,
    )
