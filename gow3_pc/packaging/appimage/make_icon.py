"""The AppImage's icon (AppDir/tlou-remastered.png, 256x256).

From the player's own game (sce_sys/icon0.png), found in the order:
  1. TLOU_APPIMAGE_ICON_GAME=<folder>,
  2. the launcher's saved game_folder (analysis/tlou-user/launcher.json),
  3. /roms-ps4/Roms/PS4/The Last of Us Remastered/CUSA00552 (this port's usual place).
Without the game a neutral placeholder is generated, so a public AppImage (no game file,
tools/package_check.py's rule) can still be built.

usage: make_icon.py <output.png>
"""
import json
import os
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

CANDIDATES = (
    os.environ.get("TLOU_APPIMAGE_ICON_GAME", ""),
    "",   # the launcher's saved folder (below)
    "/roms-ps4/Roms/PS4/The Last of Us Remastered/CUSA00552",
)


def launcher_game():
    try:
        return json.loads((Path(__file__).resolve().parents[3] / "analysis/tlou-user/launcher.json")
                          .read_text())["game_folder"]
    except (OSError, ValueError, KeyError):
        return ""


def game_icon():
    for folder in (c for c in CANDIDATES if c):
        icon = Path(folder) / "sce_sys/icon0.png"
        if icon.is_file():
            return icon
    return None


def square(image, size):
    image = image.convert("RGBA")
    width, height = image.size
    side = min(width, height)
    image = image.crop(((width - side) // 2, (height - side) // 2,
                        (width + side) // 2, (height + side) // 2))
    return image.resize((size, size), Image.LANCZOS)


def placeholder(size):
    image = Image.new("RGBA", (size, size), (13, 24, 34, 255))
    draw = ImageDraw.Draw(image)
    font = None
    for name in ("DejaVuSans-Bold.ttf", "DejaVuSans.ttf"):
        for base in ("/usr/share/fonts/dejavu/", "/usr/share/fonts/TTF/"):
            path = base + name
            if Path(path).is_file():
                font = ImageFont.truetype(path, size // 3)
                break
        if font:
            break
    text = "TLOU"
    box = draw.textbbox((0, 0), text, font=font)
    draw.text(((size - (box[2] - box[0])) // 2 - box[0],
               (size - (box[3] - box[1])) // 2 - box[1]),
              text, fill=(214, 222, 228, 255), font=font)
    return image


def main():
    output = Path(sys.argv[1])
    source = game_icon()
    if source is None:
        print("No game icon (set TLOU_APPIMAGE_ICON_GAME): a neutral placeholder instead")
        image = placeholder(256)
    else:
        print(f"Icon from {source}")
        image = square(Image.open(source), 256)
    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output)


if __name__ == "__main__":
    main()
