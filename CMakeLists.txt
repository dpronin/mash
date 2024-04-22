cmake_minimum_required(VERSION 3.28 FATAL_ERROR)

project(mash VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

add_executable(mash
    logging.cpp
    shell.cpp
)

target_sources(mash PRIVATE
    FILE_SET HEADERS
        FILES
            shell.hpp
            logging.hpp
)

add_executable(my_pause
    my_pause.cpp
)

add_executable(slow_cooker
    slow_cooker.cpp
)
