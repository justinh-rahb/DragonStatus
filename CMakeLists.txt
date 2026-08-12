# DragonStatus — open firmware for Panda Status-compatible ESP32-C3 controllers.
cmake_minimum_required(VERSION 3.16)

execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE DS_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT DS_VERSION)
    set(DS_VERSION "dev")
endif()
set(PROJECT_VER "${DS_VERSION}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(dragonstatus)
