cmake_minimum_required(VERSION 3.21.1 FATAL_ERROR)
project(esplink LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" "${CMAKE_CURRENT_BINARY_DIR}")
include(cmake/Conan.cmake)
include(CCache)
include(Sanitizers)
include(CompilerWarning)
include(StaticAnalyzers)
include(StandardProjectSetting)

run_conan()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Boost REQUIRED COMPONENTS program_options)
find_package(range-v3 REQUIRED)
find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)

add_library(project_options INTERFACE)
enable_sanitizers(project_options)

add_library(project_warnings INTERFACE)
set_project_warnings(project_warnings)

add_subdirectory(src)
