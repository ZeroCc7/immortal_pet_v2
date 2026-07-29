"""Convert cultivation PNG assets into LVGL raw pixel buffers.

The device reads these files directly from the TF card, avoiding runtime PNG
decoding in the LVGL task. Backgrounds become RGB565 little-endian; transparent
character and effect frames become LVGL ARGB8888 byte order (B, G, R, A).
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


DEFAULT_ROOT = Path("docs/images/sdcard/immortal_pet/scenes/cultivation")


def convert_rgb565(source: Path, destination: Path) -> None:
    image = Image.open(source).convert("RGB")
    if image.size != (480, 480):
        raise ValueError(f"{source} must be 480x480, got {image.size}")
    output = bytearray()
    for red, green, blue in image.getdata():
        value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        output.extend(value.to_bytes(2, "little"))
    destination.write_bytes(output)


def convert_argb8888(source: Path, destination: Path) -> None:
    image = Image.open(source).convert("RGBA")
    if image.size != (256, 256):
        raise ValueError(f"{source} must be 256x256, got {image.size}")
    destination.write_bytes(image.tobytes("raw", "BGRA"))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    args = parser.parse_args()
    root = args.root.resolve()

    for name in ("background_day", "background_night"):
        convert_rgb565(root / f"{name}.png", root / f"{name}.rgb565")

    for folder, count in (("female", 6), ("male", 6), ("enlightenment", 4)):
        for index in range(1, count + 1):
            convert_argb8888(root / folder / f"frame-{index}.png",
                             root / folder / f"frame-{index}.argb8888")

    print(f"Converted cultivation assets under {root}")


if __name__ == "__main__":
    main()
