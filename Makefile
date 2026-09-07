CXX ?= clang++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -pthread
CPPFLAGS ?= -Iinclude
BUILD := build
LIB_SOURCES := src/bloom_filter.cpp src/crc32.cpp src/database.cpp src/skip_list.cpp src/sstable.cpp src/wal.cpp
LIB_OBJECTS := $(LIB_SOURCES:%.cpp=$(BUILD)/%.o)

.DEFAULT_GOAL := all
.PHONY: all test benchmark sanitize clean

all: $(BUILD)/chronicle_cli $(BUILD)/chronicle_tests $(BUILD)/chronicle_example

$(BUILD)/src/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD)/chronicle_cli: $(LIB_OBJECTS) src/cli.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(BUILD)/chronicle_tests: $(LIB_OBJECTS) tests/chronicle_tests.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(BUILD)/chronicle_example: $(LIB_OBJECTS) examples/atomic_batch.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(BUILD)/chronicle_benchmark: $(LIB_OBJECTS) benchmarks/benchmark.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

test: $(BUILD)/chronicle_tests
	./$(BUILD)/chronicle_tests

benchmark: $(BUILD)/chronicle_benchmark
	./$(BUILD)/chronicle_benchmark

sanitize:
	$(MAKE) BUILD=build-sanitize CXXFLAGS="-std=c++20 -O1 -g -Wall -Wextra -Wpedantic -Werror -pthread -fsanitize=address,undefined -fno-omit-frame-pointer" test

clean:
	rm -rf build build-sanitize
