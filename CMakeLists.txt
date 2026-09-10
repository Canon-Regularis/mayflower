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

find_package(Threads REQUIRED)

# The interpreters the test suite shells out to, resolved once and by name
# rather than assumed. "python" does not exist on a Debian image, which is how
# four tests came to fail everywhere except the machine they were written on.
# Node is discovered the same way and its path is handed to the test, since
# distributions disagree about whether the binary is node or nodejs.
find_package(Python3 COMPONENTS Interpreter)
find_program(MF_NODE NAMES node nodejs)
if(NOT Python3_Interpreter_FOUND)
  message(STATUS "Python 3 not found: the Python tests will not be registered")
endif()
if(NOT MF_NODE)
  message(STATUS "Node not found: the JavaScript engine test will not be registered")
endif()

add_library(mayflower_core
  src/core/profile_dp.cpp
  src/core/constraints.cpp
  src/core/flows.cpp
  src/core/outcomes.cpp
  src/core/sampler.cpp
  src/core/notouch.cpp
  src/core/weighted.cpp
  src/core/profile_dp_blocked.cpp
  src/core/profile_dp_fast.cpp
  src/certify/blocking.cpp
  src/certify/transcripts.cpp
  src/search/exact_solver.cpp
  src/search/policy_expectation.cpp
  src/platform/bench_platform_win.cpp
  src/lattice/spectrum.cpp)
target_include_directories(mayflower_core PUBLIC include)

