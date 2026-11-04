"""Translate Drakengard's VU0/VU1 microprograms to C++ (static recompilation).

Extracts every VIF MPG packet from the game's microcode bank for the chosen unit in
SLUS_207.32, merges packets that continue each other into units, splits the code into basic
blocks and emits one C++ function per block. The runtime (ps2_vu1_native.h/.cpp) picks the
blocks whose bytes match micro memory and runs them instead of the interpreter.

Usage: python tools/vu1_recomp.py [--unit vu1|vu0] [--elf PATH] [--out-dir DIR] [--files N]
"""
import argparse
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vu1_disasm import lower_name, lower_text, upper_text  # noqa: E402

# Per-unit settings; main() installs one of these as the module globals below.
UNITS = {
    # 0x21FE0C/0x220234 hold two more VU1 programs (loaded at VU address 0), stored just before VU0's.
    "vu1": {"banks": [(0x21FE00, 0x220700), (0x428000, 0x437000)], "code_size": 0x4000, "files": 6,
            "qaddr": "vu1n::qaddr", "unit": "vu1n::Unit::VU1", "reserved": set()},
    # VU0 has no EFU, P register or PATH1: those opcodes are reserved and stop the interpreter.
    "vu0": {"banks": [(0x220700, 0x222000)], "code_size": 0x1000, "files": 1,
            "qaddr": "vu1n::qaddr<0x1000u>", "unit": "vu1n::Unit::VU0",
            "reserved": {"XGKICK", "MFP", "WAITP", "ESADD", "ERSADD", "ELENG", "ERLENG", "EATANxy",
                         "EATANxz", "ESUM", "ERSQRT", "ESQRT", "ESIN", "ERCPR", "EATAN", "EEXP"}},
}
BANKS = UNITS["vu1"]["banks"]
CODE_SIZE = UNITS["vu1"]["code_size"]
PC_MASK = CODE_SIZE - 1
QADDR = UNITS["vu1"]["qaddr"]
RESERVED = UNITS["vu1"]["reserved"]


# --------------------------------------------------------------------------- extraction

def extract_units(elf):
    packets = []
    for off in (o for start, end in BANKS for o in range(start, end, 4)):
        w = struct.unpack_from("<I", elf, off)[0]
        if (w >> 24) & 0x7F != 0x4A or (off + 4) % 8:
            continue
        num = (w >> 16) & 0xFF or 256
        addr = (w & 0xFFFF) * 8
        if addr >= CODE_SIZE:
            continue
        words = list(struct.unpack_from("<%dI" % (num * 2), elf, off + 4))
        packets.append({"off": off, "addr": addr, "words": words})

    units = []
    for p in packets:
        last = units[-1] if units else None
        if (last and last["addr"] + len(last["words"]) * 4 == p["addr"]
                and p["off"] - last["end_off"] <= 16):
            last["words"] += p["words"]
            last["end_off"] = p["off"] + 4 + len(p["words"]) * 4
        else:
            units.append({"off": p["off"], "addr": p["addr"], "words": list(p["words"]),
                          "end_off": p["off"] + 4 + len(p["words"]) * 4})
    for u in units:
        u["end"] = u["addr"] + len(u["words"]) * 4
    return units


def pair(unit, pc):
    i = (pc - unit["addr"]) // 4
    return unit["words"][i], unit["words"][i + 1]


# --------------------------------------------------------------------------- decoding

def imm11(lo):
    v = lo & 0x7FF
    return v - 0x800 if v & 0x400 else v


def lane(comp):
    return 1 << (3 - comp)


def comps(mask):
    return [c for c in range(4) if mask & lane(c)]


COND_BRANCHES = {"IBEQ", "IBNE", "IBLTZ", "IBGTZ", "IBLEZ", "IBGEZ"}
BRANCHES = COND_BRANCHES | {"B", "BAL"}
JUMPS = {"JR", "JALR"}
DELAYS_BRANCH_READ = {"IADDIU", "ISUBIU", "IADD", "ISUB", "IAND", "IOR", "IADDI", "LQI", "LQD", "SQI", "SQD", "MTIR"}


def lower_op(lo, up):
    if (up >> 31) & 1:
        return "LOI"
    return lower_name(lo)


class Unsupported(Exception):
    pass


# Upper ops: (kind, operand-b source, destination) keyed by opcode / special opcode.
FMAC_KIND = {0x00: "Add", 0x04: "Sub", 0x08: "Madd", 0x0C: "Msub", 0x18: "Mul"}
UPPER_QI = {0x1C: ("Mul", "q"), 0x1E: ("Mul", "i"), 0x20: ("Add", "q"), 0x21: ("Madd", "q"),
            0x22: ("Add", "i"), 0x23: ("Madd", "i"), 0x24: ("Sub", "q"), 0x25: ("Msub", "q"),
            0x26: ("Sub", "i"), 0x27: ("Msub", "i")}
UPPER_FULL = {0x28: "Add", 0x29: "Madd", 0x2A: "Mul", 0x2C: "Sub", 0x2D: "Msub"}


