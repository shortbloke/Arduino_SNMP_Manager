#!/usr/bin/env python3
"""Build and run the read-only Ubuntu/Net-SNMP integration test."""
import argparse
import os
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--host", default=os.environ.get("UBUNTU_SNMP_HOST"))
parser.add_argument("--community", default=os.environ.get("UBUNTU_SNMP_COMMUNITY"))
parser.add_argument("--expect-name", default="-")
parser.add_argument("--expect-location", default="-")
parser.add_argument("--expect-services", default="-")
args = parser.parse_args()
if not args.host or not args.community:
    parser.error("supply --host/--community or UBUNTU_SNMP_HOST/UBUNTU_SNMP_COMMUNITY")

with tempfile.TemporaryDirectory(prefix="ubuntu-snmp-test-") as directory:
    binary = Path(directory) / "ubuntu-snmp"
    subprocess.run(
        [
            os.environ.get("CXX", "c++"), "-std=c++11", "-g",
            "-I" + str(root / "tests/native/stubs"), "-I" + str(root / "src"),
            str(root / "tests/ubuntu/main.cpp"), str(root / "tests/native/stubs/Arduino.cpp"),
            *map(str, sorted((root / "src").glob("*.cpp"))), "-o", str(binary),
        ],
        check=True,
    )
    subprocess.run(
        [str(binary), args.host, args.community, args.expect_name, args.expect_location,
         args.expect_services],
        check=True,
    )
