cmake_minimum_required(VERSION 3.12 FATAL_ERROR)

project(mash VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED TRUE)

add_executable(mash
    shell.c
    logging.c
)

add_executable(my_pause
    my_pause.c
)

add_executable(slow_cooker
    slow_cooker.c
)