# Every header must compile on its own. folds.hpp used std::invalid_argument
# without including <stdexcept> and built anyway, because every translation unit
# that reached it had already pulled the header in through something else. One
# generated source per header, compiled as part of the normal build, so a header
# that only works second fails here rather than in whoever includes it first.
file(GLOB MF_PUBLIC_HEADERS CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/include/mayflower/*.hpp)
set(MF_HEADER_TUS "")
foreach(hdr IN LISTS MF_PUBLIC_HEADERS)
  get_filename_component(hdr_name ${hdr} NAME_WE)
  set(gen ${CMAKE_CURRENT_BINARY_DIR}/header_selftest/${hdr_name}.cpp)
  file(WRITE ${gen} "#include \"mayflower/${hdr_name}.hpp\"\n")
  list(APPEND MF_HEADER_TUS ${gen})
endforeach()
# The sweeps' shared vocabulary lives in src/core/detail and is included by five
# translation units that order their includes differently, so the same argument
# applies to it: a detail header that only compiles after something else has to
# fail here rather than in whichever sweep happens to include it first.
file(GLOB MF_DETAIL_HEADERS CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/src/core/detail/*.hpp)
foreach(hdr IN LISTS MF_DETAIL_HEADERS)
  get_filename_component(hdr_name ${hdr} NAME_WE)
  set(gen ${CMAKE_CURRENT_BINARY_DIR}/header_selftest/detail_${hdr_name}.cpp)
  file(WRITE ${gen} "#include \"detail/${hdr_name}.hpp\"\n")
  list(APPEND MF_HEADER_TUS ${gen})
endforeach()

add_library(mayflower_headers OBJECT ${MF_HEADER_TUS})
target_link_libraries(mayflower_headers PRIVATE mf_flags)
target_include_directories(mayflower_headers PRIVATE include src/core)

# The oracle is the independent check the differential suite rests on: eight
# tests compare a sweep against it, and the comparison is only evidence while it
# shares no code with the engine. That was stated in a comment at the top of
# tests/oracle/brute_force.hpp and nothing enforced it, while the test targets
# themselves carry both tests/ and include/ on the search path.
#
# Compiled here with only tests/ on the include path, so an include of mayflower/
# added to the oracle fails the build. This uses the same mechanism as the header
# self test above and costs one translation unit.
set(MF_ORACLE_TU ${CMAKE_CURRENT_BINARY_DIR}/oracle_selftest/brute_force.cpp)
file(WRITE ${MF_ORACLE_TU} "#include \"oracle/brute_force.hpp\"
")
add_library(mayflower_oracle_isolated OBJECT ${MF_ORACLE_TU})
target_link_libraries(mayflower_oracle_isolated PRIVATE mf_flags)
target_include_directories(mayflower_oracle_isolated PRIVATE tests)
target_link_libraries(mayflower_core PUBLIC mf_flags Threads::Threads)

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

add_executable(opponent tools/opponent.cpp)
target_link_libraries(opponent PRIVATE mayflower_core)
# opponent.cpp reimplements the exact searcher's board enumeration to study a
# belief policy, so it shares the searcher's internal outcome encoding rather
# than carrying a second copy of it.
target_include_directories(opponent PRIVATE src)

# One declaration per test.
#
# Each test was previously two statements: add_test, then set_tests_properties
# giving the label and any other properties. That is 42 pairs, and the second
# statement repeated the test name, so the two could disagree. Six combinations
# of properties occurred across the 42; this function covers all of them.
#
# SKIPPABLE rather than SKIP_RETURN_CODE 77. The value was 77 in every case, and
# it is the ctest convention for "the input this test reads is absent". Naming
# the condition removes eleven copies of the number.
function(mf_test name)
  cmake_parse_arguments(T "SKIPPABLE" "LABEL;TIMEOUT;WORKDIR" "COMMAND" ${ARGN})
  if(NOT T_LABEL)
    message(FATAL_ERROR "mf_test(${name}) has no LABEL; ctest -L would never select it")
  endif()
  add_test(NAME ${name} COMMAND ${T_COMMAND})
  set(props LABELS "${T_LABEL}")
  if(T_TIMEOUT)
    list(APPEND props TIMEOUT ${T_TIMEOUT})
  endif()
  if(T_SKIPPABLE)
    list(APPEND props SKIP_RETURN_CODE 77)
  endif()
  if(T_WORKDIR)
    list(APPEND props WORKING_DIRECTORY ${T_WORKDIR})
  endif()
  set_tests_properties(${name} PROPERTIES ${props})
endfunction()

mf_test(opponent COMMAND opponent selftest LABEL fast)

add_executable(maxcover tools/maxcover.cpp)
target_link_libraries(maxcover PRIVATE mayflower_core)

mf_test(maxcover COMMAND maxcover selftest LABEL pr TIMEOUT 900)

add_executable(weighted tools/weighted.cpp)
target_link_libraries(weighted PRIVATE mayflower_core)

# One file per mathematical extension. tools/m9.cpp is the dispatcher; the
# eight experiments and the enumerator they share live under tools/m9.
add_executable(m9
  tools/m9.cpp
  tools/m9/adaptivity.cpp
  tools/m9/adversary.cpp
  tools/m9/bimaru.cpp
  tools/m9/density.cpp
  tools/m9/noisy.cpp
  tools/m9/salvo.cpp
  tools/m9/selftest.cpp)
target_link_libraries(m9 PRIVATE mayflower_core)
target_include_directories(m9 PRIVATE tools)

mf_test(m9 COMMAND m9 selftest LABEL pr TIMEOUT 900)

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

add_executable(test_uniformity tests/test_uniformity.cpp)
target_link_libraries(test_uniformity PRIVATE mayflower_core)
mf_test(uniformity COMMAND test_uniformity 300000 LABEL nightly TIMEOUT 3600)

add_executable(test_parallel tests/test_parallel.cpp)
target_link_libraries(test_parallel PRIVATE mayflower_core)
mf_test(parallel COMMAND test_parallel LABEL pr TIMEOUT 900)

# Degenerate fleets and shapes. Four of the five sweeps were wrong on a fleet of
# length-1 ships, which the ladder never used and both oracles always handled.
add_executable(test_degenerate tests/test_degenerate.cpp)
target_include_directories(test_degenerate PRIVATE tests)
target_link_libraries(test_degenerate PRIVATE mayflower_core)
mf_test(degenerate COMMAND test_degenerate LABEL fast)

add_executable(test_gated tests/test_gated.cpp)
target_include_directories(test_gated PRIVATE tests)
target_link_libraries(test_gated PRIVATE mayflower_core)
mf_test(gated COMMAND test_gated LABEL fast)

add_executable(test_folds tests/test_folds.cpp)
target_link_libraries(test_folds PRIVATE mayflower_core)
mf_test(folds COMMAND test_folds LABEL fast)

# Random instances against literal enumeration. The hand-written case lists all
# used ships of length two or more for a long time, which is the one input where
# a sweep's two orientation branches cannot disagree, so a rung that emitted a
# length-1 ship twice passed every one of them. Seeds are fixed, so this is a
# generated case list rather than a search: a failure replays exactly.
add_executable(test_fuzz tests/test_fuzz.cpp)
target_include_directories(test_fuzz PRIVATE tests)
target_link_libraries(test_fuzz PRIVATE mayflower_core)
mf_test(fuzz COMMAND test_fuzz 250 0x5EED LABEL fast)

# A wider draw on a different seed, for the slower gate.
mf_test(fuzz_wide COMMAND test_fuzz 1200 0xA17E LABEL pr TIMEOUT 900)

# Every number in docs/BENCHMARKS.md assumes the pin took. SetThreadAffinityMask
# reporting success is not the same as the thread having moved, so this asks the
# OS which processor the thread is on rather than trusting the return code.
add_executable(test_platform tests/test_platform.cpp)
target_link_libraries(test_platform PRIVATE mayflower_core)
mf_test(platform COMMAND test_platform LABEL fast SKIPPABLE)

if(Python3_Interpreter_FOUND)
mf_test(stats
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/python/stats.py --quick
  LABEL fast)

# The belief frames the scrubber replays have no other reader, so their
# invariants are checked here rather than by the thing that draws them.
mf_test(report_data
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_report_data.py
  LABEL fast
  SKIPPABLE)

# Two tools computing one quantity have to agree, and the collector is where
# that is checked. It also fails if a tool changes a table this parses.
mf_test(results
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/collect_results.py --check
  LABEL fast
  SKIPPABLE)

# The live widget scans web/pool.bin for its opening posterior and three readers
# took its contents on trust. Decoded here with the formula export_pool
# documents, so the two disagreeing is a failure rather than a wrong heatmap.
mf_test(pool
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pool.py
  LABEL fast
  SKIPPABLE)

# The refusal that protects the pre-registered TEST fold. selfplay was not a
# registered test at all, so the only guard on the sealed data was unchecked.
mf_test(selfplay_fold
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_selfplay_fold.py
  LABEL fast
  SKIPPABLE
  WORKDIR ${CMAKE_CURRENT_SOURCE_DIR})

# The other half of that guard: the script that mints the token. run_headline.py
# decides whether TEST may be read, and nothing executed it, so both of its gates
# and the parser that turns a run into the recorded result were unchecked. The
# test replaces selfplay, because the real path reads TEST and writes the log.
mf_test(run_headline
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_run_headline.py
  LABEL fast
  SKIPPABLE
  WORKDIR ${CMAKE_CURRENT_SOURCE_DIR})

# The dossier renderer, six hundred lines that nothing executed. It concatenates
# strings, so it cannot fail on a page that came out wrong: the page is checked
# against the collected results instead of against fixed text.
mf_test(render_results
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_render_results.py
  LABEL fast
  SKIPPABLE
  WORKDIR ${CMAKE_CURRENT_SOURCE_DIR})

# The live widget's exact regime, and a game played through it. It has two
# regimes and every other probe hands it the whole pool, so the branch that runs
# the exact sweep was executed by nothing.
#
# Nightly rather than per-pull-request. The work is a string of exact sweeps over
# the 10x10 lattice: about 130 s on an idle machine, and 877 s on a busy one,
# where it overran its own subprocess clock and turned red on time rather than on
# anything it measured. The widget is deterministic, so nothing but the clock can
# vary, and a gate that a loaded runner can fail is worse than a slower one.
mf_test(live_exact
  COMMAND ${CMAKE_COMMAND} -E env MF_NODE=${MF_NODE} ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_live_exact.py
  LABEL nightly
  TIMEOUT 3600
  SKIPPABLE
  WORKDIR ${CMAKE_CURRENT_SOURCE_DIR})

# The tools are too slow to run in a suite; their entry points are not. atoi
# turns a bad argument into 0, and each tool then divides by that count or writes
# an artefact from it. export_pool emptied web/pool.bin and exited 0.
mf_test(tool_cli
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tool_cli.py
  LABEL fast
  SKIPPABLE)

# The same three tools, run for real and read. Each builds the unranking sampler
# for the standard instance first, about twenty-five seconds whatever size is
# asked for, so this sits in the slower gate.
mf_test(tools_output
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tools_output.py
  LABEL pr
  TIMEOUT 900
  SKIPPABLE)

# The order-aware reference the browser engine is validated against. It printed
# its ladder and returned success whatever the numbers were, so a drifted oracle
# would have blessed a wrong engine.
mf_test(oracle
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/python/oracle.py --order-dependence
  LABEL fast)
endif()

# The JavaScript engine is a third implementation of the sweep, and the report's
# live widget is the only thing that runs it. Checked against the Python oracle,
# which shares no code with either it or the C++.
if(Python3_Interpreter_FOUND AND MF_NODE)
mf_test(engine_js
  COMMAND ${CMAKE_COMMAND} -E env MF_NODE=${MF_NODE} ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_engine_js.py
  LABEL fast)

# The belief scrubber is shipped inside the report and nothing ran it. Driven
# against a minimal DOM and a clock the test advances by hand, so a three-second
# period costs no wall time. Needs the recorded frames, so it skips without them.
mf_test(scrubber_js
  COMMAND ${CMAKE_COMMAND} -E env MF_NODE=${MF_NODE} ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_scrubber_js.py
  LABEL fast
  SKIPPABLE)

# The live widget runs the sweep in the page and nothing ran it either. Its
# playback chained timeouts without keeping the handle, so a pause left one
# scheduled and resuming inside the period ran two chains at once.
mf_test(live_js
  COMMAND ${CMAKE_COMMAND} -E env MF_NODE=${MF_NODE} ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_live_js.py
  LABEL fast
  SKIPPABLE)
endif()

add_executable(test_weighted tests/test_weighted.cpp)
target_include_directories(test_weighted PRIVATE tests)
target_link_libraries(test_weighted PRIVATE mayflower_core)
mf_test(weighted COMMAND test_weighted LABEL fast)

add_executable(test_notouch tests/test_notouch.cpp)
target_include_directories(test_notouch PRIVATE tests)
target_link_libraries(test_notouch PRIVATE mayflower_core)
mf_test(notouch COMMAND test_notouch LABEL fast)

add_executable(test_ladder tests/test_ladder.cpp)
target_include_directories(test_ladder PRIVATE tests)
target_link_libraries(test_ladder PRIVATE mayflower_core)

add_executable(test_spectrum tests/test_spectrum.cpp)
target_include_directories(test_spectrum PRIVATE tests)
target_link_libraries(test_spectrum PRIVATE mayflower_core)

mf_test(counting COMMAND test_counting LABEL fast TIMEOUT 300)

mf_test(observations COMMAND test_observations LABEL fast TIMEOUT 300)

mf_test(sampler COMMAND test_sampler LABEL fast TIMEOUT 600)

mf_test(outcomes COMMAND test_outcomes LABEL pr TIMEOUT 900)

mf_test(harness COMMAND test_harness LABEL pr TIMEOUT 900)

mf_test(bounds COMMAND test_bounds LABEL pr TIMEOUT 1800)

mf_test(bounds_tool COMMAND bounds LABEL pr TIMEOUT 1800)

mf_test(exact COMMAND test_exact LABEL pr TIMEOUT 900)

# Labelled fast rather than pr. This is the only test asserting that V0, V1, V2
# and V3 return bit identical counts, which is the property a change to any sweep
# breaks first. The only other fast test covering the sweeps broadly is test_fuzz,
# which uses boards of side five or less. This test takes 6.5 s.
mf_test(ladder COMMAND test_ladder LABEL fast TIMEOUT 900)

mf_test(spectrum COMMAND test_spectrum LABEL pr TIMEOUT 900)

mf_test(spectrum_tool COMMAND spectrum LABEL pr TIMEOUT 900)

if(Python3_Interpreter_FOUND)
  mf_test(bond_dimension
    COMMAND ${Python3_EXECUTABLE} bond_dimension.py --quick
    LABEL fast
    TIMEOUT 300
    WORKDIR ${CMAKE_SOURCE_DIR}/python)
endif()

mf_test(omega0 COMMAND omega0 LABEL pr TIMEOUT 900)

mf_test(marginals COMMAND marginals LABEL pr TIMEOUT 900)

mf_test(sample COMMAND sample LABEL pr TIMEOUT 900)
