cmake_minimum_required(VERSION 3.14)
project(tmc)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# Download GoogleTest
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Core TM compiler library
add_library(tmc_core
    src/ir.cpp
    src/codegen.cpp
    src/parser.cpp
    src/optimizer.cpp
    src/simulator.cpp
    src/hlcompiler.cpp
)
target_include_directories(tmc_core PUBLIC include)

# TMC compiler executable
add_executable(tmc src/main.cpp)
target_link_libraries(tmc PRIVATE tmc_core)

# Tests
enable_testing()
add_executable(tmc_tests
    tests/test_ir.cpp
    tests/test_codegen.cpp
    tests/test_parser.cpp
    tests/test_simulator.cpp
    tests/test_hlcompiler.cpp
    tests/test_hlcompiler_debug.cpp
    tests/test_examples.cpp
)
target_include_directories(tmc_tests PRIVATE include)
target_compile_definitions(tmc_tests PRIVATE EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples")
target_link_libraries(tmc_tests PRIVATE tmc_core GTest::gtest_main)
add_test(NAME TMCTests COMMAND tmc_tests)
