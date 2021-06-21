cmake_minimum_required(VERSION 3.19)
project(HW4)

set(CMAKE_CXX_STANDARD 11)

add_executable(HW4 ATAM/debugger.c OS/malloc_1.cpp OS/malloc_2.cpp OS/malloc_3.cpp)