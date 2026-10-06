cmake_minimum_required(VERSION 3.22)
project(vm)

set(CMAKE_CXX_STANDARD 20)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()

add_executable(vm2 main.cpp)
# target_compile_definitions(vm2 PRIVATE DEBUG)
target_compile_options(vm2 PRIVATE -O2)

