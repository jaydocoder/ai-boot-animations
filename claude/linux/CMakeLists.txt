cmake_minimum_required(VERSION 3.16)
project(ClaudeBootAnimation VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network OpenGLWidgets)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

add_executable(ClaudeBootAnimation src/main.cpp)

target_link_libraries(ClaudeBootAnimation PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::OpenGLWidgets
    OpenGL::GL
    PkgConfig::MPV
)

target_compile_options(ClaudeBootAnimation PRIVATE -Wall -Wextra -Wno-unused-parameter)

install(TARGETS ClaudeBootAnimation RUNTIME DESTINATION bin)
