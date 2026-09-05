"""Compile the same test cases, support code and stubs as the Make runner."""
Import("env")
env.BuildSources(
    "$BUILD_DIR/shared_tests",
    "$PROJECT_DIR/tests/native",
    src_filter=["+<cases/*.cpp>", "+<support/*.cpp>", "+<stubs/Arduino.cpp>"],
)
