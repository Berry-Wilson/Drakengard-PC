#!/usr/bin/env python3
"""Make the launcher's Windows icon from the game's memory card icon.

The game writes dradra.ico to its save folder: a PS2 icon, which is a flat square model with a
128x128 RGB5551 texture at the end of the file. This takes the crest from that texture and writes
launcher/res/drakengard.ico. The result is the game's art, so git ignores it.

Usage: python tools/make_launcher_icon.py [path/to/dradra.ico]
Needs Pillow. Rebuild the launcher afterwards.
"""
import struct
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SOURCE = ROOT / "iso_extracted" / "mc0" / "BASLUS-20732" / "dradra.ico"
TARGET = ROOT / "launcher" / "res" / "drakengard.ico"
TEXTURE_SIDE = 128
TEXTURE_BYTES = TEXTURE_SIDE * TEXTURE_SIDE * 2
PS2_ICON_MAGIC = 0x00010000
SIZES = [16, 24, 32, 48, 64, 128, 256]
WORK_SIDE = 1024
MARGIN = 0.03


def read_texture(path):
    data = path.read_bytes()
    if len(data) < TEXTURE_BYTES + 20 or struct.unpack_from("<I", data)[0] != PS2_ICON_MAGIC:
        sys.exit(f"{path} isn't an uncompressed PS2 icon")
    pixels = struct.unpack(f"<{TEXTURE_SIDE * TEXTURE_SIDE}H", data[-TEXTURE_BYTES:])
    return [((v & 31) * 255 // 31, (v >> 5 & 31) * 255 // 31, (v >> 10 & 31) * 255 // 31) for v in pixels]


def crest_image(pixels):
    """The crest on a transparent square: its color comes from the texture, its shape from brightness."""
    color = max(pixels, key=sum)
    mask = Image.new("L", (TEXTURE_SIDE, TEXTURE_SIDE))
    mask.putdata([min(255, sum(p) * 255 // max(1, sum(color))) for p in pixels])

    # Smooth the 128-pixel stair steps, then sharpen the edge again.
    mask = mask.crop(mask.getbbox()).resize((WORK_SIDE, WORK_SIDE * mask.height // mask.width), Image.BICUBIC)
    mask = mask.point(lambda v: max(0, min(255, (v - 96) * 4)))

    side = int(max(mask.size) * (1 + 2 * MARGIN))
    alpha = Image.new("L", (side, side))
    alpha.paste(mask, ((side - mask.width) // 2, (side - mask.height) // 2))
    image = Image.new("RGBA", (side, side), color + (0,))
    image.putalpha(alpha)
    return image


def main():
    source = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_SOURCE
    if not source.is_file():
        sys.exit(f"{source} wasn't found. Start the game and save once, or pass the path.")
    TARGET.parent.mkdir(parents=True, exist_ok=True)
    crest = crest_image(read_texture(source))
    crest.resize((256, 256), Image.LANCZOS).save(TARGET, format="ICO", sizes=[(s, s) for s in SIZES])
    print(f"{TARGET} ({TARGET.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
