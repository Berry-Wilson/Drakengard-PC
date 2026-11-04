#!/usr/bin/env python3
"""
Drakengard (PS2 - SLUS_207.32) Emotion Engine ELF & C++ Symbol/Class Analyzer.

Extracts:
- Program headers & all 1,763 section headers
- 1,626 GNU C++ (.gnu.linkonce) methods, vtables (__vt_*), and RTTI typeinfo (__tf*)
- Demangled C++ class hierarchy (CTask subclasses, rendering, audio, streaming, gameplay classes)
- 3,840 function boundaries from .eh_frame FDEs + 358 CIE translation unit groups
- 53 VU1 microcode overlays from .DVP.ovlytab / .DVP.ovlystrtab
"""

import json
import re
import struct
from collections import defaultdict
from pathlib import Path


def demangle_gnu2(sym: str) -> dict:
    """Basic GNU v2 (GCC 2.95.2 EE) C++ symbol demangler for .gnu.linkonce sections."""
    # Strip .gnu.linkonce.t. or .gnu.linkonce.d. or .gnu.linkonce.r.
    prefix = ""
    for p in (".gnu.linkonce.t.", ".gnu.linkonce.d.", ".gnu.linkonce.r."):
        if sym.startswith(p):
            prefix = p
            sym = sym[len(p) :]
            break

    kind = "function"
    if sym.startswith("__tf"):
        kind = "rtti_typeinfo"
        rest = sym[4:]
        m = re.match(r"^(\d+)(.+)$", rest)
        if m:
            n = int(m.group(1))
            cls = m.group(2)[:n]
            return {"raw": sym, "kind": kind, "class": cls, "method": "__typeinfo"}
    elif sym.startswith("__vt_") or sym.startswith("_vt$"):
        kind = "vtable"
        rest = sym[4:] if sym.startswith("_vt$") else sym[5:]
        m = re.match(r"^(\d+)(.+)$", rest)
        if m:
            n = int(m.group(1))
            cls = m.group(2)[:n]
            return {"raw": sym, "kind": kind, "class": cls, "method": "__vtable"}

    # Constructor: __<len><Class><args>
    m_ctor = re.match(r"^__(\d+)(.+)$", sym)
    if m_ctor:
        n = int(m_ctor.group(1))
        cls = m_ctor.group(2)[:n]
        args = m_ctor.group(2)[n:]
        return {"raw": sym, "kind": "constructor", "class": cls, "method": f"{cls}::{cls}({args})"}

    # Destructor: _._<len><Class>
    m_dtor = re.match(r"^_\._(\d+)(.+)$", sym)
    if m_dtor:
        n = int(m_dtor.group(1))
        cls = m_dtor.group(2)[:n]
        return {"raw": sym, "kind": "destructor", "class": cls, "method": f"{cls}::~{cls}()"}

    # Member function: <method>__<len><Class><args> or <method>__C<len><Class><args>
    m_meth = re.match(r"^(.+?)__(C?)(\d+)(.+)$", sym)
    if m_meth:
        meth_name = m_meth.group(1)
        is_const = bool(m_meth.group(2))
        n = int(m_meth.group(3))
        cls = m_meth.group(4)[:n]
        args = m_meth.group(4)[n:]
        const_str = " const" if is_const else ""
        return {
            "raw": sym,
            "kind": "method",
            "class": cls,
            "method_name": meth_name,
            "method": f"{cls}::{meth_name}({args}){const_str}",
        }

    return {"raw": sym, "kind": kind, "class": None, "method": sym}


