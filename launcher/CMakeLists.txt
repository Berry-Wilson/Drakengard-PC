cmake_minimum_required(VERSION 3.21)
project(DrakengardLauncher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

qt_add_executable(drakengard-launcher WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/SetupPage.cpp
    src/SetupPage.h
)
target_link_libraries(drakengard-launcher PRIVATE Qt6::Widgets)

# Copy the Qt DLLs next to the exe so it starts without Qt on PATH.
add_custom_command(TARGET drakengard-launcher POST_BUILD
    COMMAND Qt6::windeployqt --no-translations --no-compiler-runtime --no-opengl-sw --no-system-d3d-compiler
            "$<TARGET_FILE:drakengard-launcher>"
    VERBATIM
)
