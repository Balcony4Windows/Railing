cmake_minimum_required(VERSION 3.28)

project(Balcony LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Balcony is a Windows-only project.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerOptions)

option(BALCONY_BUILD_TESTS "Build Balcony unit tests" ON)

add_subdirectory(src)

if(BALCONY_BUILD_TESTS)
    add_subdirectory(tests)
endif()