def decode_upper(up):
    """Returns dict: code (expression or statement), write (reg, dest) / 'acc', reads [(reg, lanes)]."""
    op = up & 0x3F
    dest = (up >> 21) & 0xF
    ft, fs, fd = (up >> 16) & 0x1F, (up >> 11) & 0x1F, (up >> 6) & 0x1F
    a = "vu1n::norm(vu1n::ldv(c, %d))" % fs
    full_b = "vu1n::norm(vu1n::ldv(c, %d))" % ft
    accv = "vu1n::norm(vu1n::ldacc(c))"

    def bc_b(bc):
        return "vu1n::bcast<%d>(vu1n::norm(vu1n::ldv(c, %d)))" % (bc, ft)

    # MAX/MINI work on raw bits (vu1n::vmax/vmini): no normalization of their operands, and their
    # result is one operand's original bits, so it is not known to be normalized.
    raw_a = "vu1n::ldv(c, %d)" % fs
    raw_full_b = "vu1n::ldv(c, %d)" % ft

    def raw_bc_b(bc):
        return "vu1n::bcast<%d>(vu1n::ldv(c, %d))" % (bc, ft)

    def qi_b(src):
        return "_mm_set1_ps(vu1n::norm1(vu1n::readQ(c)))" if src == "q" else "_mm_set1_ps(vu1n::norm1(c.i))"

    def fmac(kind, b, to_acc):
        acc = accv if kind in ("Madd", "Msub") else "_mm_setzero_ps()"
        expr = "vu1n::fmac<vu1n::k%s, %d>(c, %s, %s, %s)" % (kind, dest, a, b, acc)
        return {"expr": expr, "write": ("acc", dest) if to_acc else (fd, dest), "norm_result": True,
                "acc_reads": dest if kind in ("Madd", "Msub") else 0}

    if op < 0x30:
        reads = [(fs, dest)]
        if op <= 0x1B:
            reads.append((ft, lane(op & 3)))
        elif op >= 0x28:
            reads.append((ft, 0xE if op == 0x2E else dest))
        if op <= 0x1B:
            base, bc = op & 0x1C, op & 3
            if base in FMAC_KIND:
                r = fmac(FMAC_KIND[base], bc_b(bc), False)
            elif base == 0x10:
                r = {"expr": "vu1n::vmax(%s, %s)" % (raw_a, raw_bc_b(bc)), "write": (fd, dest)}
            else:  # 0x14 MINIbc
                r = {"expr": "vu1n::vmini(%s, %s)" % (raw_a, raw_bc_b(bc)), "write": (fd, dest)}
        elif op in UPPER_QI:
            kind, src = UPPER_QI[op]
            r = fmac(kind, qi_b(src), False)
        elif op == 0x1D:
            r = {"expr": "vu1n::vmax(%s, _mm_set1_ps(c.i))" % raw_a, "write": (fd, dest)}
        elif op == 0x1F:
            r = {"expr": "vu1n::vmini(%s, _mm_set1_ps(c.i))" % raw_a, "write": (fd, dest)}
        elif op in UPPER_FULL:
            r = fmac(UPPER_FULL[op], full_b, False)
        elif op == 0x2B:
            r = {"expr": "vu1n::vmax(%s, %s)" % (raw_a, raw_full_b), "write": (fd, dest)}
        elif op == 0x2F:
            r = {"expr": "vu1n::vmini(%s, %s)" % (raw_a, raw_full_b), "write": (fd, dest)}
        else:
            raise Unsupported("upper op 0x%02x" % op)
        r["reads"] = reads
        return r

    if op < 0x3C:
        raise Unsupported("upper op 0x%02x" % op)
    s = (up & 3) | ((up >> 4) & 0x7C)
    if s in (0x2F, 0x30):
        return {"reads": [], "write": None}
    writes_acc = s <= 0x0F or 0x18 <= s <= 0x1C or s == 0x1E or 0x20 <= s <= 0x2A or 0x2C <= s <= 0x2E
    if writes_acc:
        reads = [(fs, dest)]
        if s <= 0x1B:
            reads.append((ft, lane(s & 3)))
        elif 0x28 <= s <= 0x2E:
            reads.append((ft, 0xE if s == 0x2E else dest))
        if s <= 0x0F or 0x18 <= s <= 0x1B:
            r = fmac(FMAC_KIND[s & 0x1C], bc_b(s & 3), True)
        elif s in UPPER_QI:
            kind, src = UPPER_QI[s]
            r = fmac(kind, qi_b(src), True)
        elif s in UPPER_FULL:
            r = fmac(UPPER_FULL[s], full_b, True)
        else:
            raise Unsupported("upper special 0x%02x" % s)  # OPMULA
        r["reads"] = reads
        return r
    if 0x10 <= s <= 0x13:
        scale = (0, 4, 12, 15)[s & 3]
        return {"expr": "vu1n::itof<%d>(vu1n::ldv(c, %d))" % (scale, fs), "write": (ft, dest), "reads": [(fs, dest)],
                "norm_result": True}
    if 0x14 <= s <= 0x17:
        scale = (0, 4, 12, 15)[s & 3]
        return {"expr": "vu1n::ftoi<%d>(%s)" % (scale, a), "write": (ft, dest), "reads": [(fs, dest)]}
    if s == 0x1D:
        return {"expr": "vu1n::vabs(%s)" % a, "write": (ft, dest), "reads": [(fs, dest)], "norm_result": True}
    if s == 0x1F:
        return {"stmt": "vu1n::clip(c, %d, %d);" % (fs, ft), "write": None, "reads": [(fs, 0xE), (ft, 0x1)]}
    raise Unsupported("upper special 0x%02x" % s)


