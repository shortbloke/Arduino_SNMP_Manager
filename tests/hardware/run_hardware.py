#!/usr/bin/env python3
"""Upload a hardware-test profile, capture its serial output, and validate it."""
import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

from check_log import check

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--environment", required=True)
parser.add_argument("--port", required=True)
parser.add_argument("--timeout", type=float, default=180)
args = parser.parse_args()

pio = os.environ.get("PLATFORMIO_CMD") or shutil.which("pio")
if not pio:
    candidate = Path.home() / ".platformio" / "penv" / "bin" / "pio"
    if candidate.is_file():
        pio = str(candidate)
if not pio:
    parser.exit(2, "PlatformIO Core was not found; set PLATFORMIO_CMD to its pio executable.\n")

try:
    import serial
except ImportError:
    platformio_python = Path.home() / ".platformio" / "penv" / "bin" / "python"
    if platformio_python.is_file() and Path(sys.executable).resolve() != platformio_python.resolve():
        os.execv(str(platformio_python),
                 [str(platformio_python), str(Path(__file__).resolve()), *sys.argv[1:]])
    parser.exit(2, "pyserial is required; install it or use PlatformIO's Python environment.\n")

subprocess.run(
    [pio, "run", "-d", str(root / "tests/hardware"), "-e", args.environment,
     "-t", "upload", "--upload-port", args.port],
    check=True,
)

lines = []
started = time.monotonic()
with serial.Serial(args.port, 115200, timeout=0.25) as device:
    # Opening the serial port often resets ESP8266 boards. Ensure a clean run when it does not.
    device.dtr = False
    time.sleep(0.05)
    device.dtr = True
    print("Waiting for ESP8266 startup and test output...", flush=True)
    reported_wait = 0
    while time.monotonic() - started < args.timeout:
        line = device.readline().decode("utf-8", errors="replace").rstrip("\r\n")
        if not line:
            elapsed = int(time.monotonic() - started)
            if elapsed >= reported_wait + 10:
                reported_wait = elapsed
                print(f"Still waiting for serial output ({elapsed}s/{args.timeout:g}s)...", flush=True)
            continue
        lines.append(line)
        if line.startswith(("START ", "WIFI ", "ERRORCHECK ", "RESULT ", "DONE ", "FAIL ")) or "wdt reset" in line or "Exception (" in line:
            print(line, flush=True)
        if line.startswith("DONE "):
            break
        if "FAIL" in line or "wdt reset" in line or "Exception (" in line:
            break
    else:
        parser.exit(1, f"Hardware test did not finish within {args.timeout:g} seconds.\n")

try:
    print(check("\n".join(lines)))
except ValueError as error:
    parser.exit(1, f"Hardware test failed validation: {error}\n")
