cmake_minimum_required(VERSION 3.22)
project(MK7BlossomMacro LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(MK7BlossomMacro WIN32
    main.cpp
    clicker.cpp
    monitor.cpp
    keybinds.cpp
    settings.cpp
)
set_target_properties(MK7BlossomMacro PROPERTIES OUTPUT_NAME "Mk7Macro")

target_link_libraries(MK7BlossomMacro PRIVATE user32 gdi32 comctl32 shell32)

if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(MK7BlossomMacro PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(MK7BlossomMacro PRIVATE /O2 /Oi /Ot /GL /fp:fast /arch:AVX2 /W4 /permissive-)
    target_link_options(MK7BlossomMacro PRIVATE /LTCG)
else()
    target_compile_definitions(MK7BlossomMacro PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
