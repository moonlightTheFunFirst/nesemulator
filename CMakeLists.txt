cmake_minimum_required(VERSION 3.21)

project(nesemu LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(nesemu_core
    src/core/nes.c
)
target_include_directories(nesemu_core PUBLIC include)

add_executable(nesemu WIN32
    src/platform/win32/main.c
)
target_link_libraries(nesemu PRIVATE nesemu_core user32 shell32 gdi32 winmm)
target_compile_definitions(nesemu PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
if(MINGW)
    target_link_options(nesemu PRIVATE -municode)
endif()

add_executable(nesemu_core_tests
    tests/test_core.c
)
target_link_libraries(nesemu_core_tests PRIVATE nesemu_core)
