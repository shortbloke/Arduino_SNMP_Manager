CXX ?= c++
CPPFLAGS += -Istubs -I../../src
CXXFLAGS ?= -std=gnu++11 -g -O0
BUILD := build
.PHONY: test check regressions sanitize sanitize-regressions clean
$(BUILD)/tests: Makefile tests.cpp stubs/Arduino.h stubs/Udp.h $(wildcard ../../src/*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests.cpp -o $@
test: $(BUILD)/tests
	./$(BUILD)/tests
check: test regressions
regressions: $(BUILD)/tests
	./$(BUILD)/tests --regressions
sanitize:
	$(MAKE) BUILD=build-sanitize CXXFLAGS='-std=gnu++11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer' test
sanitize-regressions:
	$(MAKE) BUILD=build-sanitize CXXFLAGS='-std=gnu++11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer' regressions
clean:
	rm -rf build build-sanitize
