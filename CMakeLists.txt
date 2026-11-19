cmake_minimum_required(VERSION 3.28)

project(UnderTheBed
  VERSION 0.0.1
  DESCRIPTION "Hardcore 2D strawberry greenhouse simulation"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SIM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(SIM_BUILD_CLI "Build the headless sim-cli runner" ON)
option(SIM_BUILD_TESTS "Build the test suite" ON)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(SimCompileOptions)
include(SimDependencies)

add_subdirectory(sim-core)
add_subdirectory(sim-data)

if(SIM_BUILD_CLI)
  add_subdirectory(sim-cli)
endif()

if(SIM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
