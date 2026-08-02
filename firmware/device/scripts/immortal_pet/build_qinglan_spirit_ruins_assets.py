#!/usr/bin/env python3
"""Build the Qinglan Spirit Ruins TF-card assets from the user-owned PNGs."""

from __future__ import annotations

import json
import shutil
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont


ROOT = Path(__file__).resolve().parents[4]
RAW = ROOT / "docs/images/raw"
OUTPUT = ROOT / "docs/images/sdcard/immortal_pet/journey/qinglan_spirit_ruins"
ACTORS = {
    "female": RAW / "all_女火/characters/all_女火/base/07004/actions",
    "male": RAW / "all_男火/characters/all_男火/base/06004/actions",
    "willow_wraith": RAW / "历练/青岚灵墟/怪/柳鬼",
    "peach_treant": RAW / "历练/青岚灵墟/怪/桃精",
    "azure_dragon": RAW / "历练/青岚灵墟/怪/青龙",
}
ACTION_NAMES = ("stand", "attack", "defense", "die")
PLAYER_PROFILES = (
    ("female_fire", "\u5973", "\u706b", "07004"),
    ("female_metal", "\u5973", "\u91d1", "07001"),
    ("male_fire", "\u7537", "\u706b", "06004"),
    ("male_metal", "\u7537", "\u91d1", "06001"),
)
SHOP_WEAPON_IDS = {
    "fire": tuple(f"{value:05d}" for value in range(1101, 1112)),
    "metal": tuple(f"{value:05d}" for value in range(1134, 1145)),
}
PLAYER_DIRECTIONS = {
    "stand": 1,
    "attack": 0,
    "defense": 0,
    "die": 0,
}
MONSTER_DIRECTIONS = {
    "stand": 2,
    "attack": 2,
    "defense": 2,
    "die": 2,
}
FRAME_SIZE = 160
TARGET_FRAME_COUNT = 8
UI_FONT = Path("C:/Windows/Fonts/msyhbd.ttc")
SELECTION_SCREEN = OUTPUT.parent / "selection_screen.rgb565"


def rgb565(source: Path, destination: Path) -> None:
    image = Image.open(source).convert("RGB")
    rgb565_image(image, destination)


def rgb565_image(image: Image.Image, destination: Path) -> None:
    if image.size != (480, 480):
        raise ValueError(f"{destination} must be 480x480")
    pixels = bytearray()
    for red, green, blue in image.getdata():
        value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        pixels.extend(value.to_bytes(2, "little"))
    destination.write_bytes(pixels)