def analyze_elf(elf_path: Path) -> dict:
    data = elf_path.read_bytes()
    if data[:4] != b"\x7fELF":
        raise ValueError("Not a valid ELF binary")

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

    # Program headers
    phdrs = []
    for i in range(e_phnum):
        p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = struct.unpack_from(
            "<8I", data, e_phoff + i * e_phentsize
        )
        phdrs.append({
            "index": i,
            "type": hex(p_type),
            "offset": hex(p_offset),
            "vaddr": hex(p_vaddr),
            "filesz": hex(p_filesz),
            "memsz": hex(p_memsz),
            "flags": hex(p_flags),
            "align": hex(p_align),
        })

    # Section string table
    shstr_hdr = struct.unpack_from("<10I", data, e_shoff + e_shstrndx * e_shentsize)
    strtab = data[shstr_hdr[4] : shstr_hdr[4] + shstr_hdr[5]]

    def get_sh_name(off: int) -> str:
        end = strtab.find(b"\x00", off)
        return strtab[off:end].decode("ascii", errors="replace")

    main_sections = []
    linkonce_symbols = []
    vu_overlays = []
    classes = defaultdict(lambda: {"methods": [], "has_rtti": False, "has_vtable": False, "vaddr_samples": []})

    eh_frame_sec = None

    for i in range(e_shnum):
        s_name, s_type, s_flags, s_addr, s_offset, s_size, s_link, s_info, s_align, s_entsize = struct.unpack_from(
            "<10I", data, e_shoff + i * e_shentsize
        )
        name = get_sh_name(s_name)
        sec_info = {
            "index": i,
            "name": name,
            "type": hex(s_type),
            "vaddr": hex(s_addr),
            "offset": hex(s_offset),
            "size": hex(s_size),
        }
        if name == ".eh_frame":
            eh_frame_sec = (s_addr, s_offset, s_size)

        if name.startswith(".gnu.linkonce."):
            dem = demangle_gnu2(name)
            dem["vaddr"] = hex(s_addr)
            dem["offset"] = hex(s_offset)
            dem["size"] = s_size
            linkonce_symbols.append(dem)
            cls = dem.get("class")
            if cls:
                if dem["kind"] == "rtti_typeinfo":
                    classes[cls]["has_rtti"] = True
                elif dem["kind"] == "vtable":
                    classes[cls]["has_vtable"] = True
                else:
                    classes[cls]["methods"].append({
                        "signature": dem["method"],
                        "vaddr": hex(s_addr),
                        "size": s_size,
                    })
                classes[cls]["vaddr_samples"].append(hex(s_addr))
        elif name.startswith(".DVP.overlay."):
            vu_overlays.append(sec_info)
        else:
            main_sections.append(sec_info)

    # Parse .eh_frame FDEs & CIEs
    fdes = []
    cies = []
    if eh_frame_sec:
        eh_vaddr, eh_off, eh_size = eh_frame_sec
        eh = data[eh_off : eh_off + eh_size]
        pos = 0
        while pos < len(eh):
            length = struct.unpack_from("<I", eh, pos)[0]
            if length == 0:
                pos += 4
                continue
            id_val = struct.unpack_from("<I", eh, pos + 4)[0]
            if id_val == 0:
                cies.append({"offset": hex(eh_off + pos), "length": length})
            else:
                pc_begin, pc_range = struct.unpack_from("<II", eh, pos + 8)
                fdes.append({
                    "vaddr": hex(pc_begin),
                    "end_vaddr": hex(pc_begin + pc_range),
                    "size": pc_range,
                })
            pos += 4 + length

    # Sort classes by number of methods/symbols
    sorted_classes = {
        k: {
            "has_rtti": v["has_rtti"],
            "has_vtable": v["has_vtable"],
            "method_count": len(v["methods"]),
            "methods": v["methods"],
        }
        for k, v in sorted(classes.items(), key=lambda x: (-len(x[1]["methods"]), x[0]))
    }

    return {
        "binary": elf_path.name,
        "entry_point": hex(e_entry),
        "program_headers": phdrs,
        "main_sections": main_sections,
        "summary": {
            "total_sections": e_shnum,
            "main_section_count": len(main_sections),
            "gnu_linkonce_count": len(linkonce_symbols),
            "discovered_cpp_classes": len(sorted_classes),
            "eh_frame_translation_units_cies": len(cies),
            "eh_frame_functions_fdes": len(fdes),
            "vu1_microcode_overlays": len(vu_overlays),
        },
        "cpp_classes": sorted_classes,
        "vu1_overlays": vu_overlays,
        "eh_frame_functions_sample": fdes[:50],
        "eh_frame_functions_total": len(fdes),
    }


if __name__ == "__main__":
    root = Path(__file__).resolve().parent.parent
    elf_file = root / "iso_extracted" / "SLUS_207.32"
    out_file = root / "analysis" / "slus_207_32_analysis.json"
    out_file.parent.mkdir(parents=True, exist_ok=True)
    report = analyze_elf(elf_file)
    out_file.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("ELF Analysis Summary:", json.dumps(report["summary"], indent=2))
    print("Top 25 C++ Classes in SLUS_207.32:")
    for i, (cls, info) in enumerate(list(report["cpp_classes"].items())[:25]):
        print(f"  {i+1:2d}. {cls:28s} (methods={info['method_count']:2d}, rtti={info['has_rtti']}, vtable={info['has_vtable']})")
