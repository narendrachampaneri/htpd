# HTPD - build with GNU make on Linux.
#
#   ./scripts/get_pdfium.sh     download PDFium once (into third_party/pdfium)
#   make                        build build/htpd
#   make test                   build and run the tests
#   make clean                  remove the build folder
#
# PDFium somewhere else:  make PDFIUM_ROOT=/path/to/pdfium
# Other compiler:         make CXX=g++-14
# CMake users: see CMakeLists.txt (same sources, same result).

CXX            ?= g++
OPT            ?= -O2
PDFIUM_ROOT    ?= third_party/pdfium
PDFIUM_INCLUDE ?= $(PDFIUM_ROOT)/include
PDFIUM_LIB     ?= $(PDFIUM_ROOT)/lib

BUILD    := build
WARNINGS := -Wall -Wextra -Wpedantic -Wshadow
CXXFLAGS += -std=c++23 $(OPT) $(WARNINGS) -Iinclude -isystem $(PDFIUM_INCLUDE) -MMD -MP
# A program looks for libpdfium.so in its own folder only. The build puts a
# copy there, so build/htpd and build/libpdfium.so can be moved together to
# any other folder. No absolute path is stored in the program.
LDFLAGS  += -L$(PDFIUM_LIB) -Wl,-rpath,'$$ORIGIN'
LDLIBS   += -lpdfium -pthread

CORE_SRC := $(wildcard src/*.cpp)
CORE_OBJ := $(CORE_SRC:src/%.cpp=$(BUILD)/obj/%.o)
CORE_LIB := $(BUILD)/libhtpd_core.a
PDFIUM_SO := $(BUILD)/libpdfium.so

.PHONY: all test clean check
all: $(BUILD)/htpd

# Clear messages for the two things that can be missing: PDFium and a C++23 compiler.
check:
	@test -f $(PDFIUM_INCLUDE)/fpdfview.h && test -f $(PDFIUM_LIB)/libpdfium.so || { \
	  echo "PDFium was not found in '$(PDFIUM_ROOT)'."; \
	  echo "Run ./scripts/get_pdfium.sh, or pass PDFIUM_ROOT=/path/to/pdfium"; \
	  echo "(the folder with include/fpdfview.h and lib/libpdfium.so)."; exit 1; }
	@printf '#include <expected>\n#include <format>\nint main() {}\n' | \
	  $(CXX) -std=c++23 -x c++ -fsyntax-only - 2>/dev/null || { \
	  echo "'$(CXX)' cannot build HTPD: C++23 with <expected> and <format> is needed."; \
	  echo "Use g++ 13 or newer (Ubuntu 24.04 has it), for example: make CXX=g++-14"; exit 1; }

$(BUILD)/obj/%.o: src/%.cpp | check
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/obj/cli_main.o: cli/main.cpp | check
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/obj/test_main.o: tests/test_main.cpp | check
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -DHTPD_TEST_DATA='"$(abspath tests/data)"' -c $< -o $@

$(CORE_LIB): $(CORE_OBJ)
	$(AR) rcs $@ $^

$(PDFIUM_SO): $(PDFIUM_LIB)/libpdfium.so | check
	@mkdir -p $(@D)
	cp $< $@

$(BUILD)/htpd: $(BUILD)/obj/cli_main.o $(CORE_LIB) $(PDFIUM_SO)
	$(CXX) $(BUILD)/obj/cli_main.o $(CORE_LIB) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD)/htpd_tests: $(BUILD)/obj/test_main.o $(CORE_LIB) $(PDFIUM_SO)
	$(CXX) $(BUILD)/obj/test_main.o $(CORE_LIB) $(LDFLAGS) $(LDLIBS) -o $@

test: $(BUILD)/htpd $(BUILD)/htpd_tests
	$(BUILD)/htpd_tests

clean:
	rm -rf $(BUILD)

-include $(wildcard $(BUILD)/obj/*.d)
