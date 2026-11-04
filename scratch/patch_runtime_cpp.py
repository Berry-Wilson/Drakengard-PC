with open('external/PS2Recomp/ps2xRuntime/src/lib/ps2_runtime.cpp', 'r', encoding='utf-8') as f:
    text = f.read()

target1 = '''#include "runtime/gs/gs_frontend.h"'''
replacement1 = '''#include "runtime/gs/gs_frontend.h"
#if defined(PS2X_HAS_PARALLEL_GS)
#include "runtime/gs/gs_parallel_backend.h"
#endif'''

target2 = '''    m_gs.init(gsVram, static_cast<uint32_t>(PS2_GS_VRAM_SIZE), &m_memory.gs());'''
replacement2 = '''    m_gs.init(gsVram, static_cast<uint32_t>(PS2_GS_VRAM_SIZE), &m_memory.gs());
#if defined(PS2X_HAS_PARALLEL_GS)
    {
        const char *gsBackendEnv = std::getenv("PS2X_GS_BACKEND");
        bool useParallel = true;
        if (gsBackendEnv && (_stricmp(gsBackendEnv, "cpu") == 0 || _stricmp(gsBackendEnv, "software") == 0))
        {
            useParallel = false;
        }
        if (useParallel)
        {
            m_gs.setRasterBackend(std::make_unique<GSParallelBackend>());
        }
    }
#endif'''

text = text.replace(target1.replace('\n', '\r\n'), replacement1.replace('\n', '\r\n'))
text = text.replace(target2.replace('\n', '\r\n'), replacement2.replace('\n', '\r\n'))

with open('external/PS2Recomp/ps2xRuntime/src/lib/ps2_runtime.cpp', 'w', encoding='utf-8', newline='\r\n') as f:
    f.write(text)

print("ps2_runtime.cpp patched successfully")
