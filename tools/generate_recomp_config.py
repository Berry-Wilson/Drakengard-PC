#!/usr/bin/env python3
"""
Generates PS2Recomp's function map CSV (functions.csv) and unified config TOML (drakengard.toml)
by combining:
  1. .eh_frame FDEs (3,796 exact compiler-emitted function start/end ranges)
  2. .gnu.linkonce.t.* sections (1,346 named C++ inline/virtual methods with exact ranges)
  3. _vt$* C++ vtable function pointers, .ctors, .dtors, and entry point (0x120008)
  4. JAL call targets across executable sections
  5. ps2_analyzer's 708 SCE SDK symbol matches, stubs, entry_points, MMIO, and 118 jump tables
"""

import re
import struct
from pathlib import Path


def sanitize_ident(name: str) -> str:
    for p in (".gnu.linkonce.t.", ".gnu.linkonce.d.", ".gnu.linkonce.r."):
        if name.startswith(p):
            name = name[len(p) :]
            break
    clean = re.sub(r"[^A-Za-z0-9_]", "_", name)
    if not clean or clean[0].isdigit():
        clean = f"fn_{clean}"
    while clean.startswith("__"):
        clean = "cxx_" + clean[2:]
    return clean[:56]


def generate_function_map(elf_path: Path, csv_out: Path, toml_out: Path, analyzer_toml: Path):
    data = elf_path.read_bytes()
    (
        e_type,
        e_machine,
        e_version,
        e_entry,
        e_phoff,
        e_shoff,
        e_flags,
        e_ehsize,
        e_phentsize,
        e_phnum,
        e_shentsize,
        e_shnum,
        e_shstrndx,
    ) = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)

    shstr_hdr = struct.unpack_from("<10I", data, e_shoff + e_shstrndx * e_shentsize)
    strtab = data[shstr_hdr[4] : shstr_hdr[4] + shstr_hdr[5]]

    def get_sh_name(off: int) -> str:
        end = strtab.find(b"\x00", off)
        return strtab[off:end].decode("ascii", errors="replace")

    exec_sections = []
    vtable_sections = []
    ctor_dtor_sections = []
    eh_frame_sec = None

    # Map start_addr -> [name, end_addr, is_authoritative]
    functions = {}

    for i in range(e_shnum):
        s_name, s_type, s_flags, s_addr, s_offset, s_size, s_link, s_info, s_align, s_entsize = struct.unpack_from(
            "<10I", data, e_shoff + i * e_shentsize
        )
        name = get_sh_name(s_name)
        if name == ".eh_frame":
            eh_frame_sec = (s_addr, s_offset, s_size)
        elif name in (".ctors", ".dtors"):
            ctor_dtor_sections.append((name, s_addr, s_offset, s_size))
        elif name.startswith(".gnu.linkonce.d._vt$"):
            vtable_sections.append((name, s_addr, s_offset, s_size))

        # Check executable sections (excluding .vutext and .DVP.overlay)
        if (s_flags & 0x4) and s_size > 0 and not name.startswith(".vutext") and not name.startswith(".DVP.overlay"):
            exec_sections.append((name, s_addr, s_offset, s_size))
            if name.startswith(".gnu.linkonce.t."):
                fn_name = sanitize_ident(name)
                functions[s_addr] = [fn_name, s_addr + s_size, True]

    def find_exec_section(addr: int):
        for s_name, s_addr, s_off, s_size in exec_sections:
            if s_addr <= addr < s_addr + s_size:
                return (s_name, s_addr, s_off, s_size)
        return None

    # 1. Extract .eh_frame FDEs
    eh_count = 0
    if eh_frame_sec:
        _, eh_off, eh_size = eh_frame_sec
        eh = data[eh_off : eh_off + eh_size]
        pos = 0
        while pos < len(eh):
            length = struct.unpack_from("<I", eh, pos)[0]
            if length == 0:
                pos += 4
                continue
            id_val = struct.unpack_from("<I", eh, pos + 4)[0]
            if id_val != 0:
                pc_begin, pc_range = struct.unpack_from("<II", eh, pos + 8)
                if pc_begin > 0 and pc_range > 0 and find_exec_section(pc_begin):
                    if pc_begin not in functions:
                        functions[pc_begin] = [f"func_{pc_begin:08X}", pc_begin + pc_range, True]
                    else:
                        functions[pc_begin][1] = max(functions[pc_begin][1], pc_begin + pc_range)
                        functions[pc_begin][2] = True
                    eh_count += 1
            pos += 4 + length

    # 2. Entry point
    if e_entry not in functions and find_exec_section(e_entry):
        functions[e_entry] = ["_start", 0, False]

    # 3. Extract .ctors / .dtors function pointers
    ctor_count = 0
    for s_name, s_addr, s_off, s_size in ctor_dtor_sections:
        for off in range(0, s_size, 4):
            ptr = struct.unpack_from("<I", data, s_off + off)[0]
            if ptr not in (0, 0xFFFFFFFF) and (ptr & 3) == 0 and find_exec_section(ptr):
                if ptr not in functions:
                    tag = "ctor" if s_name == ".ctors" else "dtor"
                    functions[ptr] = [f"{tag}_{ptr:08X}", 0, False]
                    ctor_count += 1

    # 4. Extract C++ vtable (_vt$*) function pointers
    vt_count = 0
    for s_name, s_addr, s_off, s_size in vtable_sections:
        for off in range(0, s_size, 4):
            ptr = struct.unpack_from("<I", data, s_off + off)[0]
            if (ptr & 3) == 0 and find_exec_section(ptr):
                if ptr not in functions:
                    functions[ptr] = [f"vfunc_{ptr:08X}", 0, False]
                    vt_count += 1

    # 5. Scan JAL instructions across all executable sections
    jal_count = 0
    for s_name, s_addr, s_off, s_size in exec_sections:
        for off in range(0, s_size, 4):
            pc = s_addr + off
            w = struct.unpack_from("<I", data, s_off + off)[0]
            op = (w >> 26) & 0x3F
            if op == 3:  # JAL
                target = ((pc + 4) & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
                if find_exec_section(target) and target not in functions:
                    functions[target] = [f"func_{target:08X}", 0, False]
                    jal_count += 1

    # 6. Merge SCE SDK symbols and all discovered function addresses from ps2_analyzer's TOML
    sce_matched = 0
    analyzer_extra = 0
    if analyzer_toml.exists():
        txt = analyzer_toml.read_text(encoding="utf-8", errors="replace")
        for m in re.finditer(r'"([A-Za-z0-9_]+)@(0x[0-9A-Fa-f]+)"', txt):
            sym_name, addr_str = m.group(1), m.group(2)
            addr = int(addr_str, 16)
            if addr in functions:
                if functions[addr][0].startswith(("func_", "vfunc_", "sub_", "ctor_", "dtor_")):
                    functions[addr][0] = sym_name
                    sce_matched += 1
            elif find_exec_section(addr):
                functions[addr] = [sym_name, 0, False]
                sce_matched += 1
        for m in re.finditer(r'\bsub_([0-9A-Fa-f]{8})\b', txt):
            addr = int(m.group(1), 16)
            if addr not in functions and find_exec_section(addr):
                functions[addr] = [f"func_{addr:08X}", 0, False]
                analyzer_extra += 1

    # 7. Scan data sections (.data, .rodata, .sdata) for 32-bit function pointer tables/clusters
    data_ptr_count = 0
    for i in range(e_shnum):
        s_name, s_type, s_flags, s_addr, s_offset, s_size, s_link, s_info, s_align, s_entsize = struct.unpack_from(
            "<10I", data, e_shoff + i * e_shentsize
        )
        name = get_sh_name(s_name)
        if name in (".data", ".rodata", ".sdata") and s_size >= 4:
            for off in range(0, s_size - 3, 4):
                ptr = struct.unpack_from("<I", data, s_offset + off)[0]
                if (ptr & 3) == 0 and ptr not in functions and find_exec_section(ptr):
                    # Check if target looks like a function prologue (addiu sp, sp, -N or sd/sw ra, N(sp) or jr ra)
                    sec = find_exec_section(ptr)
                    toff = sec[2] + (ptr - sec[1])
                    if toff + 8 <= len(data):
                        w0, w1 = struct.unpack_from("<II", data, toff)
                        op0 = (w0 >> 26) & 0x3F
                        rs0 = (w0 >> 21) & 0x1F
                        rt0 = (w0 >> 16) & 0x1F
                        imm0 = w0 & 0xFFFF
                        is_prologue = (
                            (op0 in (8, 9, 24, 25) and rs0 == 29 and rt0 == 29 and (imm0 & 0x8000))
                            or (w0 == 0x03E00008)  # jr ra (leaf thunk)
                        )
                        if is_prologue:
                            functions[ptr] = [f"func_{ptr:08X}", 0, False]
                            data_ptr_count += 1

    # 8. Recover leaf / callback functions in unmapped gaps across all executable sections
    gap_func_count = 0
    prelim_starts = sorted(functions.keys())
    for idx, start in enumerate(prelim_starts):
        name, end_addr, is_auth = functions[start]
        sec = find_exec_section(start)
        sec_end = sec[1] + sec[3]
        next_start = prelim_starts[idx + 1] if idx + 1 < len(prelim_starts) else sec_end
        gs = end_addr if (is_auth and end_addr > start) else start
        ge = min(sec_end, next_start)
        while gs < ge:
            toff = sec[2] + (gs - sec[1])
            if struct.unpack_from("<I", data, toff)[0] != 0:
                break
            gs += 4
        if gs >= ge:
            continue

        cur = gs
        max_br = cur
        pc = gs
        while pc < ge:
            toff = sec[2] + (pc - sec[1])
            w = struct.unpack_from("<I", data, toff)[0]
            op = (w >> 26) & 0x3F
            if op in (1, 4, 5, 6, 7, 20, 21, 22, 23):
                simm = w & 0xFFFF
                if simm & 0x8000:
                    simm -= 0x10000
                tgt = pc + 4 + (simm << 2)
                if cur <= tgt < ge and tgt > max_br:
                    max_br = tgt
            elif op == 2:  # J
                tgt = ((pc + 4) & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
                if cur <= tgt < ge and tgt > max_br:
                    max_br = tgt
            if w == 0x03E00008 and pc >= max_br:  # jr $ra and no forward branch jumps past it
                end_fn = min(ge, pc + 8)
                if cur not in functions:
                    functions[cur] = [f"func_{cur:08X}", end_fn, True]
                    gap_func_count += 1
                else:
                    functions[cur][1] = end_fn
                pc = end_fn
                while pc < ge:
                    toff2 = sec[2] + (pc - sec[1])
                    if struct.unpack_from("<I", data, toff2)[0] != 0:
                        break
                    pc += 4
                cur = pc
                max_br = cur
                continue
            pc += 4
        if cur < ge and cur not in functions:
            functions[cur] = [f"func_{cur:08X}", ge, True]
            gap_func_count += 1

    # Sort all discovered starts and compute non-overlapping End & Size
    sorted_starts = sorted(functions.keys())
    records = []
    for idx, start in enumerate(sorted_starts):
        name, end_addr, is_auth = functions[start]
        sec = find_exec_section(start)
        sec_end = sec[1] + sec[3]
        next_start = sorted_starts[idx + 1] if idx + 1 < len(sorted_starts) else sec_end
        max_end = min(sec_end, next_start)

        if end_addr == 0 or end_addr > max_end or end_addr <= start:
            end_addr = max_end
        if end_addr <= start:
            end_addr = start + 4

        size = end_addr - start
        records.append((name, start, end_addr, size))

    csv_out.parent.mkdir(parents=True, exist_ok=True)
    with open(csv_out, "w", encoding="utf-8", newline="\n") as f:
        f.write("Name,Start,End,Size\n")
        for name, start, end_addr, size in records:
            f.write(f"{name},0x{start:08X},0x{end_addr:08X},{size}\n")

    print(f"Generated {len(records)} function records in {csv_out}:")
    print(f"  - .eh_frame FDEs:       {eh_count}")
    print(f"  - .gnu.linkonce.t.*:    {sum(1 for s in exec_sections if s[0].startswith('.gnu.linkonce.t.'))}")
    print(f"  - .ctors/.dtors:        {ctor_count}")
    print(f"  - C++ vtable targets:   {vt_count}")
    print(f"  - JAL call targets:     {jal_count}")
    print(f"  - SCE SDK symbol names: {sce_matched}")

    # Build unified drakengard.toml from drakengard_analyzer.toml
    if analyzer_toml.exists():
        txt = analyzer_toml.read_text(encoding="utf-8", errors="replace")
        # Paths are relative to the repo root, where build_recomp.ps1 runs PS2Recomp
        repo_root = toml_out.parent.parent
        out_cpp_dir = (toml_out.parent / "output").relative_to(repo_root).as_posix()
        txt = re.sub(
            r'(?m)^ghidra_output\s*=\s*"[^"]*"',
            f'ghidra_output = "{csv_out.relative_to(repo_root).as_posix()}"',
            txt,
            count=1,
        )
        txt = re.sub(
            r'(?m)^output\s*=\s*"[^"]*"',
            f'output = "{out_cpp_dir}"',
            txt,
            count=1,
        )
        toml_out.write_text(txt, encoding="utf-8")
        print(f"Updated unified {toml_out} with ghidra_output + SCE stubs + 118 jump tables")


if __name__ == "__main__":
    root = Path(__file__).resolve().parent.parent
    generate_function_map(
        elf_path=root / "iso_extracted" / "SLUS_207.32",
        csv_out=root / "recomp" / "functions.csv",
        toml_out=root / "recomp" / "drakengard.toml",
        analyzer_toml=root / "recomp" / "drakengard_analyzer.toml",
    )
