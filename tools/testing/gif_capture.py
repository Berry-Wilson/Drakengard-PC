"""Read a PS2X_GIF_CAPTURE file: the GIF packets the game sent to the GS over a few vsyncs.

Capture with PS2X_GIF_CAPTURE=<file>, PS2X_GIF_CAPTURE_AT=<seconds> and PS2X_GIF_CAPTURE_VSYNCS=<n>
(up to about 4 vsyncs does not disturb a replay's timing).

  gif_capture.py tags <capture> [--limit 80]
        every GIF tag in order, with the transfer and frame registers it sets
  gif_capture.py draws <capture> [--vsync N] [--all] [--overlay frame.png out.png]
        one line per GIF tag that draws: path, PRIM, vertex count, depth, and its box on screen.
        Without --all only tags whose vertices share one depth are listed (2D and flat sprites);
        a summary by path, PRIM and kind comes first. --overlay outlines them on a 640x480 copy
        of a frame dump.

Screen coordinates assume the game's usual XYOFFSET of (0x6C00, 0x7100).
"""
import argparse
import struct
from collections import Counter

REGISTERS = {0x00: "PRIM", 0x06: "TEX0_1", 0x18: "XYOFFSET_1", 0x40: "SCISSOR_1", 0x4C: "FRAME_1", 0x4D: "FRAME_2",
             0x50: "BITBLTBUF", 0x51: "TRXPOS", 0x52: "TRXREG", 0x53: "TRXDIR"}
SCREEN_Z = {65535, 16777215, 65000, 50000, 49999}  # the game's fixed 2D layers
X_OFFSET, Y_OFFSET = 0x6C00, 0x7100


def records(path):
    """Yields (gif path 1-3, vsync, packet bytes)."""
    data = open(path, "rb").read()
    at = 0
    while at + 16 <= len(data):
        gif_path, size = struct.unpack_from("<II", data, at)
        vsync = struct.unpack_from("<Q", data, at + 8)[0]
        yield gif_path, vsync, data[at + 16:at + 16 + size]
        at += 16 + size


def tags(packet):
    """Yields (format, nloop, nreg, registers set by A+D, prim or None, vertices [(x, y, z)])."""
    at = 0
    while at + 16 <= len(packet):
        lo, hi = struct.unpack_from("<QQ", packet, at)
        at += 16
        nloop, fmt, nreg = lo & 0x7FFF, (lo >> 58) & 3, (lo >> 60) & 0xF or 16
        prim = (lo >> 47) & 0x7FF if (lo >> 46) & 1 else None
        set_regs, vertices = [], []

        def put(reg, value):
            nonlocal prim
            if reg in (0x05, 0x0D):
                vertices.append((value & 0xFFFF, (value >> 16) & 0xFFFF, value >> 32))
            elif reg in (0x04, 0x0C):
                vertices.append((value & 0xFFFF, (value >> 16) & 0xFFFF, (value >> 32) & 0xFFFFFF))
            elif reg == 0x00:
                prim = value & 0x7FF
            if reg in REGISTERS:
                set_regs.append((REGISTERS[reg], value))

        if fmt == 0:  # PACKED
            for i in range(nloop * nreg):
                if at + 16 > len(packet):
                    break
                desc = (hi >> ((i % nreg) * 4)) & 0xF
                w = struct.unpack_from("<4I", packet, at)
                q0, q1 = struct.unpack_from("<QQ", packet, at)
                at += 16
                if desc == 0xE:
                    put(q1 & 0xFF, q0)
                elif desc in (0x5, 0xD):
                    vertices.append((w[0] & 0xFFFF, w[1] & 0xFFFF, w[2]))
                elif desc in (0x4, 0xC):
                    vertices.append((w[0] & 0xFFFF, w[1] & 0xFFFF, (w[2] >> 4) & 0xFFFFFF))
                elif desc == 0x0:
                    prim = w[0] & 0x7FF
        elif fmt == 1:  # REGLIST
            words = nloop * nreg
            for k in range(words):
                if at + k * 8 + 8 > len(packet):
                    break
                put((hi >> ((k % nreg) * 4)) & 0xF, struct.unpack_from("<Q", packet, at + k * 8)[0])
            at += (words + 1) // 2 * 16
        else:  # IMAGE
            at += nloop * 16
        yield ("PACKED", "REGLIST", "IMAGE", "IMAGE")[fmt], nloop, nreg, set_regs, prim, vertices


def show_tags(args):
    shown = 0
    for gif_path, vsync, packet in records(args.capture):
        print(f"== path {gif_path}, vsync {vsync}, {len(packet)} bytes")
        for fmt, nloop, nreg, set_regs, _prim, _vertices in tags(packet):
            regs = " ".join(f"{name}={value:x}" for name, value in set_regs)
            print(f"  {fmt} nloop {nloop} nreg {nreg} {regs}"[:220])
            shown += 1
            if shown >= args.limit:
                return


def show_draws(args):
    recs = list(records(args.capture))
    vsyncs = sorted({v for _p, v, _d in recs})
    want = args.vsync if args.vsync is not None else vsyncs[-1]
    print(f"vsyncs {vsyncs}, showing {want}; packets by path {dict(Counter(p for p, v, _d in recs if v == want))}")

    image = draw = None
    if args.overlay:
        from PIL import Image, ImageDraw
        image = Image.open(args.overlay[0]).convert("RGB").resize((640, 480))
        draw = ImageDraw.Draw(image)

    prims = {1: 0, 2: 0, 3: 0}
    rows = []
    for gif_path, vsync, packet in recs:
        if vsync != want:
            continue
        for _fmt, _nloop, _nreg, _regs, prim, vertices in tags(packet):
            if prim is not None:
                prims[gif_path] = prim
            if not vertices:
                continue
            prim = prims[gif_path]
            depths = {z for _x, _y, z in vertices}
            xs = [(x - X_OFFSET) / 16 for x, _y, _z in vertices]
            ys = [(y - Y_OFFSET) / 16 for _x, y, _z in vertices]
            flat = len(depths) == 1
            kind = "2D" if flat and vertices[0][2] in SCREEN_Z else "flat" if flat else "3D"
            depth = vertices[0][2] if flat else (min(depths), max(depths))
            box = (round(min(xs)), round(min(ys)), round(max(xs)), round(max(ys)))
            rows.append((gif_path, prim, len(vertices), kind, depth, box))
            if draw and flat:
                draw.rectangle(box, outline={"2D": "lime", "flat": "red"}[kind])

    for (gif_path, prim, kind), count in sorted(Counter((r[0], r[1], r[3]) for r in rows).items()):
        print(f"  path {gif_path} prim {prim:#x} {kind}: {count} tags")
    for gif_path, prim, count, kind, depth, box in rows:
        if args.all or kind != "3D":
            print(f"path {gif_path} prim {prim:#x} verts {count} {kind} z {depth} box {box}")
    if image:
        image.save(args.overlay[1])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("tags"); p.add_argument("capture"); p.add_argument("--limit", type=int, default=80); p.set_defaults(run=show_tags)
    p = sub.add_parser("draws"); p.add_argument("capture"); p.add_argument("--vsync", type=int)
    p.add_argument("--all", action="store_true"); p.add_argument("--overlay", nargs=2, metavar=("FRAME", "OUT")); p.set_defaults(run=show_draws)
    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
