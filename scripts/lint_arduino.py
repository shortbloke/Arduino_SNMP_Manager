"""Lint tracked working-tree files, excluding local build dependencies and secrets."""

import shutil
import subprocess
import tempfile
from pathlib import Path


def main():
    root = Path(__file__).resolve().parents[1]
    tracked = subprocess.check_output(["git", "ls-files", "-z"], cwd=root).decode().split("\0")
    with tempfile.TemporaryDirectory(prefix="snmp-arduino-lint-") as directory:
        package = Path(directory) / "Arduino_SNMP_Manager"
        package.mkdir()
        for name in filter(None, tracked):
            source = root / name
            if source.is_file():
                destination = package / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination)
        # Specification compliance permits the existing registered library name.
        subprocess.run(
            [
                "arduino-lint",
                "--compliance",
                "specification",
                "--library-manager",
                "update",
                "--project-type",
                "library",
                str(package),
            ],
            check=True,
        )


if __name__ == "__main__":
    main()
