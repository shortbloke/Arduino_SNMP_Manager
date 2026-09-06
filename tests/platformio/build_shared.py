"""Compile the same test cases, support code and stubs as the Make runner."""

from SCons.Script import COMMAND_LINE_TARGETS

# PlatformIO/SCons injects Import and env when loading this extra script.
Import("env")  # noqa: F821
source_filter = ["+<cases/*.cpp>", "+<support/*.cpp>", "+<stubs/Arduino.cpp>"]
# Test builds get their main from test_snmp/test_main.cpp. Ordinary builds
# (including the IDE's Build All task) need the standalone runner instead.
if "__test" not in COMMAND_LINE_TARGETS and env.GetProjectOption("build_type") != "test":  # noqa: F821
    source_filter.append("+<runner.cpp>")

env.BuildSources(  # noqa: F821
    "$BUILD_DIR/shared_tests",
    "$PROJECT_DIR/tests/native",
    src_filter=source_filter,
)