def read_rgb565(source: Path) -> Image.Image:
    data = source.read_bytes()
    if len(data) != 480 * 480 * 2:
        raise ValueError(f"{source} is not a 480x480 RGB565 image")
    pixels = []
    for offset in range(0, len(data), 2):
        value = int.from_bytes(data[offset:offset + 2], "little")
        pixels.append(((value >> 11 & 0x1F) * 255 // 31,
                       (value >> 5 & 0x3F) * 255 // 63,
                       (value & 0x1F) * 255 // 31))
    image = Image.new("RGB", (480, 480))
    image.putdata(pixels)
    return image


def refresh_selection_screen() -> None:
    """Repaint mutable labels on the checked-in SD-card selection image."""
    screen = read_rgb565(SELECTION_SCREEN)
    softened = screen.filter(ImageFilter.MedianFilter(9))
    erase = Image.new("L", screen.size)
    source_pixels = screen.load()
    softened_pixels = softened.load()
    erase_pixels = erase.load()
    # Only remove high-contrast gold/dark glyph pixels.  Blurring whole label
    # rectangles damages the ornamental circles and button borders beneath them.
    for left, top, right, bottom in ((90, 292, 390, 331), (174, 326, 306, 355),
                                     (98, 383, 208, 411), (268, 383, 382, 411)):
        for y in range(top, bottom + 1):
            for x in range(left, right + 1):
                red, green, blue = source_pixels[x, y]
                smooth_red, smooth_green, smooth_blue = softened_pixels[x, y]
                contrast = abs(red - smooth_red) + abs(green - smooth_green) + abs(blue - smooth_blue)
                gold = red > 135 and green > 115 and blue < 225 and red >= green
                outline = red < 105 and green < 125 and blue < 125
                if contrast > 48 and (gold or outline):
                    erase_pixels[x, y] = 255
    screen.paste(softened, mask=erase)
    draw = ImageDraw.Draw(screen)
    button_font = ImageFont.truetype(str(UI_FONT), 19)
    energy_font = ImageFont.truetype(str(UI_FONT), 17)

    for x, name in ((143, "\u67f3\u9b3c"), (240, "\u6843\u7cbe"), (337, "\u9752\u9f99")):
        draw.text((x, 321), name, font=button_font, fill="#f8e8b5", anchor="mm",
                  stroke_width=1, stroke_fill="#1d4a42")
    draw.text((240, 348), "\u6d88\u8017\u7cbe\u529b  1", font=energy_font, fill="#6c5141", anchor="mm")
    draw.text((150, 393), "\u524d\u5f80\u5386\u7ec3", font=button_font, fill="#f8e8b5", anchor="mm",
              stroke_width=1, stroke_fill="#1d4a42")
    draw.text((330, 393), "\u8fd4\u56de\u6d1e\u5e9c", font=button_font, fill="#f8e8b5", anchor="mm",
              stroke_width=1, stroke_fill="#1d4a42")
    rgb565_image(screen, SELECTION_SCREEN)


def build_selection_screen(destination: Path) -> None:
    background = Image.open(RAW / "历练/历练选择背景.png").convert("RGBA")
    frame = Image.open(RAW / "历练/历练选择UI框架透明.png").convert("RGBA")
    background.alpha_composite(frame.resize((480, 480), Image.Resampling.LANCZOS))
    draw = ImageDraw.Draw(background)

    def font(size: int) -> ImageFont.FreeTypeFont:
        return ImageFont.truetype(str(UI_FONT), size)

    def centered(text: str, y: int, size: int, fill: str, stroke: int = 0) -> None:
        draw.text((240, y), text, font=font(size), fill=fill, anchor="ma",
                  stroke_width=stroke, stroke_fill="#183d39")

    centered("\u70bc\u6c14\u5386\u7ec3", 70, 30, "#f6dda0", 1)
    centered("\u9752\u5c9a\u7075\u589f", 119, 25, "#315d55")
    centered("\u7b2c\u4e00\u91cd\u8bd5\u70bc", 160, 19, "#52776c")
    draw.text((240, 301), "\u67f3\u9b3c   \u00b7   \u6843\u7cbe   \u00b7   \u9752\u9f99", font=font(19),
              fill="#f8e8b5", anchor="mm", stroke_width=1, stroke_fill="#1d4a42")
    draw.text((240, 337), "\u6d88\u8017\u7cbe\u529b  1", font=font(17), fill="#6c5141", anchor="mm")
    draw.text((155, 393), "\u524d\u5f80\u5386\u7ec3", font=font(19), fill="#f8e8b5", anchor="mm",
              stroke_width=1, stroke_fill="#1d4a42")
    draw.text((325, 393), "\u8fd4\u56de\u6d1e\u5e9c", font=font(19), fill="#f8e8b5", anchor="mm",
              stroke_width=1, stroke_fill="#1d4a42")
    rgb565_image(background.convert("RGB"), destination)


def export_actor(name: str, source: Path,
                 actions: tuple[str, ...] = ACTION_NAMES) -> None:
    destination = OUTPUT / "actors" / name
    destination.mkdir(parents=True, exist_ok=True)
    metadata: dict[str, object] = {}
    for action in actions:
        action_root = source / "actions" / action if (source / "actions").is_dir() else source / action
        animation = action_root / "animation.json"
        playback = action_root / "playback.json"
        if not animation.is_file() or not playback.is_file():
            raise ValueError(f"missing {action} metadata for {name}")
        canvas = json.loads(animation.read_text(encoding="utf-8"))["canvas"]
        directions = json.loads(playback.read_text(encoding="utf-8"))["directions"]
        is_player = name in ("female", "male") or name.startswith(("female_", "male_"))
        direction_index = (PLAYER_DIRECTIONS if is_player else MONSTER_DIRECTIONS)[action]
        direction = next((item for item in directions if item["index"] == direction_index),
                         directions[0])
        source_frames = direction["frames"]
        if not source_frames:
            raise ValueError(f"empty {action} animation for {name}")
        if action == "stand" and len(source_frames) < TARGET_FRAME_COUNT:
            # A short stand sequence must loop, not freeze on its final frame.
            # Journey uses these eight frames for the monster's entrance shot.
            selected_indices = [index % len(source_frames) for index in range(TARGET_FRAME_COUNT)]
        elif len(source_frames) <= TARGET_FRAME_COUNT:
            selected_indices = list(range(len(source_frames)))
        else:
            selected_indices = [
                round(index * (len(source_frames) - 1) / (TARGET_FRAME_COUNT - 1))
                for index in range(TARGET_FRAME_COUNT)
            ]
        frames = []
        for output_index, source_index in enumerate(selected_indices):
            frame = source_frames[source_index]
            image = Image.open(action_root / frame["file"]).convert("RGBA")
            normalized = Image.new("RGBA", (256, 256))
            normalized.alpha_composite(image, (frame["x"], frame["y"]))
            normalized = normalized.resize((FRAME_SIZE, FRAME_SIZE), Image.Resampling.LANCZOS)
            target = destination / action / f"frame-{output_index:03d}.argb8888"
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(normalized.tobytes("raw", "BGRA"))
            frames.append(target.relative_to(destination).as_posix())
        while len(frames) < TARGET_FRAME_COUNT:
            target = destination / action / f"frame-{len(frames):03d}.argb8888"
            shutil.copy2(destination / frames[-1], target)
            frames.append(target.relative_to(destination).as_posix())
        source_interval = json.loads(playback.read_text(encoding="utf-8")).get(
            "frame_interval_ms", 100)
        metadata[action] = {
            "frame_interval_ms": round(source_interval * len(source_frames) / len(frames)),
            "frames": frames,
            "canvas": canvas,
        }
    (destination / "actor.json").write_text(json.dumps(metadata, separators=(",", ":")), encoding="utf-8")


def export_player_appearances() -> int:
    """Export every selectable body so battle uses the equipped suit."""
    exported = 0
    for family, gender, element, base_name in PLAYER_PROFILES:
        label = f"all_{gender}{element}"
        character_root = RAW / label / "characters" / label
        sources = {base_name: character_root / "base" / base_name}
        for group in ("without_weapon", "with_weapon"):
            for suit in sorted((character_root / "suits" / group).glob("*")):
                if suit.is_dir():
                    # High-tier with-weapon bodies replace the same ids from
                    # without_weapon, matching the layered-idle catalog.
                    sources[suit.name] = suit
        for body_name, source in sorted(sources.items()):
            # Player battle shots only request these two actions. Avoid adding
            # unused stand/die copies for every shop appearance to the SD card.
            export_actor(f"{family}_{body_name}", source, ("attack", "defense"))
            exported += 1
    return exported


def export_player_weapons() -> int:
    """Export attack layers separately so body and weapon stay independent."""
    exported = 0
    for family, gender, element, base_name in PLAYER_PROFILES:
        label = f"all_{gender}{element}"
        character_root = RAW / label / "characters" / label
        weapon_family = "metal" if family.endswith("metal") else "fire"
        for weapon_name in SHOP_WEAPON_IDS[weapon_family]:
            source = character_root / "weapons" / base_name / weapon_name
            if not source.is_dir():
                raise ValueError(f"missing shop weapon source: {source}")
            export_actor(f"{family}_weapon_{base_name}_{weapon_name}", source,
                         ("attack",))
            exported += 1
    return exported


def main() -> None:
    # The repository owns the final SD-card assets.  The original PNG sources
    # are optional, so never delete working journey assets when they are absent.
    if not (RAW / "\u5386\u7ec3").exists():
        refresh_selection_screen()
        return
    if OUTPUT.exists():
        shutil.rmtree(OUTPUT)
    OUTPUT.mkdir(parents=True)
    selection_output = OUTPUT.parent / "selection_screen.rgb565"
    selection_output.parent.mkdir(parents=True, exist_ok=True)
    build_selection_screen(selection_output)
    backgrounds = RAW / "历练/青岚灵墟/背景"
    rgb565(backgrounds / "青岚灵墟.png", OUTPUT / "background_day.rgb565")
    rgb565(backgrounds / "青岚灵墟-夜.png", OUTPUT / "background_night.rgb565")
    for name, source in ACTORS.items():
        export_actor(name, source)
    player_appearance_count = export_player_appearances()
    player_weapon_count = export_player_weapons()
    print(f"Built {player_appearance_count} equipped player appearances")
    print(f"Built {player_weapon_count} equipped player weapon layers")
    (OUTPUT / "manifest.json").write_text(json.dumps({"id": 1, "name": "青岚灵墟", "monsters": ["willow_wraith", "peach_treant", "azure_dragon"]}, ensure_ascii=False), encoding="utf-8")


if __name__ == "__main__":
    main()
