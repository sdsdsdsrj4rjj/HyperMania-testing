cmake_minimum_required(VERSION 3.7)
project(bssspecial C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2")

add_library(bssspecial SHARED
    ../src/GameAPI/C/GameAPI/Game.c
    src/dllmain.c
)

target_include_directories(bssspecial PRIVATE
    ../src/GameAPI/C
    src
)

set_target_properties(bssspecial PROPERTIES PREFIX "")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set_target_properties(bssspecial PROPERTIES SUFFIX ".dll")
endif()
