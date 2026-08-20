cmake_minimum_required(VERSION 3.24)
project(mayflower LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)   # __uint128_t

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# MinGW-w64 GCC 13.2 ships no ASan/UBSan/TSan runtime, so -fsanitize=address
# fails at link time. -fsanitize=undefined -fsanitize-trap=undefined works and
# needs no runtime library. _GLIBCXX_ASSERTIONS is on for all build types since
# the hot path uses fixed-size storage.
add_library(mf_flags INTERFACE)
target_compile_options(mf_flags INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
target_compile_definitions(mf_flags INTERFACE _GLIBCXX_ASSERTIONS)

option(MF_NATIVE "Build for the host microarchitecture (-march=native)" ON)
if(MF_NATIVE AND CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_options(mf_flags INTERFACE -march=native)
endif()

add_library(mayflower_core
  src/core/profile_dp.cpp)
target_include_directories(mayflower_core PUBLIC include)
target_link_libraries(mayflower_core PUBLIC mf_flags)

add_executable(omega0 tools/omega0.cpp)
target_link_libraries(omega0 PRIVATE mayflower_core)

enable_testing()

# tests/oracle is header-only and must never link against mayflower_core's
# algorithms.
add_executable(test_counting tests/test_counting.cpp)
target_include_directories(test_counting PRIVATE tests)
target_link_libraries(test_counting PRIVATE mayflower_core)

add_test(NAME counting COMMAND test_counting)
set_tests_properties(counting PROPERTIES LABELS "fast" TIMEOUT 300)

add_test(NAME omega0 COMMAND omega0)
set_tests_properties(omega0 PROPERTIES LABELS "pr" TIMEOUT 900)
