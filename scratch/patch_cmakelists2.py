with open('external/PS2Recomp/CMakeLists.txt', 'r', encoding='utf-8') as f:
    text = f.read()

# Normalize to \n
text = text.replace('\r\n', '\n')

target = """if(PS2X_BUILD_RUNTIME)
    add_subdirectory("ps2xIOP")
    add_subdirectory("ps2xRuntime")
endif()"""

replacement = """if(PS2X_BUILD_RUNTIME)
    add_subdirectory("ps2xIOP")
    set(PS2X_PARALLEL_GS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../parallel-gs" CACHE PATH "parallel-gs source directory")
    message(STATUS "Checking parallel-gs at: ${PS2X_PARALLEL_GS_DIR}")
    if(EXISTS "${PS2X_PARALLEL_GS_DIR}/CMakeLists.txt")
        message(STATUS "Found parallel-gs, enabling...")
        set(PARALLEL_GS_STANDALONE ON CACHE BOOL "" FORCE)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "" FORCE)
        add_subdirectory("${PS2X_PARALLEL_GS_DIR}" "${CMAKE_BINARY_DIR}/parallel-gs" EXCLUDE_FROM_ALL)
    endif()
    add_subdirectory("ps2xRuntime")
endif()"""

assert target in text, "Target not found in normalized text"
text = text.replace(target, replacement)

# Write back with \r\n
with open('external/PS2Recomp/CMakeLists.txt', 'w', encoding='utf-8', newline='\r\n') as f:
    f.write(text.replace('\n', '\r\n'))

print("top CMakeLists.txt successfully replaced!")
