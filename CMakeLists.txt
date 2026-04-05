cmake_minimum_required(VERSION 3.20)

project(microstructure_sim
  VERSION 0.1.0
  DESCRIPTION "Event-driven limit order book + matching engine simulator"
  LANGUAGES CXX
)

# ---------------- Options ----------------
option(MSIM_ENABLE_WERROR "Treat warnings as errors" OFF)
option(MSIM_ENABLE_SANITIZERS "Enable ASan/UBSan (Clang/GCC)" OFF)

# ---------------- Language setup ----------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------- Warnings (define BEFORE use) ----------------
function(msim_set_warnings target)
  if (MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
    if (MSIM_ENABLE_WERROR)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic
      -Wshadow -Wconversion -Wsign-conversion
      -Wnon-virtual-dtor -Wold-style-cast
      -Woverloaded-virtual -Wnull-dereference
    )
    if (MSIM_ENABLE_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

# ---------------- Sanitizers (define BEFORE use) ----------------
function(msim_enable_sanitizers target)
  if (MSIM_ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

# ---------------- Dependencies ----------------
include(FetchContent)

# GoogleTest (tests)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googletest)

# cpp-httplib (gateway)
FetchContent_Declare(
  cpp_httplib
  URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.5.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(cpp_httplib)

# Google Benchmark (benchmarks)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  benchmark
  URL https://github.com/google/benchmark/archive/refs/tags/v1.9.4.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(benchmark)

# ---------------- Library: msim ----------------
add_library(msim
  src/book.cpp
  src/matching_engine.cpp
  src/simulator.cpp
  src/order_flow.cpp
  src/rules.cpp
  src/world.cpp
  src/agents/noise_trader.cpp
  src/agents/market_maker.cpp
  src/live_world.cpp
)

target_include_directories(msim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
msim_set_warnings(msim)
msim_enable_sanitizers(msim)

# ---------------- CLI executable ----------------
add_executable(msim_cli
  src/main.cpp
)
target_link_libraries(msim_cli PRIVATE msim)
msim_set_warnings(msim_cli)
msim_enable_sanitizers(msim_cli)

# ---------------- Live gateway executable ----------------
add_executable(msim_gateway
  src/gateway_main.cpp
)
target_link_libraries(msim_gateway PRIVATE msim httplib::httplib)
msim_set_warnings(msim_gateway)
msim_enable_sanitizers(msim_gateway)

# ---------------- Unit tests ----------------
include(CTest)
enable_testing()

add_executable(msim_tests
  tests/test_types.cpp
  tests/test_book.cpp
  tests/test_matching.cpp
  tests/test_cancel_modify.cpp
  tests/test_simulator.cpp
  tests/test_rules.cpp
  tests/test_order_types.cpp
  tests/test_agents_smoke.cpp
)

target_link_libraries(msim_tests PRIVATE msim GTest::gtest_main)
msim_set_warnings(msim_tests)
msim_enable_sanitizers(msim_tests)

include(GoogleTest)
gtest_discover_tests(msim_tests)

# ---------------- Benchmarks ----------------
set(BENCH_SRC "")
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/bench_engine.cpp")
  set(BENCH_SRC "${CMAKE_CURRENT_SOURCE_DIR}/tests/bench_engine.cpp")
elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/bench_engine.cpp")
  set(BENCH_SRC "${CMAKE_CURRENT_SOURCE_DIR}/test/bench_engine.cpp")
else()
  message(FATAL_ERROR
    "Benchmark source not found. Expected either:\n"
    "  tests/bench_engine.cpp\n"
    "or\n"
    "  test/bench_engine.cpp\n"
    "Make sure the file exists AND is committed to the repo."
  )
endif()

add_executable(msim_bench
  ${BENCH_SRC}
)
target_link_libraries(msim_bench PRIVATE msim benchmark::benchmark benchmark::benchmark_main)
msim_set_warnings(msim_bench)
msim_enable_sanitizers(msim_bench)

add_executable(run_theory src/run_theory.cpp)
target_link_libraries(run_theory PRIVATE msim)
msim_set_warnings(run_theory)
msim_enable_sanitizers(run_theory)

# ── Python bindings (opt-in, does not affect existing CI) ──────────────────
#
# Windows (x64 Native Tools Command Prompt for VS 2022):
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMSIM_BUILD_PYTHON=ON -G "NMake Makefiles"
#   cmake --build build
#   setx PYTHONPATH "%CD%\src\python"
#   (close and reopen prompt)
#   python -c "import msim; print(msim.__version__)"
#
# macOS / Linux:
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMSIM_BUILD_PYTHON=ON
#   cmake --build build
#   export PYTHONPATH=$PWD/src/python
#   python -c "import msim; print(msim.__version__)"
#
# The compiled extension (_msim_core.*.pyd / .so) is written directly into
# src/python/msim/ so that `from ._msim_core import ...` in __init__.py
# resolves without any manual copying or pip install.
#
option(MSIM_BUILD_PYTHON "Build pybind11 Python bindings" OFF)

if (MSIM_BUILD_PYTHON)

  FetchContent_Declare(
    pybind11
    URL https://github.com/pybind/pybind11/archive/refs/tags/v2.13.6.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(pybind11)

  pybind11_add_module(_msim_core src/python/msim_py.cpp)
  target_link_libraries(_msim_core PRIVATE msim)

  # Write the extension directly into the Python package directory so that
  # `from ._msim_core import ...` in src/python/msim/__init__.py works
  # immediately after cmake --build, with no manual copying required.
  set_target_properties(_msim_core PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/src/python/msim"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/src/python/msim"
  )

  target_include_directories(_msim_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  )

  msim_set_warnings(_msim_core)

  message(STATUS "Python bindings: ON  (_msim_core)")
  message(STATUS "  Extension will be written to: src/python/msim/")
  message(STATUS "  After build, set PYTHONPATH to src/python and run:")
  message(STATUS "    python -c \"import msim; print(msim.__version__)\"")

endif()
