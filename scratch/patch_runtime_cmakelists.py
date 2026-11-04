with open('external/PS2Recomp/ps2xRuntime/CMakeLists.txt', 'r', encoding='utf-8') as f:
    text = f.read()

target1 = '''    src/lib/gs/gs_cpu_backend.cpp'''
replacement1 = '''    src/lib/gs/gs_cpu_backend.cpp
    src/lib/gs/gs_parallel_backend.cpp'''

target2 = '''target_link_libraries(ps2_runtime PUBLIC ps2_host_backend ffmpeg ps2_iop)'''
replacement2 = '''target_link_libraries(ps2_runtime PUBLIC ps2_host_backend ffmpeg ps2_iop)

if(TARGET parallel-gs)
    target_link_libraries(ps2_runtime PUBLIC parallel-gs)
    target_include_directories(ps2_runtime PUBLIC
        "${PS2X_PARALLEL_GS_DIR}/gs"
        "${PS2X_PARALLEL_GS_DIR}/Granite/vulkan"
        "${PS2X_PARALLEL_GS_DIR}/Granite/math"
        "${PS2X_PARALLEL_GS_DIR}/Granite/util"
        "${PS2X_PARALLEL_GS_DIR}/Granite/threading"
        "${PS2X_PARALLEL_GS_DIR}/Granite/filesystem"
        "${PS2X_PARALLEL_GS_DIR}/Granite/third_party/volk"
        "${PS2X_PARALLEL_GS_DIR}/Granite/third_party/vulkan-headers/include"
    )
    target_compile_definitions(ps2_runtime PUBLIC PS2X_HAS_PARALLEL_GS=1)
endif()'''

text = text.replace(target1.replace('\n', '\r\n'), replacement1.replace('\n', '\r\n'))
text = text.replace(target2.replace('\n', '\r\n'), replacement2.replace('\n', '\r\n'))

with open('external/PS2Recomp/ps2xRuntime/CMakeLists.txt', 'w', encoding='utf-8', newline='\r\n') as f:
    f.write(text)

print("ps2xRuntime CMakeLists.txt updated successfully")
