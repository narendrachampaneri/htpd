# HTPD - CMake build (Linux). Same sources and same result as the Makefile.
#
#   ./scripts/get_pdfium.sh
#   cmake -S . -B build-cmake
#   cmake --build build-cmake -j
#   ctest --test-dir build-cmake --output-on-failure
#
# PDFium somewhere else:  cmake -S . -B build-cmake -DPDFIUM_ROOT=/path/to/pdfium
cmake_minimum_required(VERSION 3.20)
project(htpd VERSION 0.1.2 DESCRIPTION "PDF to HTPD converter" LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HTPD_BUILD_TESTS "Build the tests" ON)
option(HTPD_INSTALL_PDFIUM "Install libpdfium.so together with htpd" ON)

# ---- compiler check: a clear message instead of pages of template errors ----
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
  #include <expected>
  #include <format>
  #include <string>
  int main() {
      std::expected<int, std::string> value{1};
      return std::format(\"{}\", *value).size() == 1 ? 0 : 1;
  }" HTPD_COMPILER_HAS_CXX23)
if(NOT HTPD_COMPILER_HAS_CXX23)
  message(FATAL_ERROR
    "This compiler cannot build HTPD. C++23 with <expected> and <format> is needed: "
    "g++ 13 or newer (Ubuntu 24.04 has it). Another compiler: cmake -DCMAKE_CXX_COMPILER=g++-14 ...")
endif()

# ---- PDFium -------------------------------------------------------------------
set(PDFIUM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pdfium" CACHE PATH
    "Folder that holds PDFium: include/fpdfview.h and lib/libpdfium.so")

find_path(PDFIUM_INCLUDE_DIR NAMES fpdfview.h
          HINTS "${PDFIUM_ROOT}" PATH_SUFFIXES include include/pdfium)
find_library(PDFIUM_LIBRARY NAMES pdfium
             HINTS "${PDFIUM_ROOT}" PATH_SUFFIXES lib lib64)
if(NOT PDFIUM_INCLUDE_DIR OR NOT PDFIUM_LIBRARY)
  message(FATAL_ERROR
    "PDFium was not found in '${PDFIUM_ROOT}'.\n"
    "Run ./scripts/get_pdfium.sh first, or pass -DPDFIUM_ROOT=/path/to/pdfium "
    "(the folder with include/fpdfview.h and lib/libpdfium.so).")
endif()
message(STATUS "PDFium headers: ${PDFIUM_INCLUDE_DIR}")
message(STATUS "PDFium library: ${PDFIUM_LIBRARY}")

add_library(htpd_pdfium SHARED IMPORTED)
set_target_properties(htpd_pdfium PROPERTIES
  IMPORTED_LOCATION "${PDFIUM_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${PDFIUM_INCLUDE_DIR}")

# ---- the converter library ------------------------------------------------------
add_library(htpd_core STATIC
  src/bundle_writer.cpp
  src/columns.cpp
  src/converter.cpp
  src/furniture.cpp
  src/tags.cpp
  src/headings.cpp
  src/layout.cpp
  src/links.cpp
  src/order.cpp
  src/structure.cpp
  src/styles.cpp
  src/tables.cpp
  src/ocr.cpp
  src/png.cpp
  src/figures.cpp
  src/pdf_reader_pdfium.cpp
  src/sections.cpp
  src/sha256.cpp
  src/text.cpp)
target_include_directories(htpd_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# Only src/pdf_reader_pdfium.cpp uses PDFium; the HTPD headers do not show it.
find_package(Threads REQUIRED)   # OCR runs several reader processes at once
target_link_libraries(htpd_core PRIVATE htpd_pdfium PUBLIC Threads::Threads)

set(HTPD_WARNINGS "")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(HTPD_WARNINGS -Wall -Wextra -Wpedantic -Wshadow)
endif()
target_compile_options(htpd_core PRIVATE ${HTPD_WARNINGS})

# ---- the command-line tool --------------------------------------------------------
include(GNUInstallDirs)
get_filename_component(PDFIUM_LIBRARY_FILE "${PDFIUM_LIBRARY}" REALPATH)

# Where a program looks for libpdfium.so: next to itself, then in ../lib (the
# installed layout). No absolute path is stored in the program, and the same
# search path is used before and after "install". (Otherwise CMake leaves an
# empty entry in the path of the build-tree program, and for the Linux loader
# an empty entry means "the current folder".)
function(htpd_program target)
  target_link_libraries(${target} PRIVATE htpd_core)
  target_compile_options(${target} PRIVATE ${HTPD_WARNINGS})
  set_target_properties(${target} PROPERTIES
    BUILD_WITH_INSTALL_RPATH ON
    INSTALL_RPATH "$ORIGIN;$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
  # A copy of libpdfium.so next to the program, so the build folder works as it is.
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${PDFIUM_LIBRARY_FILE}" "$<TARGET_FILE_DIR:${target}>/libpdfium.so"
    VERBATIM)
endfunction()

add_executable(htpd cli/main.cpp)
htpd_program(htpd)

install(TARGETS htpd RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if(HTPD_INSTALL_PDFIUM)
  install(FILES "${PDFIUM_LIBRARY_FILE}" DESTINATION "${CMAKE_INSTALL_LIBDIR}" RENAME libpdfium.so)
endif()

# ---- tests ------------------------------------------------------------------------
if(HTPD_BUILD_TESTS)
  enable_testing()
  add_executable(htpd_tests tests/test_main.cpp)
  htpd_program(htpd_tests)
  # Both programs copy libpdfium.so to the same place: never at the same time.
  add_dependencies(htpd_tests htpd)
  target_compile_definitions(htpd_tests PRIVATE
    HTPD_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  add_test(NAME htpd_tests COMMAND htpd_tests)
endif()
