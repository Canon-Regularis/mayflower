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
  src/core/profile_dp.cpp
  src/core/notouch.cpp
  src/core/weighted.cpp
  src/core/profile_dp_fast.cpp
  src/certify/blocking.cpp
  src/certify/transcripts.cpp
  src/search/exact_solver.cpp
  src/platform/bench_platform_win.cpp
  src/lattice/spectrum.cpp)
target_include_directories(mayflower_core PUBLIC include)
target_link_libraries(mayflower_core PUBLIC mf_flags)

add_executable(omega0 tools/omega0.cpp)
target_link_libraries(omega0 PRIVATE mayflower_core)

add_executable(marginals tools/marginals.cpp)
target_link_libraries(marginals PRIVATE mayflower_core)

add_executable(sample tools/sample.cpp)
target_link_libraries(sample PRIVATE mayflower_core)

add_executable(selfplay tools/selfplay.cpp)
target_link_libraries(selfplay PRIVATE mayflower_core)

add_executable(bounds tools/bounds.cpp)
target_link_libraries(bounds PRIVATE mayflower_core)

add_executable(optimal tools/optimal.cpp)
target_link_libraries(optimal PRIVATE mayflower_core)

add_executable(maxcover tools/maxcover.cpp)
target_link_libraries(maxcover PRIVATE mayflower_core)

add_test(NAME maxcover COMMAND maxcover selftest)
set_tests_properties(maxcover PROPERTIES LABELS "pr" TIMEOUT 900)

add_executable(weighted tools/weighted.cpp)
target_link_libraries(weighted PRIVATE mayflower_core)

add_executable(m9 tools/m9.cpp)
target_link_libraries(m9 PRIVATE mayflower_core)

add_test(NAME m9 COMMAND m9 selftest)
set_tests_properties(m9 PROPERTIES LABELS "fast")

add_executable(spectrum tools/spectrum.cpp)
target_link_libraries(spectrum PRIVATE mayflower_core)

add_executable(export_pool tools/export_pool.cpp)
target_link_libraries(export_pool PRIVATE mayflower_core)

add_executable(report_data tools/report_data.cpp)
target_link_libraries(report_data PRIVATE mayflower_core)

add_executable(dp_bench bench/dp_bench.cpp)
target_link_libraries(dp_bench PRIVATE mayflower_core)

enable_testing()

# tests/oracle is header-only and must never link against mayflower_core's
# algorithms.
add_executable(test_counting tests/test_counting.cpp)
target_include_directories(test_counting PRIVATE tests)
target_link_libraries(test_counting PRIVATE mayflower_core)

add_executable(test_observations tests/test_observations.cpp)
target_include_directories(test_observations PRIVATE tests)
target_link_libraries(test_observations PRIVATE mayflower_core)

add_executable(test_sampler tests/test_sampler.cpp)
target_include_directories(test_sampler PRIVATE tests)
target_link_libraries(test_sampler PRIVATE mayflower_core)

add_executable(test_outcomes tests/test_outcomes.cpp)
target_include_directories(test_outcomes PRIVATE tests)
target_link_libraries(test_outcomes PRIVATE mayflower_core)

add_executable(test_harness tests/test_harness.cpp)
target_include_directories(test_harness PRIVATE tests)
target_link_libraries(test_harness PRIVATE mayflower_core)

add_executable(test_bounds tests/test_bounds.cpp)
target_include_directories(test_bounds PRIVATE tests)
target_link_libraries(test_bounds PRIVATE mayflower_core)

add_executable(test_exact tests/test_exact.cpp)
target_include_directories(test_exact PRIVATE tests)
target_link_libraries(test_exact PRIVATE mayflower_core)

add_executable(test_weighted tests/test_weighted.cpp)
target_include_directories(test_weighted PRIVATE tests)
target_link_libraries(test_weighted PRIVATE mayflower_core)
add_test(NAME weighted COMMAND test_weighted)
set_tests_properties(weighted PROPERTIES LABELS "fast")

add_executable(test_notouch tests/test_notouch.cpp)
target_include_directories(test_notouch PRIVATE tests)
target_link_libraries(test_notouch PRIVATE mayflower_core)
add_test(NAME notouch COMMAND test_notouch)
set_tests_properties(notouch PROPERTIES LABELS "fast")

add_executable(test_ladder tests/test_ladder.cpp)
target_include_directories(test_ladder PRIVATE tests)
target_link_libraries(test_ladder PRIVATE mayflower_core)

add_executable(test_spectrum tests/test_spectrum.cpp)
target_include_directories(test_spectrum PRIVATE tests)
target_link_libraries(test_spectrum PRIVATE mayflower_core)

add_test(NAME counting COMMAND test_counting)
set_tests_properties(counting PROPERTIES LABELS "fast" TIMEOUT 300)

add_test(NAME observations COMMAND test_observations)
set_tests_properties(observations PROPERTIES LABELS "fast" TIMEOUT 300)

add_test(NAME sampler COMMAND test_sampler)
set_tests_properties(sampler PROPERTIES LABELS "fast" TIMEOUT 600)

add_test(NAME outcomes COMMAND test_outcomes)
set_tests_properties(outcomes PROPERTIES LABELS "pr" TIMEOUT 900)

add_test(NAME harness COMMAND test_harness)
set_tests_properties(harness PROPERTIES LABELS "pr" TIMEOUT 900)

add_test(NAME bounds COMMAND test_bounds)
set_tests_properties(bounds PROPERTIES LABELS "pr" TIMEOUT 1800)

add_test(NAME bounds_tool COMMAND bounds)
set_tests_properties(bounds_tool PROPERTIES LABELS "pr" TIMEOUT 1800)

add_test(NAME exact COMMAND test_exact)
set_tests_properties(exact PROPERTIES LABELS "pr" TIMEOUT 900)

add_test(NAME ladder COMMAND test_ladder)
set_tests_properties(ladder PROPERTIES LABELS "pr" TIMEOUT 900)

add_test(NAME spectrum COMMAND test_spectrum)
set_tests_properties(spectrum PROPERTIES LABELS "pr" TIMEOUT 900)

add_test(NAME spectrum_tool COMMAND spectrum)
set_tests_properties(spectrum_tool PROPERTIES LABELS "pr" TIMEOUT 900)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME bond_dimension
           COMMAND ${Python3_EXECUTABLE} bond_dimension.py --quick
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/python)
  set_tests_properties(bond_dimension PROPERTIES LABELS "fast" TIMEOUT 300)
endif()

add_test(NAME omega0 COMMAND omega0)
set_tests_properties(omega0 PROPERTIES LABELS "pr" TIMEOUT 900)

add_test(NAME marginals COMMAND marginals)
set_tests_properties(marginals PROPERTIES LABELS "pr" TIMEOUT 900)

add_test(NAME sample COMMAND sample)
set_tests_properties(sample PROPERTIES LABELS "pr" TIMEOUT 900)
