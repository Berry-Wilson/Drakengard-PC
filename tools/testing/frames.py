"""Look at and compare frame dumps (PS2X_DUMP_FRAMES_DIR, or F12 screenshots). Needs Pillow.

  frames.py sheet <folder> <out.png> [--cols 6]       contact sheet, each frame labelled
  frames.py diff <folder A> <folder B> [--level 40]   which same-named frames differ, and by how much
  frames.py pair <4:3 image> <16:9 image> <out.png>   side by side at equal height
  frames.py crop <4:3 image> <16:9 image> <out.png> X Y W H
        the same part of the scene from both, at equal scale; X Y W H are fractions of the 4:3 picture
"""
import argparse
import glob
import os

from PIL import Image, ImageChops, ImageDraw


def frames_in(folder):
    return sorted(glob.glob(os.path.join(folder, "*.png")))


def load(path):
    try:
        return Image.open(path).convert("RGB")
    except OSError:
        return None  # a dump cut short when the game was stopped


def sheet(args):
    files = frames_in(args.folder)
    width, height, label = 320, 180, 14
    rows = (len(files) + args.cols - 1) // args.cols
    out = Image.new("RGB", (args.cols * width, rows * (height + label)), "black")
    draw = ImageDraw.Draw(out)
    for i, path in enumerate(files):
        image = load(path)
        x, y = (i % args.cols) * width, (i // args.cols) * (height + label)
        if image:
            out.paste(image.resize((width, height)), (x, y + label))
        draw.text((x + 2, y), os.path.splitext(os.path.basename(path))[0], fill="yellow")
    out.save(args.out)
    print(f"{len(files)} frames -> {args.out}")


def diff(args):
    same = 0
    for path in frames_in(args.a):
        name = os.path.basename(path)
        other = os.path.join(args.b, name)
        if not os.path.exists(other):
            continue
        a, b = load(path), load(other)
        if a is None or b is None:
            continue
        if a.size != b.size:
            print(f"{name}: sizes differ, {a.size} and {b.size}")
            continue
        delta = ImageChops.difference(a.convert("L"), b.convert("L"))
        box = delta.getbbox()
        if box is None:
            same += 1
            continue
        over = sum(1 for p in delta.get_flattened_data() if p > args.level) if hasattr(delta, "get_flattened_data") \
            else sum(1 for p in delta.getdata() if p > args.level)
        print(f"{name}: differs in {box}, {over} pixels by more than {args.level}")
    print(f"{same} frames identical")


def pair(args):
    a, b = load(args.a), load(args.b)
    height = 480
    a = a.resize((height * 4 // 3, height))
    b = b.resize((height * 16 // 9, height))
    out = Image.new("RGB", (a.width + b.width + 8, height), "white")
    out.paste(a, (0, 0))
    out.paste(b, (a.width + 8, 0))
    out.save(args.out)


def crop(args):
    a, b = load(args.a), load(args.b)
    height, narrow, wide = 960, 1280, 1707
    a, b = a.resize((narrow, height)), b.resize((wide, height))
    left, top = int(args.x * narrow), int(args.y * height)
    right, bottom = int((args.x + args.w) * narrow), int((args.y + args.h) * height)
    offset = (wide - narrow) // 2  # the 4:3 picture's place in the 16:9 one, for things at screen centre
    part_a = a.crop((left, top, right, bottom))
    part_b = b.crop((offset + left, top, offset + right, bottom))
    out = Image.new("RGB", (part_a.width * 2 + 8, part_a.height), "white")
    out.paste(part_a, (0, 0))
    out.paste(part_b, (part_a.width + 8, 0))
    out.save(args.out)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("sheet"); p.add_argument("folder"); p.add_argument("out"); p.add_argument("--cols", type=int, default=6); p.set_defaults(run=sheet)
    p = sub.add_parser("diff"); p.add_argument("a"); p.add_argument("b"); p.add_argument("--level", type=int, default=40); p.set_defaults(run=diff)
    p = sub.add_parser("pair"); p.add_argument("a"); p.add_argument("b"); p.add_argument("out"); p.set_defaults(run=pair)
    p = sub.add_parser("crop"); p.add_argument("a"); p.add_argument("b"); p.add_argument("out")
    for name in ("x", "y", "w", "h"):
        p.add_argument(name, type=float)
    p.set_defaults(run=crop)
    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
