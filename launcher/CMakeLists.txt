cmake_minimum_required(VERSION 3.21)
project(DrakengardLauncher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

# GLFW reads controllers for the Controls tab. The game uses GLFW 3.4 too, so both name and
# number controller buttons the same way.
include(FetchContent)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw)

# The controls file format and bindings are shared with the game's runtime.
set(PS2X_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../external/PS2Recomp/ps2xRuntime")

qt_add_executable(drakengard-launcher WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/SetupPage.cpp
    src/SetupPage.h
    src/ControlsTab.cpp
    src/ControlsTab.h
    src/GamepadReader.cpp
    src/GamepadReader.h
    "${PS2X_RUNTIME_DIR}/src/lib/input_bindings.cpp"
    "${PS2X_RUNTIME_DIR}/include/runtime/input_bindings.h"
)
target_include_directories(drakengard-launcher PRIVATE "${PS2X_RUNTIME_DIR}/include")
target_link_libraries(drakengard-launcher PRIVATE Qt6::Widgets glfw)

# The icon is the game's art, which git ignores, so a build works without it.
# Qt also uses the IDI_ICON1 resource for the window and taskbar.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/res/drakengard.ico")
    target_sources(drakengard-launcher PRIVATE res/drakengard.rc)
endif()

# Copy the Qt DLLs next to the exe so it starts without Qt on PATH.
add_custom_command(TARGET drakengard-launcher POST_BUILD
    COMMAND Qt6::windeployqt --no-translations --no-compiler-runtime --no-opengl-sw --no-system-d3d-compiler
            "$<TARGET_FILE:drakengard-launcher>"
    VERBATIM
)
