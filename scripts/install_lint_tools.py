"""Install checksum-pinned lint binaries for the CI runner or an Apple Silicon Mac."""

import argparse
import hashlib
import io
import json
import platform
import tarfile
import urllib.request
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    manifest = json.loads(Path(__file__).with_name("lint-tools.json").read_text())
    host = f"{platform.system()}-{platform.machine()}"
    args.destination.mkdir(parents=True, exist_ok=True)
    for name, tool in manifest.items():
        if host not in tool["platforms"]:
            parser.error(f"Unsupported platform {host}; install {name} {tool['version']} manually")
        asset = tool["platforms"][host]
        with urllib.request.urlopen(asset["url"], timeout=120) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != asset["sha256"]:
            raise ValueError(f"Checksum mismatch for {name}")
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
            binary = archive.extractfile(asset["member"])
            if binary is None:
                raise ValueError(f"Missing binary for {name}")
            destination = args.destination / name
            destination.write_bytes(binary.read())
            destination.chmod(0o755)
        print(f"Installed {name} {tool['version']}")


if __name__ == "__main__":
    main()
