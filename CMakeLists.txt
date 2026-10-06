cmake_minimum_required(VERSION 3.22)
project(cache_analyzer)

set(CMAKE_CXX_STANDARD 20)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()

add_executable(cache_analyzer main.cpp)

target_compile_options(cache_analyzer PRIVATE -O2)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(cache_analyzer PRIVATE DEBUG)
endif ()
