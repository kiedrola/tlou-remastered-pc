"""The prompt pictures dumped with GOW3_PROMPTS_DUMP=1 (gow3_prompts.cpp DumpPrompt) as PNGs.

Each analysis/tlou-user/prompt-dump/<hash>.bin holds the HUD sprite shader's colour (BC1) and
alpha (BC4) textures, their descriptors, the quad and the known_prompts answer. This decodes them
(linear or 1D-thin tiled) and writes <out>/<hash>.png (colour with alpha) and a contact sheet
(sheet.png, the answer and quad size under each). Needs Pillow.

    python tools/prompt_dump_view.py [dump folder] [out folder]
"""
from pathlib import Path
import struct
import sys

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
RULES = {-1: "-", 0: "X", 1: "O", 2: "[]", 3: "/\\", 4: "L1", 5: "R1", 8: "L3"}


def descriptor(raw):
    q = struct.unpack("<4Q", raw)
    return {
        "address": (q[0] & ((1 << 38) - 1)) << 8,
        "data_format": (q[0] >> 52) & 0x3F,
        "width": ((q[1]) & 0x3FFF) + 1,
        "height": ((q[1] >> 14) & 0x3FFF) + 1,
        "tiling_index": (q[1] >> 52) & 0x1F,
        "pitch": ((q[2] >> 13) & 0x3FFF) + 1,
    }


def block_order(blocks_w, blocks_h, tiling):
    """(bx, by) of each 8-byte block in memory order: linear, or 1D-thin (8x8 blocks per tile, Z order)."""
    if tiling != 13:   # linear (8) and anything else read as linear for a first look
        return [(x, y) for y in range(blocks_h) for x in range(blocks_w)]
    order = []
    for ty in range(0, blocks_h, 8):
        for tx in range(0, blocks_w, 8):
            for e in range(64):
                x = sum(((e >> (2 * b)) & 1) << b for b in range(3))
                y = sum(((e >> (2 * b + 1)) & 1) << b for b in range(3))
                order.append((tx + x, ty + y))
    return order


def bc1_block(b):
    c0, c1, bits = struct.unpack("<HHI", b)

    def rgb(c):
        return ((c >> 11) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)
    p0, p1 = rgb(c0), rgb(c1)
    if c0 > c1:
        pal = [p0, p1, tuple((2 * a + b) // 3 for a, b in zip(p0, p1)), tuple((a + 2 * b) // 3 for a, b in zip(p0, p1))]
    else:
        pal = [p0, p1, tuple((a + b) // 2 for a, b in zip(p0, p1)), (0, 0, 0)]
    return [pal[(bits >> (2 * i)) & 3] for i in range(16)]


def bc4_block(b):
    a0, a1 = b[0], b[1]
    bits = int.from_bytes(b[2:8], "little")
    if a0 > a1:
        pal = [a0, a1] + [((6 - i) * a0 + (i + 1) * a1) // 7 for i in range(6)]
    else:
        pal = [a0, a1] + [((4 - i) * a0 + (i + 1) * a1) // 5 for i in range(4)] + [0, 255]
    return [pal[(bits >> (3 * i)) & 7] for i in range(16)]


def decode(data, desc, decoder, mode):
    w, h = desc["width"], desc["height"]
    bw, bh = (max(desc["pitch"], w) + 3) // 4, (h + 3) // 4
    img = Image.new(mode, (bw * 4, bh * 4))
    px = img.load()
    for i, (bx, by) in enumerate(block_order(bw, bh, desc["tiling_index"])):
        if (i + 1) * 8 > len(data):
            break
        for j, value in enumerate(decoder(data[i * 8:(i + 1) * 8])):
            px[bx * 4 + j % 4, by * 4 + j // 4] = value
    return img.crop((0, 0, w, h))


def main():
    folder = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "analysis/tlou-user/prompt-dump"
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else folder / "png"
    out.mkdir(parents=True, exist_ok=True)
    tiles = []
    for path in sorted(folder.glob("*.bin")):
        raw = path.read_bytes()
        magic, version, size0, size1 = struct.unpack_from("<4I", raw)
        if magic != 0x44504C54:
            continue
        d0, d1 = descriptor(raw[16:48]), descriptor(raw[48:80])
        quad = struct.unpack_from("<10f", raw, 80)
        rule, = struct.unpack_from("<i", raw, 120)
        colour = raw[124:124 + size0]
        alpha = raw[124 + size0:124 + size0 + size1]
        rgb = decode(colour, d0, bc1_block, "RGB")
        a = decode(alpha, d1, bc4_block, "L")
        picture = rgb.convert("RGBA")
        picture.putalpha(a.resize(rgb.size))
        picture.save(out / f"{path.stem}.png")
        tiles.append((path.stem, picture, a, rule, quad, d0, d1))
        print(f"{path.stem}: colour fmt {d0['data_format']} tile {d0['tiling_index']} {d0['width']}x{d0['height']} "
              f"pitch {d0['pitch']}, alpha fmt {d1['data_format']} tile {d1['tiling_index']}, "
              f"quad {quad[2]:.0f}x{quad[3]:.0f} at {quad[0]:.0f},{quad[1]:.0f}, answer {RULES.get(rule, rule)}")
    if not tiles:
        print("No dumps in", folder)
        return
    columns = 8
    cell = 140
    sheet = Image.new("RGB", (columns * cell * 2, ((len(tiles) + columns - 1) // columns) * (cell + 16)), (40, 40, 40))
    draw = ImageDraw.Draw(sheet)
    for i, (name, picture, a, rule, quad, _, _) in enumerate(tiles):
        x, y = (i % columns) * cell * 2, (i // columns) * (cell + 16)
        back = Image.new("RGBA", picture.size, (40, 40, 40, 255))
        sheet.paste(Image.alpha_composite(back, picture).convert("RGB"), (x + 4, y + 4))
        sheet.paste(a.convert("RGB"), (x + cell + 4, y + 4))
        draw.text((x + 4, y + cell - 4), f"{name[:8]} {RULES.get(rule, rule)} {quad[2]:.0f}px", fill=(230, 230, 230))
    sheet.save(out / "sheet.png")
    print(f"{len(tiles)} pictures; contact sheet {out / 'sheet.png'}")


if __name__ == "__main__":
    main()
