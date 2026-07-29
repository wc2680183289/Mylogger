cmake_minimum_required(VERSION 3.10)
project(Mylogger)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(logger)
add_subdirectory(example)

enable_testing()
find_package(GTest CONFIG REQUIRED)
add_subdirectory(tests)