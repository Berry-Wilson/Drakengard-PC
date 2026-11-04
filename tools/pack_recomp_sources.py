#!/usr/bin/env python3
"""
Packs the 7,904 generated C++ files from ps2_recomp into 32 unity batch .cpp files
and converts register_functions.cpp's 115,000 assignment lines into a static table
initializer so MSVC compiles the entire recompiled executable in seconds.
"""

import re
import shutil
from pathlib import Path

PREAMBLE = """#include <stdexcept>
#include "ps2_runtime_macros.h"
#include "ps2_runtime.h"
#include <ps2_recompiled_functions.h>
#include <ps2_recompiled_stubs.h>
#include "ps2_syscalls.h"
#include "ps2_stubs.h"
#ifdef PS2_FUNCTION_LOG_TRACKER
#include "ps2_log.h"
#endif

"""


def pack_sources(in_dir: Path, out_dir: Path, num_batches: int = 32):
    out_dir.mkdir(parents=True, exist_ok=True)

    # Copy generated headers
    for hdr in ("ps2_recompiled_functions.h", "ps2_recompiled_stubs.h"):
        shutil.copy2(in_dir / hdr, out_dir / hdr)

    # Parse active function names from ps2_recompiled_functions.h so stale .cpp files are excluded
    hdr_txt = (in_dir / "ps2_recompiled_functions.h").read_text(encoding="utf-8")
    active_fns = set(re.findall(r"void\s+([A-Za-z0-9_]+)\s*\(", hdr_txt))

    cpp_files = []
    removed_stale = 0
    for p in sorted(in_dir.glob("*.cpp")):
        if p.name == "register_functions.cpp":
            continue
        if p.stem in active_fns:
            cpp_files.append(p)
        else:
            p.unlink()
            removed_stale += 1

    if removed_stale:
        print(f"Removed {removed_stale} stale .cpp files from previous run.")
    print(f"Packing {len(cpp_files)} generated C++ files into {num_batches} batch files...")

    batches = [[] for _ in range(num_batches)]
    for idx, p in enumerate(cpp_files):
        batches[idx % num_batches].append(p)

    for b_idx, files in enumerate(batches):
        batch_path = out_dir / f"recomp_batch_{b_idx:02d}.cpp"
        chunks = [PREAMBLE]
        for p in files:
            txt = p.read_text(encoding="utf-8")
            marker = txt.find("// Function:")
            if marker != -1:
                chunks.append(txt[marker:])
            else:
                chunks.append(txt)
            chunks.append("\n")
        batch_path.write_text("".join(chunks), encoding="utf-8")

    # Convert register_functions.cpp to a compact table-driven loop
    reg_in = (in_dir / "register_functions.cpp").read_text(encoding="utf-8")
    m_base = re.search(r"g_ps2RecompiledFunctionTableBase\s*=\s*(0x[0-9a-fA-Fu]+|\d+u);", reg_in)
    m_end = re.search(r"g_ps2RecompiledFunctionTableEnd\s*=\s*(0x[0-9a-fA-Fu]+|\d+u);", reg_in)
    m_cnt = re.search(r"g_ps2RecompiledFunctionTableSlotCount\s*=\s*(\d+u);", reg_in)

    base_val = m_base.group(1)
    end_val = m_end.group(1)
    cnt_val = m_cnt.group(1)

    entries = re.findall(
        r"g_ps2RecompiledFunctionTable\[(\d+)\]\s*=\s*([A-Za-z0-9_]+);",
        reg_in,
    )
    print(f"Converting {len(entries)} dispatch table entries in register_functions.cpp to static array...")

    reg_lines = [
        '#include "ps2_runtime.h"\n',
        "#include <ps2_recompiled_functions.h>\n",
        '#include "ps2_stubs.h"\n',
        "#include <ps2_recompiled_stubs.h>\n",
        '#include "ps2_syscalls.h"\n\n',
        f"extern const uint32_t g_ps2RecompiledFunctionTableBase = {base_val};\n",
        f"extern const uint32_t g_ps2RecompiledFunctionTableEnd = {end_val};\n",
        f"extern const uint32_t g_ps2RecompiledFunctionTableSlotCount = {cnt_val};\n",
        f"PS2Runtime::RecompiledFunction g_ps2RecompiledFunctionTable[{cnt_val}] = {{}};\n\n",
        "namespace {\n",
        "struct SlotBinding {\n",
        "    uint32_t slot;\n",
        "    PS2Runtime::RecompiledFunction fn;\n",
        "};\n",
        "static const SlotBinding kSlotBindings[] = {\n",
    ]
    for slot, fn in entries:
        reg_lines.append(f"    {{{slot}u, {fn}}},\n")
    reg_lines.extend([
        "};\n",
        "struct GeneratedFunctionTableInitializer {\n",
        "    GeneratedFunctionTableInitializer() {\n",
        "        for (const auto& b : kSlotBindings) {\n",
        "            g_ps2RecompiledFunctionTable[b.slot] = b.fn;\n",
        "        }\n",
        "    }\n",
        "};\n",
        "static const GeneratedFunctionTableInitializer g_generatedFunctionTableInitializer;\n",
        "} // namespace\n",
    ])
    (out_dir / "register_functions.cpp").write_text("".join(reg_lines), encoding="utf-8")
    print(f"Wrote optimized {out_dir / 'register_functions.cpp'} and {num_batches} batch files.")


if __name__ == "__main__":
    root = Path(__file__).resolve().parent.parent / "recomp"
    pack_sources(root / "output", root / "batched", num_batches=32)