def decode_lower(lo, pc):
    """Returns dict: name, stmts, reads_vf, reads_vi, vf_write (reg, dest), vi_write (reg, latency), flags."""
    n = lower_name(lo)
    dest = (lo >> 21) & 0xF
    vit, vis, vid = (lo >> 16) & 0xF, (lo >> 11) & 0xF, (lo >> 6) & 0xF
    ft, fs = (lo >> 16) & 0x1F, (lo >> 11) & 0x1F
    fsf, ftf = (lo >> 21) & 3, (lo >> 23) & 3
    d = {"name": n, "stmts": [], "reads_vf": [], "reads_vi": [], "vf_write": None, "vi_write": None,
         "fdiv": False, "efu": False, "xgkick": False}
    if n in RESERVED:
        raise Unsupported("lower %s is reserved on this unit" % n)

    def vi_set(reg, expr, latency=1):
        if reg:
            d["stmts"].append("c.vi[%d] = %s;" % (reg, expr))
            d["vi_write"] = (reg, latency)

    if n == "NOP":
        return d
    if n == "LQ":
        d["reads_vi"] = [vis]
        if ft and dest:
            d["stmts"].append(("vu1n::stv<%d>(c.vf[%d], vu1n::lq(c, " + QADDR + "(c.vi[%d], %d)));") % (dest, ft, vis, imm11(lo)))
            d["vf_write"] = (ft, dest)
    elif n == "SQ":
        d["reads_vi"] = [vit]
        d["reads_vf"] = [(fs, dest)]
        if dest:
            d["stmts"].append(("vu1n::sq<%d>(c, " + QADDR + "(c.vi[%d], %d), vu1n::ldv(c, %d));") % (dest, vit, imm11(lo), fs))
    elif n == "ILW":
        d["reads_vi"] = [vis]
        vi_set(vit, ("vu1n::ilw<%d>(c, " + QADDR + "(c.vi[%d], %d))") % (dest, vis, imm11(lo)), 4)
    elif n == "ISW":
        d["reads_vi"] = [vis, vit]
        if dest:
            d["stmts"].append(("vu1n::isw<%d>(c, " + QADDR + "(c.vi[%d], %d), c.vi[%d]);") % (dest, vis, imm11(lo), vit))
    elif n == "ILWR":
        d["reads_vi"] = [vis]
        vi_set(vit, ("vu1n::ilw<%d>(c, " + QADDR + "(c.vi[%d], 0))") % (dest, vis), 4)
    elif n == "ISWR":
        d["reads_vi"] = [vis, vit]
        if dest:
            d["stmts"].append(("vu1n::isw<%d>(c, " + QADDR + "(c.vi[%d], 0), c.vi[%d]);") % (dest, vis, vit))
    elif n == "FCSET":
        d["stmts"].append("vu1n::fcset(c, 0x%xu);" % (lo & 0xFFFFFF))
    elif n in ("IADDIU", "ISUBIU"):
        imm = ((lo >> 10) & 0x7800) | (lo & 0x7FF)
        d["reads_vi"] = [vis]
        vi_set(vit, "vu1n::s16(c.vi[%d] %s %d)" % (vis, "+" if n == "IADDIU" else "-", imm))
    elif n in ("IADD", "ISUB", "IAND", "IOR"):
        opc = {"IADD": "+", "ISUB": "-", "IAND": "&", "IOR": "|"}[n]
        d["reads_vi"] = [vis, vit]
        vi_set(vid, "vu1n::s16(c.vi[%d] %s c.vi[%d])" % (vis, opc, vit))
    elif n == "IADDI":
        imm = (lo >> 6) & 0x1F
        imm = imm - 32 if imm & 0x10 else imm
        d["reads_vi"] = [vis]
        vi_set(vit, "vu1n::s16(c.vi[%d] + %d)" % (vis, imm))
    elif n == "FCAND":
        vi_set(1, "(vu1n::readClip(c) & 0x%xu) != 0u ? 1 : 0" % (lo & 0xFFFFFF))
    elif n == "FCOR":
        vi_set(1, "(vu1n::readClip(c) | 0x%xu) == 0xFFFFFFu ? 1 : 0" % (lo & 0xFFFFFF))
    elif n == "FCEQ":
        vi_set(1, "(vu1n::readClip(c) & 0xFFFFFFu) == 0x%xu ? 1 : 0" % (lo & 0xFFFFFF))
    elif n == "FCGET":
        vi_set(vit, "static_cast<int32_t>(vu1n::readClip(c) & 0xFFFu)")
    elif n in ("FMAND", "FMOR"):
        d["reads_vi"] = [vis]
        vi_set(vit, "vu1n::s16(static_cast<int32_t>(vu1n::readMac(c) %s static_cast<uint16_t>(c.vi[%d])))"
               % ("&" if n == "FMAND" else "|", vis))
    elif n == "FMEQ":
        d["reads_vi"] = [vis]
        vi_set(vit, "(vu1n::readMac(c) & 0xFFFFu) == static_cast<uint16_t>(c.vi[%d]) ? 1 : 0" % vis)
    elif n in ("B", "BAL"):
        if n == "BAL":
            d["link"] = vit
            d["vi_write"] = (vit, 1) if vit else None
    elif n in ("JR", "JALR"):
        d["reads_vi"] = [vis]
        if n == "JALR":
            d["link"] = vit
            d["vi_write"] = (vit, 1) if vit else None
    elif n in COND_BRANCHES:
        d["reads_vi"] = [vis, vit] if n in ("IBEQ", "IBNE") else [vis]
    elif n in ("MOVE", "MR32"):
        d["reads_vf"] = [(fs, 0xF if n == "MR32" else dest)]
        if ft and dest:
            v = "vu1n::ldv(c, %d)" % fs
            if n == "MR32":
                v = "_mm_shuffle_ps(%s, %s, _MM_SHUFFLE(0, 3, 2, 1))" % (v, v)
            d["stmts"].append("vu1n::stv<%d>(c.vf[%d], %s);" % (dest, ft, v))
            d["vf_write"] = (ft, dest)
    elif n in ("LQI", "LQD"):
        d["reads_vi"] = [vis]
        if n == "LQD" and vis:
            d["stmts"].append("c.vi[%d] = vu1n::s16(c.vi[%d] - 1);" % (vis, vis))
        if ft and dest:
            d["stmts"].append(("vu1n::stv<%d>(c.vf[%d], vu1n::lq(c, " + QADDR + "(c.vi[%d], 0)));") % (dest, ft, vis))
            d["vf_write"] = (ft, dest)
        if n == "LQI" and vis:
            d["stmts"].append("c.vi[%d] = vu1n::s16(c.vi[%d] + 1);" % (vis, vis))
        if vis:
            d["vi_write"] = (vis, 1)
    elif n in ("SQI", "SQD"):
        d["reads_vi"] = [vit]
        d["reads_vf"] = [(fs, dest)]
        if n == "SQD" and vit:
            d["stmts"].append("c.vi[%d] = vu1n::s16(c.vi[%d] - 1);" % (vit, vit))
        if dest:
            d["stmts"].append(("vu1n::sq<%d>(c, " + QADDR + "(c.vi[%d], 0), vu1n::ldv(c, %d));") % (dest, vit, fs))
        if n == "SQI" and vit:
            d["stmts"].append("c.vi[%d] = vu1n::s16(c.vi[%d] + 1);" % (vit, vit))
        if vit:
            d["vi_write"] = (vit, 1)
    elif n == "DIV":
        d["reads_vf"] = [(fs, lane(fsf)), (ft, lane(ftf))]
        d["fdiv"] = True
        d["stmts"].append("vu1n::vdiv(c, c.vf[%d][%d], c.vf[%d][%d]);" % (fs, fsf, ft, ftf))
    elif n == "SQRT":
        d["reads_vf"] = [(ft, lane(ftf))]
        d["fdiv"] = True
        d["stmts"].append("vu1n::vsqrt(c, c.vf[%d][%d]);" % (ft, ftf))
    elif n == "RSQRT":
        d["reads_vf"] = [(fs, lane(fsf)), (ft, lane(ftf))]
        d["fdiv"] = True
        d["stmts"].append("vu1n::vrsqrt(c, c.vf[%d][%d], c.vf[%d][%d]);" % (fs, fsf, ft, ftf))
    elif n == "WAITQ":
        d["fdiv"] = True
    elif n == "MTIR":
        d["reads_vf"] = [(fs, lane(fsf))]
        if vit:
            vi_set(vit, "vu1n::s16(static_cast<int32_t>(vu1n::bits(c.vf[%d][%d]) & 0xFFFFu))" % (fs, fsf))
    elif n == "MFIR":
        d["reads_vi"] = [vis]
        if ft and dest:
            d["stmts"].append("vu1n::stv<%d>(c.vf[%d], _mm_castsi128_ps(_mm_set1_epi32(static_cast<int16_t>(c.vi[%d]))));" % (dest, ft, vis))
            d["vf_write"] = (ft, dest)
    elif n == "MFP":
        if ft and dest:
            d["stmts"].append("vu1n::stv<%d>(c.vf[%d], _mm_set1_ps(vu1n::readP(c)));" % (dest, ft))
            d["vf_write"] = (ft, dest)
    elif n == "XTOP":
        vi_set(vit, "static_cast<int32_t>(c.top & 0x3FFu)")
    elif n == "XGKICK":
        d["reads_vi"] = [vis]
        d["xgkick"] = True
        d["stmts"].append("vu1n::xgkick(c, c.vi[%d]);" % vis)
    elif n == "ESIN":
        d["reads_vf"] = [(fs, lane(fsf))]
        d["efu"] = True
        d["stmts"].append("vu1n::queueP(c, vu1n::esin(vu1n::norm1(c.vf[%d][%d])), 29u);" % (fs, fsf))
    elif n == "ERSQRT":
        d["reads_vf"] = [(fs, lane(fsf))]
        d["efu"] = True
        d["stmts"].append("vu1n::queueP(c, vu1n::ersqrt(vu1n::norm1(c.vf[%d][%d])), 18u);" % (fs, fsf))
    else:
        raise Unsupported("lower %s" % n)
    d["reads_vi"] = [r for r in d["reads_vi"] if r]
    d["reads_vf"] = [(r, m) for r, m in d["reads_vf"] if r and m]
    return d


