"""Compile the same test cases, support code and stubs as the Make runner."""

# PlatformIO/SCons injects Import and env when loading this extra script.
Import("env")  # noqa: F821
env.BuildSources(  # noqa: F821
    "$BUILD_DIR/shared_tests",
    "$PROJECT_DIR/tests/native",
    src_filter=["+<cases/*.cpp>", "+<support/*.cpp>", "+<stubs/Arduino.cpp>"],
)
