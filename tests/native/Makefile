CXX ?= c++
CPPFLAGS += -Istubs -I../../src -Isupport
CXXFLAGS ?= -std=gnu++11 -g -O0
BUILD := build
LIB_SOURCES := $(wildcard ../../src/*.cpp)
LIB_OBJECTS := $(patsubst ../../src/%.cpp,$(BUILD)/lib/%.o,$(LIB_SOURCES))
SHARED_SOURCES := $(wildcard cases/*.cpp support/*.cpp) stubs/Arduino.cpp
TEST_OBJECTS := $(patsubst %.cpp,$(BUILD)/native/%.o,$(SHARED_SOURCES) runner.cpp)
COMPAT_OBJECTS := $(patsubst %.cpp,$(BUILD)/native/%.o,compatibility/main.cpp compatibility/other.cpp stubs/Arduino.cpp)

.PHONY: test check debug sanitize clean compatibility compatibility-run headers
.DEFAULT_GOAL := test
$(BUILD)/lib/%.o: ../../src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(BUILD)/native/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(BUILD)/libsnmp.a: $(LIB_OBJECTS)
	$(AR) rcs $@ $^
$(BUILD)/tests: $(TEST_OBJECTS) $(BUILD)/libsnmp.a
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@
test: $(BUILD)/tests
	./$(BUILD)/tests
check: test debug compatibility headers configuration
sanitize:
	$(MAKE) BUILD=build-sanitize CXXFLAGS='-std=c++11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer' test
debug:
	$(MAKE) BUILD=$(BUILD)-debug CPPFLAGS='$(CPPFLAGS) -DDEBUG -DDEBUG_BER' test
compatibility:
	$(MAKE) BUILD=$(BUILD)-compat CXXFLAGS='-std=c++11 -pedantic-errors -Wall -Wextra -Werror -fno-exceptions -fno-rtti' compatibility-run
$(BUILD)/compatibility: $(COMPAT_OBJECTS) $(BUILD)/libsnmp.a
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@
compatibility-run: $(BUILD)/compatibility
	./$(BUILD)/compatibility
headers:
	CXX="$(CXX)" python3 compatibility/check_headers.py
clean:
	rm -rf build build-*

-include $(LIB_OBJECTS:.o=.d) $(TEST_OBJECTS:.o=.d) $(COMPAT_OBJECTS:.o=.d)

.PHONY: configuration configuration-run
configuration:
	$(MAKE) BUILD=$(BUILD)-config-small CPPFLAGS='$(CPPFLAGS) -DSNMP_PACKET_LENGTH=256 -DSNMP_OCTETSTRING_MAX_LENGTH=512 -DMAX_OID_LENGTH=64 -DSNMP_MAX_PENDING_REQUESTS=2' configuration-run
	$(MAKE) BUILD=$(BUILD)-config-large CPPFLAGS='$(CPPFLAGS) -DSNMP_PACKET_LENGTH=768 -DSNMP_OCTETSTRING_MAX_LENGTH=2048 -DMAX_OID_LENGTH=192 -DSNMP_MAX_PENDING_REQUESTS=6' configuration-run
configuration-run: $(BUILD)/tests
	./$(BUILD)/tests --group Configuration
