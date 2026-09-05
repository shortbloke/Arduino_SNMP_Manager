CXX ?= c++
CPPFLAGS += -Istubs -I../../src
CXXFLAGS ?= -std=gnu++11 -g -O0
BUILD := build
LIB_SOURCES := $(wildcard ../../src/*.cpp) stubs/Arduino.cpp
.PHONY: test check regressions debug sanitize sanitize-regressions clean
$(BUILD)/tests: Makefile tests.cpp $(wildcard stubs/*.h) $(wildcard ../../src/*.h) $(LIB_SOURCES)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests.cpp $(LIB_SOURCES) -o $@
test: $(BUILD)/tests
	./$(BUILD)/tests
check: test regressions debug compatibility headers
regressions: $(BUILD)/tests
	./$(BUILD)/tests --regressions
sanitize:
	$(MAKE) BUILD=build-sanitize CXXFLAGS='-std=gnu++11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer' test
sanitize-regressions:
	$(MAKE) BUILD=build-sanitize CXXFLAGS='-std=gnu++11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer' regressions
clean:
	rm -rf build build-debug build-sanitize build-sanitize-debug

debug:
	$(MAKE) BUILD=$(BUILD)-debug CPPFLAGS='-Istubs -I../../src -DDEBUG -DDEBUG_BER' test

.PHONY: compatibility
$(BUILD)/compatibility: Makefile compatibility/main.cpp compatibility/other.cpp $(wildcard ../../src/*.h) $(LIB_SOURCES) $(wildcard stubs/*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) -std=c++11 -pedantic-errors -Wall -Wextra -Werror -fno-exceptions -fno-rtti compatibility/main.cpp compatibility/other.cpp $(LIB_SOURCES) -o $@
compatibility: $(BUILD)/compatibility
	./$(BUILD)/compatibility

.PHONY: headers
headers:
	CXX="$(CXX)" python3 compatibility/check_headers.py