# --------------------------------------------------------------------------- analysis

def find_leaders(units):
    """Per-unit block leaders. Targets that leave their own unit are applied to every unit."""
    local = []
    cross = {0}
    ilw_targets = set()
    for u in units:
        own = set(range(u["addr"], min(u["end"], u["addr"] + 0x80), 0x10))  # MSCAL entry tables
        jr_regs = set()
        consts = []
        targets = set()
        for pc in range(u["addr"], u["end"], 8):
            lo, up = pair(u, pc)
            n = lower_op(lo, up)
            if n in BRANCHES:
                targets.add((pc + 8 + imm11(lo) * 8) & PC_MASK)
            if n in BRANCHES or n in JUMPS or (up >> 30) & 1:
                own.add((pc + 16) & PC_MASK)
            if n in JUMPS:
                jr_regs.add((lo >> 11) & 0xF)
            if n == "IADDIU" and (lo >> 11) & 0xF == 0:
                consts.append(((lo >> 16) & 0xF, ((lo >> 10) & 0x7800) | (lo & 0x7FF)))
            if n == "ILW" and (lo >> 16) & 0xF:
                ilw_targets.add((lo >> 16) & 0xF)
        for reg, k in consts:
            if reg in jr_regs and k * 8 < CODE_SIZE:
                targets.add(k * 8)
        for t in targets:
            (own if u["addr"] <= t < u["end"] else cross).add(t)
        local.append(own)
    leaders = [{t for t in own | cross if u["addr"] <= t < u["end"]} for own, u in zip(local, units)]
    return leaders, ilw_targets


