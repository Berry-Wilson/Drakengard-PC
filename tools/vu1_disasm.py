"""Disassemble and walk VU1 microprograms dumped by the runtime (PS2X_VU1_DUMP_DIR).

Usage:
  python vu1_disasm.py <vu1_<hash>.bin> <entry_pc_hex> [<entry_pc_hex> ...]
  python vu1_disasm.py --stats <vu1_<hash>.bin> <entry_pc_hex> ...

Each entry is walked by following branches until the E bit (+ its delay slot).
JR/JALR targets cannot be resolved statically and are reported.
"""
import struct
import sys
from collections import Counter

BC = "xyzw"


def dest(i):
    d = (i >> 21) & 0xF
    return "".join(c for c, b in zip("xyzw", (8, 4, 2, 1)) if d & b)


def ft(i): return (i >> 16) & 0x1F
def fs(i): return (i >> 11) & 0x1F
def fd(i): return (i >> 6) & 0x1F


UPPER3 = {0x00: "ADD", 0x04: "SUB", 0x08: "MADD", 0x0C: "MSUB", 0x10: "MAX", 0x14: "MINI", 0x18: "MUL"}
UPPER = {0x1C: "MULq", 0x1D: "MAXi", 0x1E: "MULi", 0x1F: "MINIi", 0x20: "ADDq", 0x21: "MADDq", 0x22: "ADDi",
         0x23: "MADDi", 0x24: "SUBq", 0x25: "MSUBq", 0x26: "SUBi", 0x27: "MSUBi", 0x28: "ADD", 0x29: "MADD",
         0x2A: "MUL", 0x2B: "MAX", 0x2C: "SUB", 0x2D: "MSUB", 0x2E: "OPMSUB", 0x2F: "MINI"}
UPPERS3 = {0x00: "ADDA", 0x04: "SUBA", 0x08: "MADDA", 0x0C: "MSUBA", 0x18: "MULA"}
UPPERS = {0x10: "ITOF0", 0x11: "ITOF4", 0x12: "ITOF12", 0x13: "ITOF15", 0x14: "FTOI0", 0x15: "FTOI4",
          0x16: "FTOI12", 0x17: "FTOI15", 0x1C: "MULAq", 0x1D: "ABS", 0x1E: "MULAi", 0x1F: "CLIP",
          0x20: "ADDAq", 0x21: "MADDAq", 0x22: "ADDAi", 0x23: "MADDAi", 0x24: "SUBAq", 0x25: "MSUBAq",
          0x26: "SUBAi", 0x27: "MSUBAi", 0x28: "ADDA", 0x29: "MADDA", 0x2A: "MULA", 0x2C: "SUBA",
          0x2D: "MSUBA", 0x2E: "OPMULA", 0x2F: "NOP", 0x30: "NOP"}


def upper_name(i):
    op = i & 0x3F
    if op in (0x3C, 0x3D, 0x3E, 0x3F):
        s = ((i >> 4) & 0x7C) | (i & 3)
        if s < 0x10 or 0x18 <= s <= 0x1B:
            return UPPERS3[s & 0x7C] + BC[s & 3]
        return UPPERS.get(s, "UPPER?%02x" % s)
    base = op & 0x3C
    if op < 0x1C:
        return UPPER3[base] + BC[op & 3]
    return UPPER.get(op, "UPPER?%02x" % op)


def upper_text(i):
    n = upper_name(i)
    if n == "NOP":
        return "nop"
    op = i & 0x3F
    d = dest(i)
    if n == "CLIP":
        return "clipw.xyz vf%d, vf%d" % (fs(i), ft(i))
    if n.startswith(("ITOF", "FTOI", "ABS")):
        return "%s.%s vf%d, vf%d" % (n.lower(), d, ft(i), fs(i))
    if op in (0x3C, 0x3D, 0x3E, 0x3F):  # accumulator forms
        if n[-1] in BC or n.endswith(("q", "i")):
            tail = ("vf%d%s" % (ft(i), n[-1])) if n[-1] in BC else n[-1].upper()
            return "%s.%s ACC, vf%d, %s" % (n.lower(), d, fs(i), tail)
        return "%s.%s ACC, vf%d, vf%d" % (n.lower(), d, fs(i), ft(i))
    if n[-1] in BC and op < 0x1C:
        return "%s.%s vf%d, vf%d, vf%d%s" % (n.lower(), d, fd(i), fs(i), ft(i), n[-1])
    if n.endswith(("q", "i")):
        return "%s.%s vf%d, vf%d, %s" % (n.lower(), d, fd(i), fs(i), n[-1].upper())
    return "%s.%s vf%d, vf%d, vf%d" % (n.lower(), d, fd(i), fs(i), ft(i))


