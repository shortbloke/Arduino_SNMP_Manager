#!/usr/bin/env python3
"""Create ignored hardware_config.h from explicit environment variables."""
import os
from pathlib import Path

destination = Path(__file__).parent / "src" / "hardware_config.h"
required = ("TEST_WIFI_SSID", "TEST_WIFI_PASSWORD")
missing = [name for name in required if not os.environ.get(name)]
if missing:
    raise SystemExit("Set " + ", ".join(missing) + " before generating the hardware config")

def value(name, default=""):
    return os.environ.get(name, default).replace("\\", "\\\\").replace('"', '\\"')

lines = [
    f'#define TEST_SSID "{value("TEST_WIFI_SSID")}"',
    f'#define TEST_PASSWORD "{value("TEST_WIFI_PASSWORD")}"',
    f'#define TEST_AGENT "{value("TEST_AGENT", value("TEST_UBUNTU_AGENT"))}"',
    f'#define TEST_COMMUNITY "{value("TEST_COMMUNITY", value("TEST_UBUNTU_COMMUNITY", "public"))}"',
    f'#define TEST_PORT {value("TEST_PORT", "161")}',
]
for target in ("WINDOWS", "UBUNTU"):
    for field, default in (("AGENT", ""), ("COMMUNITY", "public"), ("PORT", "161")):
        lines.append(f'#define TEST_{target}_{field} "{value(f"TEST_{target}_{field}", default)}"'
                     if field != "PORT" else f'#define TEST_{target}_{field} {value(f"TEST_{target}_{field}", default)}')
destination.write_text("\n".join(lines) + "\n")
print(f"Wrote ignored {destination}")
