cmake_minimum_required(VERSION 3.12 FATAL_ERROR)

project(mash VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED TRUE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

add_executable(mash
    shell.c
    logging.c
)

add_executable(my_pause
    my_pause.cpp
)

add_executable(slow_cooker
    slow_cooker.cpp
)