LOWER = {0x00: "LQ", 0x01: "SQ", 0x04: "ILW", 0x05: "ISW", 0x08: "IADDIU", 0x09: "ISUBIU", 0x10: "FCEQ",
         0x11: "FCSET", 0x12: "FCAND", 0x13: "FCOR", 0x14: "FSEQ", 0x15: "FSSET", 0x16: "FSAND", 0x17: "FSOR",
         0x18: "FMEQ", 0x1A: "FMAND", 0x1B: "FMOR", 0x1C: "FCGET", 0x20: "B", 0x21: "BAL", 0x24: "JR",
         0x25: "JALR", 0x28: "IBEQ", 0x29: "IBNE", 0x2C: "IBLTZ", 0x2D: "IBGTZ", 0x2E: "IBLEZ", 0x2F: "IBGEZ"}
LOWER1 = {0x30: "IADD", 0x31: "ISUB", 0x32: "IADDI", 0x34: "IAND", 0x35: "IOR"}
LOWER2 = {0x30: "MOVE", 0x31: "MR32", 0x34: "LQI", 0x35: "SQI", 0x36: "LQD", 0x37: "SQD", 0x38: "DIV",
          0x39: "SQRT", 0x3A: "RSQRT", 0x3B: "WAITQ", 0x3C: "MTIR", 0x3D: "MFIR", 0x3E: "ILWR", 0x3F: "ISWR",
          0x40: "RNEXT", 0x41: "RGET", 0x42: "RINIT", 0x43: "RXOR", 0x64: "MFP", 0x68: "XTOP", 0x69: "XITOP",
          0x6C: "XGKICK", 0x70: "ESADD", 0x71: "ERSADD", 0x72: "ELENG", 0x73: "ERLENG", 0x74: "EATANxy",
          0x75: "EATANxz", 0x76: "ESUM", 0x78: "ESQRT", 0x79: "ERSQRT", 0x7A: "ERCPR",
          0x7B: "WAITP", 0x7C: "ESIN", 0x7D: "EATAN", 0x7E: "EEXP"}
BRANCHES = {"B", "BAL", "IBEQ", "IBNE", "IBLTZ", "IBGTZ", "IBLEZ", "IBGEZ"}


def lower_name(i):
    if i == 0x8000033C:
        return "NOP"
    op = (i >> 25) & 0x7F
    if op == 0x40:
        f = i & 0x3F
        if f in (0x3C, 0x3D, 0x3E, 0x3F):
            f2 = (i & 3) | ((i >> 4) & 0x7C)
            if f2 == 0x33:
                return "NOP"
            return LOWER2.get(f2, "LOWER?%02x" % f2)
        return LOWER1.get(f, "LOWER?%02x" % f)
    return LOWER.get(op, "LOWER?op%02x" % op)


def imm11(i):
    v = i & 0x7FF
    return v - 0x800 if v & 0x400 else v