# --------------------------------------------------------------------------- emission

class BlockEmitter:
    # VF results are ready 4 cycles after issue and every pair advances the cycle count by at least
    # one, so inside a block:
    #  - a value written before the block is ready by its 4th pair (index 3): only the first three
    #    pairs need to stall on c.vfReady;
    #  - a value written by pair j of the block is ready at pair j + 4: later reads skip the check,
    #    nearer ones compare against a local holding that write's ready cycle;
    #  - only writes in the last three pairs can still be pending at block exit, so only they update
    #    c.vfReady for the following blocks.
    # Stall timing is unchanged; only checks that can never stall are dropped.
    VF_LATENCY = 4

    def __init__(self, unit, uidx, leaders, ilw_targets):
        self.u, self.uidx, self.leaders, self.ilw = unit, uidx, leaders, ilw_targets
        self.start = None   # pc of the block being emitted
        self.pairs = None   # pairs in that block (None during the sizing pass)
        self.wvars = {}     # (vf reg, lane) -> (pair index of its last in-block write, local holding its ready cycle)
        # Lanes written earlier in this block by an op whose result is already normalized (FMAC,
        # MAX/MINI, ITOF, ABS, MFP); reads of those skip vu1n::norm. "acc" tracks the accumulator.
        self.normv = set()
        self.ncache = {}    # vf reg -> local holding vu1n::norm of its current value (until the reg is written)
        self.dead_mac = set()  # pair indices whose FMAC needs no MAC flag push (see mac_dead_pairs)

    def reset_block_state(self):
        self.wvars = {}
        self.normv = set()
        self.ncache = {}
        self.dead_mac = self.mac_dead_pairs() if self.pairs is not None else set()

    def mac_dead_pairs(self):
        """Pair indices whose MAC flag push can never be observed.

        readMac returns the newest push issued at least 4 cycles earlier. A push at pair i is
        shadowed for every later reader if a later push at pair j has at least 4 pairs (so at least
        4 cycles) left in the block, and no FMAND/FMOR/FMEQ in the block reads between them. The
        block's newest push is never dropped, so the final MAC flags are unchanged.
        """
        pushes, reads = [], []
        for k in range(self.pairs):
            lo, up = pair(self.u, self.start + 8 * k)
            dest = (up >> 21) & 0xF
            if dest and "vu1n::fmac<" in decode_upper(up).get("expr", ""):
                pushes.append(k)
            if not (up >> 31) & 1 and lower_name(lo) in ("FMAND", "FMOR", "FMEQ"):
                reads.append(k)
        dead = set()
        for i in pushes:
            if any(r >= i for r in reads):
                continue
            if any(j > i and self.pairs - j >= 4 for j in pushes):
                dead.add(i)
        return dead

    def known_norm(self, reg, mask):
        if reg == 0:
            return True  # vf0 is (0, 0, 0, 1)
        return all((reg, comp) in self.normv for comp in comps(mask))

    def drop_redundant_norm(self, upd):
        """Rewrites upper-op operand loads of lanes already known to be normalized."""
        for key in ("expr", "stmt"):
            if key not in upd:
                continue
            text = upd[key]
            lanes = {}
            for reg, mask in upd["reads"]:
                lanes[reg] = lanes.get(reg, 0) | mask
            for reg, mask in lanes.items():
                if self.known_norm(reg, mask):
                    text = text.replace("vu1n::norm(vu1n::ldv(c, %d))" % reg, "vu1n::ldv(c, %d)" % reg)
            acc_mask = upd.get("acc_reads", 0)
            if acc_mask and self.known_norm("acc", acc_mask):
                text = text.replace("vu1n::norm(vu1n::ldacc(c))", "vu1n::ldacc(c)")
            upd[key] = text

    def note_norm_writes(self, upd, lod):
        uw = upd.get("write")
        if uw and uw[1]:
            reg = uw[0]
            produces_norm = upd.get("norm_result", False)
            for comp in comps(uw[1]):
                if produces_norm:
                    self.normv.add((reg, comp))
                else:
                    self.normv.discard((reg, comp))
        lw = lod.get("vf_write")
        if lw:
            reg, mask = lw
            for comp in comps(mask):
                if lod["name"] == "MFP":
                    self.normv.add((reg, comp))
                else:
                    self.normv.discard((reg, comp))

    def vf_stall_exprs(self, k, reg, mask):
        """Stall operands for reading `mask` lanes of vf `reg` at pair index k of the block."""
        out = []
        for comp in comps(mask):
            if (reg, comp) in self.wvars:
                j, var = self.wvars[(reg, comp)]
                if k - j < self.VF_LATENCY:
                    out.append(var)
            elif k < self.VF_LATENCY - 1:
                out.append("c.vfReady[%d][%d]" % (reg, comp))
        return out

    def emit_pair(self, pc, out, decls):
        """Emits one instruction pair. Returns (lower decode, upper decode, old-value var or None)."""
        lo, up = pair(self.u, pc)
        if up & 0x38000000:
            raise Unsupported("M/D/T bit")
        ibit = (up >> 31) & 1
        upd = decode_upper(up)
        lod = decode_lower(lo, pc) if not ibit else {
            "name": "LOI", "stmts": [], "reads_vf": [], "reads_vi": [], "vf_write": None,
            "vi_write": None, "fdiv": False, "efu": False, "xgkick": False}

        uw = upd.get("write")
        u_reg = uw[0] if uw and uw[0] != "acc" else None
        if u_reg == 0 and any(r == 0 for r, m in lod["reads_vf"]):
            raise Unsupported("lower reads vf0 written by upper in same pair")
        suppressed = u_reg and lod["vf_write"] and lod["vf_write"][0] == u_reg
        if suppressed:
            # The interpreter drops the lower write entirely when both units write one register.
            lod["stmts"] = [s for s in lod["stmts"] if not s.startswith("vu1n::stv<%d>(c.vf[%d]" % (lod["vf_write"][1], u_reg))]
            lod["vf_write"] = None
        self.drop_redundant_norm(upd)
        k = (pc - self.start) // 8
        if k in self.dead_mac:
            e = upd["expr"]
            at = e.index(">(", e.index("vu1n::fmac<"))
            upd["expr"] = e[:at] + ", false" + e[at:]
        # Normalize each operand register once per value: later reads in this block reuse the local
        # until the register is written. The fill is a separate statement because the order in which
        # function arguments are evaluated is unspecified.
        nfill = []
        for key in ("expr", "stmt"):
            if key not in upd:
                continue
            text = upd[key]
            for reg in sorted({r for r, _ in upd["reads"]}):
                pat = "vu1n::norm(vu1n::ldv(c, %d))" % reg
                if pat not in text:
                    continue
                var = self.ncache.get(reg)
                if var is None:
                    var = "n_%04x_%d" % (pc, reg)
                    decls.append("    __m128 %s;" % var)
                    nfill.append("        %s = %s;" % (var, pat))
                    self.ncache[reg] = var
                text = text.replace(pat, var)
            upd[key] = text

        comment = upper_text(up) + " | " + (("loi 0x%08x" % lo) if ibit else lower_text(lo, pc))
        out.append("    // %04x: %s" % (pc, comment))
        out.append("    {")
        stalls = []
        for reg, mask in upd["reads"] + lod["reads_vf"]:
            if reg:
                stalls.extend(self.vf_stall_exprs(k, reg, mask))
        for reg in lod["reads_vi"]:
            if reg in self.ilw:
                stalls.append("c.viReady[%d]" % reg)
        stalls = list(dict.fromkeys(stalls))
        conds = []
        if lod["fdiv"]:
            conds.append(("c.qPending", "c.qReady"))
        if lod["efu"]:
            stalls.append("c.efuReady")
        if lod["xgkick"]:
            conds.append(("c.kickActive", "c.kickDone"))
        if stalls or conds:
            out.append("        uint32_t r = c.cyc;")
            for s in stalls:
                out.append("        vu1n::stall(r, %s);" % s)
            for cond, s in conds:
                out.append("        if (%s) vu1n::stall(r, %s);" % (cond, s))
            out.append("        c.cyc = r;")

        old = None
        if lod["name"] in DELAYS_BRANCH_READ and lod["vi_write"]:
            old = "old_%04x" % pc
            decls.append("    int32_t %s;" % old)
            out.append("        %s = c.vi[%d];" % (old, lod["vi_write"][0]))

        out.extend(nfill)
        if "expr" in upd:
            out.append("        const __m128 u = %s;" % upd["expr"])
        elif "stmt" in upd:
            out.append("        " + upd["stmt"])
        for s in lod["stmts"]:
            out.append("        " + s)
        if "expr" in upd and uw[1]:
            if uw[0] == "acc":
                out.append("        vu1n::stv<%d>(c.acc, u);" % uw[1])
            elif uw[0] != 0:
                out.append("        vu1n::stv<%d>(c.vf[%d], u);" % (uw[1], uw[0]))
        if ibit:
            out.append("        c.i = vu1n::norm1(vu1n::fromBits(0x%08xu));" % lo)

        writes = []
        if u_reg and uw[1]:
            writes.append((u_reg, uw[1]))
        if lod["vf_write"]:
            writes.append(lod["vf_write"])
        for reg, mask in writes:
            var = "rdy_%04x_%d" % (pc, reg)
            decls.append("    uint32_t %s;" % var)
            out.append("        %s = c.cyc + %du;" % (var, self.VF_LATENCY))
            for comp in comps(mask):
                self.wvars[(reg, comp)] = (k, var)
            if self.pairs is None or self.pairs - k < self.VF_LATENCY:
                for comp in comps(mask):
                    out.append("        c.vfReady[%d][%d] = %s;" % (reg, comp, var))
        self.note_norm_writes(upd, lod)
        for reg, _ in writes:
            self.ncache.pop(reg, None)
        if lod["vi_write"] and lod["vi_write"][0] in self.ilw:
            out.append("        c.viReady[%d] = c.cyc + %du;" % lod["vi_write"])
        out.append("        ++c.cyc;")
        out.append("    }")
        return lod, upd, old

    def branch_vi(self, reg, prev):
        """Expression for a VI read by a branch, honoring the previous pair's pending IALU write."""
        if reg == 0:
            return "0"
        if prev is None:
            return "vu1n::branchVi(c, %d)" % reg
        lod, old = prev
        if old and lod["vi_write"][0] == reg:
            return old
        return "c.vi[%d]" % reg

    def block(self, start):
        """Returns (function body lines, end pc, pairs) or raises Unsupported."""
        u = self.u
        out = []
        decls = []
        pc = start
        prev = None  # (lower decode, old var) of the previous pair in this block
        first = True
        while True:
            if pc + 8 > u["end"]:
                raise Unsupported("block runs off unit end")
            lo, up = pair(u, pc)
            n = lower_op(lo, up)
            ebit = (up >> 30) & 1
            is_ctl = n in BRANCHES or n in JUMPS
            if is_ctl:
                target = (pc + 8 + imm11(lo) * 8) & PC_MASK
                vis, vit = (lo >> 11) & 0xF, (lo >> 16) & 0xF
                pv = None if first else prev
                s_expr = self.branch_vi(vis, pv)
                t_expr = self.branch_vi(vit, pv)
                cond = {"IBEQ": "%s == %s" % (s_expr, t_expr), "IBNE": "%s != %s" % (s_expr, t_expr),
                        "IBLTZ": "%s < 0" % s_expr, "IBGTZ": "%s > 0" % s_expr,
                        "IBLEZ": "%s <= 0" % s_expr, "IBGEZ": "%s >= 0" % s_expr}.get(n)
                decls.append("    uint32_t next = 0;")
                if n in COND_BRANCHES:
                    out.append("    next = (%s) ? 0x%04xu : 0x%04xu;" % (cond, target, (pc + 16) & PC_MASK))
                elif n in ("B", "BAL"):
                    out.append("    next = 0x%04xu;" % target)
                else:
                    out.append("    next = (static_cast<uint32_t>(static_cast<uint16_t>(%s)) * 8u) & 0x%04xu;" % (s_expr, PC_MASK))
                lod, upd, old = self.emit_pair(pc, out, decls)
                if lod.get("link"):
                    # Link register write happens in the branch pair (after its stall).
                    out.insert(len(out) - 1, "        c.vi[%d] = %d;" % (lod["link"], ((pc + 16) & PC_MASK) // 8))
                if pc + 16 > u["end"]:
                    raise Unsupported("delay slot off unit end")
                dlo, dup = pair(u, pc + 8)
                dn = lower_op(dlo, dup)
                if dn in BRANCHES or dn in JUMPS or (dup >> 30) & 1:
                    raise Unsupported("control flow in delay slot")
                dlod, _, dold = self.emit_pair(pc + 8, out, decls)
                self.exit_bk(out, dlod, dold)
                if ebit:
                    out.append("    c.endPc = next;")
                    out.append("    return vu1n::kEnd;")
                else:
                    out.append("    return next;")
                return decls, out, pc + 16

            lod, upd, old = self.emit_pair(pc, out, decls)
            prev = (lod, old)
            first = False
            if ebit:
                if pc + 16 > u["end"]:
                    raise Unsupported("E delay slot off unit end")
                dlo, dup = pair(u, pc + 8)
                dn = lower_op(dlo, dup)
                if dn in BRANCHES or dn in JUMPS or (dup >> 30) & 1:
                    raise Unsupported("control flow in E delay slot")
                self.emit_pair(pc + 8, out, decls)
                out.append("    c.endPc = 0x%04xu;" % ((pc + 16) & PC_MASK))
                out.append("    return vu1n::kEnd;")
                return decls, out, pc + 16
            pc += 8
            if pc in self.leaders or pc >= u["end"]:
                self.exit_bk(out, lod, old)
                out.append("    return 0x%04xu;" % (pc & PC_MASK))
                return decls, out, pc

    def exit_bk(self, out, lod, old):
        if old:
            out.append("    c.bkReg = %du;" % lod["vi_write"][0])
            out.append("    c.bkVal = %s;" % old)
        else:
            out.append("    c.bkReg = 0u;")


def translate(units, leaders, ilw_targets):
    """Returns list of (unit index, list of (start pc, end pc, function code or None, reason))."""
    result = []
    for uidx, u in enumerate(units):
        em = BlockEmitter(u, uidx, leaders[uidx], ilw_targets)
        blocks = []
        for start in sorted(leaders[uidx]):
            name = "b%02d_%04x" % (uidx, start)
            try:
                # Size the block first: stall and ready-store elision need its length.
                em.start, em.pairs = start, None
                em.reset_block_state()
                _, _, end = em.block(start)
                em.pairs = (end - start) // 8
                em.reset_block_state()
                decls, body, end = em.block(start)
                code = list(dict.fromkeys(decls)) + body
                fn = ["static uint32_t %s(vu1n::Ctx &c)" % name, "{"] + code + ["}"]
                blocks.append((start, end, fn, None))
            except Unsupported as e:
                fn = ["static uint32_t %s(vu1n::Ctx &)" % name, "{",
                      "    return vu1n::kBailFlag | 0x%04xu; // %s" % (start, e), "}"]
                blocks.append((start, start + 8, fn, str(e)))
        result.append(blocks)
    return result


def main():
    global BANKS, CODE_SIZE, PC_MASK, QADDR, RESERVED
    ap = argparse.ArgumentParser()
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ap.add_argument("--unit", choices=sorted(UNITS), default="vu1")
    ap.add_argument("--elf", default=os.path.join(root, "iso_extracted", "SLUS_207.32"))
    ap.add_argument("--out-dir", default=os.path.join(root, "src"))
    ap.add_argument("--files", type=int)
    args = ap.parse_args()
    cfg = UNITS[args.unit]
    BANKS = cfg["banks"]
    CODE_SIZE = cfg["code_size"]
    PC_MASK = CODE_SIZE - 1
    QADDR = cfg["qaddr"]
    RESERVED = cfg["reserved"]
    files = args.files or cfg["files"]

    elf = open(args.elf, "rb").read()
    units = extract_units(elf)
    leaders, ilw_targets = find_leaders(units)
    translated = translate(units, leaders, ilw_targets)

    total_blocks = sum(len(b) for b in translated)
    bails = [(u, s, r) for u, blocks in enumerate(translated) for s, e, f, r in blocks if r]
    print("units=%d pairs=%d blocks=%d untranslatable=%d" % (
        len(units), sum(len(u["words"]) // 2 for u in units), total_blocks, len(bails)))
    for u, s, r in bails:
        print("  unit %d (elf 0x%x) block 0x%04x: %s" % (u, units[u]["off"], s, r))

    per_file = (len(units) + files - 1) // files
    for f in range(files):
        lines = ["// Generated by tools/vu1_recomp.py from SLUS_207.32. Do not edit.",
                 '#include "runtime/ps2_vu1_native.h"', "", "namespace", "{"]
        descs = []
        for uidx in range(f * per_file, min(len(units), (f + 1) * per_file)):
            u = units[uidx]
            lines.append("    // unit %d: elf offset 0x%x, VU 0x%04x-0x%04x" % (uidx, u["off"], u["addr"], u["end"]))
            lines.append("    alignas(16) const uint32_t u%02d[] = {" % uidx)
            for i in range(0, len(u["words"]), 8):
                lines.append("        " + ", ".join("0x%08xu" % w for w in u["words"][i:i + 8]) + ",")
            lines.append("    };")
            for start, end, fn, reason in translated[uidx]:
                lines.extend("    " + l if l else l for l in fn)
                lines.append("")
                pairs = (end - start) // 8
                descs.append("        {0x%04xu, %du, u%02d + %d, b%02d_%04x}," % (
                    start, pairs, uidx, (start - u["addr"]) // 4, uidx, start))
        lines.append("    const vu1n::BlockDesc kBlocks[] = {")
        lines.extend(descs)
        lines.append("    };")
        lines.append("    const vu1n::Registrar kRegistrar(%s, kBlocks, sizeof(kBlocks) / sizeof(kBlocks[0]));" % cfg["unit"])
        lines.append("}")
        path = os.path.join(args.out_dir, "%s_programs_%d.cpp" % (args.unit, f))
        with open(path, "w", newline="\n") as fh:
            fh.write("\n".join(lines) + "\n")
        print("wrote", path)


if __name__ == "__main__":
    main()
