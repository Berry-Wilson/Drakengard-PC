with open('external/PS2Recomp/CMakeLists.txt', 'r', encoding='utf-8') as f:
    text = f.read()

target = '''if(PS2X_BUILD_RUNTIME)
    add_subdirectory("ps2xIOP")
    add_subdirectory("ps2xRuntime")
endif()'''

replacement = '''if(PS2X_BUILD_RUNTIME)
    add_subdirectory("ps2xIOP")
    set(PS2X_PARALLEL_GS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../parallel-gs" CACHE PATH "parallel-gs source directory")
    if(EXISTS "${PS2X_PARALLEL_GS_DIR}/CMakeLists.txt")
        set(PARALLEL_GS_STANDALONE ON CACHE BOOL "" FORCE)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "" FORCE)
        add_subdirectory("${PS2X_PARALLEL_GS_DIR}" "${CMAKE_BINARY_DIR}/parallel-gs" EXCLUDE_FROM_ALL)
    endif()
    add_subdirectory("ps2xRuntime")
endif()'''

text = text.replace(target.replace('\n', '\r\n'), replacement.replace('\n', '\r\n'))
with open('external/PS2Recomp/CMakeLists.txt', 'w', encoding='utf-8', newline='\r\n') as f:
    f.write(text)

print("top CMakeLists.txt updated successfully")