def lower_text(i, pc):
    n = lower_name(i)
    if n == "NOP":
        return "nop"
    vit, vis, vid = (i >> 16) & 0xF, (i >> 11) & 0xF, (i >> 6) & 0xF
    if n in BRANCHES:
        tgt = (pc + 8 + imm11(i) * 8) & 0x3FFF
        if n == "B":
            return "b 0x%04x" % tgt
        if n == "BAL":
            return "bal vi%d, 0x%04x" % (vit, tgt)
        if n in ("IBEQ", "IBNE"):
            return "%s vi%d, vi%d, 0x%04x" % (n.lower(), vit, vis, tgt)
        return "%s vi%d, 0x%04x" % (n.lower(), vis, tgt)
    if n in ("LQ", "SQ"):
        if n == "LQ":
            return "lq.%s vf%d, %d(vi%d)" % (dest(i), ft(i), imm11(i), vis)
        return "sq.%s vf%d, %d(vi%d)" % (dest(i), fs(i), imm11(i), vit)
    if n in ("ILW", "ISW"):
        return "%s.%s vi%d, %d(vi%d)" % (n.lower(), dest(i), vit, imm11(i), vis)
    if n in ("IADDIU", "ISUBIU"):
        imm = ((i >> 10) & 0x7800) | (i & 0x7FF)
        return "%s vi%d, vi%d, 0x%x" % (n.lower(), vit, vis, imm)
    if n in ("IADD", "ISUB", "IAND", "IOR"):
        return "%s vi%d, vi%d, vi%d" % (n.lower(), vid, vis, vit)
    if n == "IADDI":
        imm = (i >> 6) & 0x1F
        imm = imm - 32 if imm & 0x10 else imm
        return "iaddi vi%d, vi%d, %d" % (vit, vis, imm)
    if n in ("JR", "JALR"):
        return "%s vi%d" % (n.lower(), vis) if n == "JR" else "jalr vi%d, vi%d" % (vit, vis)
    if n == "XGKICK":
        return "xgkick vi%d" % vis
    if n in ("LQI", "LQD"):
        return "%s.%s vf%d, (vi%d)" % (n.lower(), dest(i), ft(i), vis)
    if n in ("SQI", "SQD"):
        return "%s.%s vf%d, (vi%d)" % (n.lower(), dest(i), fs(i), vit)
    if n in ("DIV", "RSQRT"):
        return "%s Q, vf%d%s, vf%d%s" % (n.lower(), fs(i), BC[(i >> 21) & 3], ft(i), BC[(i >> 23) & 3])
    if n == "SQRT":
        return "sqrt Q, vf%d%s" % (ft(i), BC[(i >> 23) & 3])
    if n in ("MOVE", "MR32"):
        return "%s.%s vf%d, vf%d" % (n.lower(), dest(i), ft(i), fs(i))
    if n in ("MTIR",):
        return "mtir vi%d, vf%d%s" % (vit, fs(i), BC[(i >> 21) & 3])
    if n in ("MFIR",):
        return "mfir.%s vf%d, vi%d" % (dest(i), ft(i), vis)
    if n in ("XTOP", "XITOP"):
        return "%s vi%d" % (n.lower(), vit)
    if n in ("FCAND", "FCOR", "FCEQ", "FCSET"):
        return "%s vi1, 0x%06x" % (n.lower(), i & 0xFFFFFF)
    if n == "FCGET":
        return "fcget vi%d" % vit
    if n in ("FSAND", "FSOR", "FSEQ", "FMAND", "FMOR", "FMEQ"):
        return "%s vi%d, ..." % (n.lower(), vit)
    return "%s ..." % n.lower()


def walk(code, entry):
    seen, stack, jr = set(), [entry], set()
    while stack:
        pc = stack.pop()
        delay_end = None
        while 0 <= pc < len(code) and pc not in seen:
            if delay_end is not None and pc == delay_end:
                break
            seen.add(pc)
            lo, up = struct.unpack_from("<II", code, pc)
            n = lower_name(lo) if not (up >> 31) & 1 else "LOI"
            if n in BRANCHES:
                stack.append((pc + 8 + imm11(lo) * 8) & 0x3FFF)
                if n == "B":
                    delay_end = pc + 16
            elif n in ("JR", "JALR"):
                jr.add(pc)
                delay_end = pc + 16
            if (up >> 30) & 1:  # E bit
                delay_end = pc + 16
            pc += 8
    return sorted(seen), jr


def main():
    args = sys.argv[1:]
    stats = args and args[0] == "--stats"
    if stats:
        args = args[1:]
    code = open(args[0], "rb").read()
    for e in args[1:]:
        entry = int(e, 16)
        pcs, jr = walk(code, entry)
        print("=== entry 0x%04x: %d pairs, %d JR sites" % (entry, len(pcs), len(jr)))
        if stats:
            c = Counter()
            for pc in pcs:
                lo, up = struct.unpack_from("<II", code, pc)
                c[upper_name(up)] += 1
                if not (up >> 31) & 1:
                    c[lower_name(lo)] += 1
            print("  ", ", ".join("%s=%d" % kv for kv in c.most_common()))
            continue
        for pc in pcs:
            lo, up = struct.unpack_from("<II", code, pc)
            flags = "".join(f for f, b in zip("IEMDT", (31, 30, 29, 28, 27)) if (up >> b) & 1)
            ltxt = ("loi %r" % struct.unpack("<f", struct.pack("<I", lo))[0]) if (up >> 31) & 1 else lower_text(lo, pc)
            print("%04x: %-5s %-40s %s" % (pc, flags, upper_text(up), ltxt))


if __name__ == "__main__":
    main()
