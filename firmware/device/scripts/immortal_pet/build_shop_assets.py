#!/usr/bin/env python3
"""Prepare generated weapon-shop art for the SD-card LVGL runtime."""
from __future__ import annotations

from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[4]
SOURCE = ROOT / "docs/images/raw/shop"
OUTPUT = ROOT / "docs/images/sdcard/immortal_pet/shop"
BACKGROUND = SOURCE / "shop_background_source.png"
UI_ATLAS = SOURCE / "shop_ui_atlas_source.png"
REALM_ATLAS = SOURCE / "shop_realm_atlas_source.png"
PRIMARY_BUTTON = SOURCE / "shop_primary_button_source.png"
CONTROL_ATLAS = SOURCE / "shop_controls_source.png"


def save_rgb565(image: Image.Image, output: Path) -> None:
    image = image.convert("RGB")
    data = bytearray()
    for red, green, blue in image.getdata():
        value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        data.extend((value & 0xFF, value >> 8))
    output.write_bytes(data)


def remove_magenta(image: Image.Image) -> Image.Image:
    image = image.convert("RGBA")
    pixels = image.load()
    for y in range(image.height):
        for x in range(image.width):
            red, green, blue, alpha = pixels[x, y]
            if red > 220 and blue > 160 and green < 100:
                pixels[x, y] = (red, green, blue, 0)
    return image


def crop_cell(atlas: Image.Image, column: int, row: int, columns: int, rows: int) -> Image.Image:
    width = atlas.width // columns
    height = atlas.height // rows
    return atlas.crop((column * width, row * height, (column + 1) * width, (row + 1) * height))


def main() -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    background = Image.open(BACKGROUND).resize((480, 480), Image.Resampling.LANCZOS)
    save_rgb565(background, OUTPUT / "shop_background.rgb565")

    ui = remove_magenta(Image.open(UI_ATLAS))
    for name, column, row in (
        ("shop_card.png", 0, 0),
        ("shop_selected.png", 1, 0),
        ("shop_locked.png", 0, 1),
        ("shop_equipped.png", 1, 1),
    ):
        target_size = (220, 116) if "card" in name or "selected" in name else (64, 64)
        crop_cell(ui, column, row, 2, 2).resize(target_size, Image.Resampling.LANCZOS).save(
            OUTPUT / name
        )

    realms = remove_magenta(Image.open(REALM_ATLAS))
    for index in range(8):
        crop_cell(realms, index % 2, index // 2, 2, 4).resize(
            (64, 64), Image.Resampling.LANCZOS
        ).save(
            OUTPUT / f"shop_realm_{index + 1:02d}.png"
        )

    primary = remove_magenta(Image.open(PRIMARY_BUTTON))
    primary.crop(primary.getbbox()).resize((280, 50), Image.Resampling.LANCZOS).save(
        OUTPUT / "shop_primary_button.png"
    )
    controls = remove_magenta(Image.open(CONTROL_ATLAS))
    for name, column in (("shop_previous.png", 0), ("shop_next.png", 1), ("shop_back.png", 2)):
        crop_cell(controls, column, 0, 3, 1).resize((58, 48), Image.Resampling.LANCZOS).save(
            OUTPUT / name
        )


if __name__ == "__main__":
    main()
