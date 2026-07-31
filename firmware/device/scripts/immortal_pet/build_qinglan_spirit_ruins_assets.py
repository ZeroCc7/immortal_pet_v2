#!/usr/bin/env python3
"""Build the Qinglan Spirit Ruins TF-card assets from the user-owned PNGs."""

from __future__ import annotations

import json
import shutil
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


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
UI_FONT = Path("C:/Windows/Fonts/msyhbd.ttc")


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
    centered("\u67f3\u9b3c   \u00b7   \u6843\u7cbe   \u00b7   \u9752\u9f99", 301, 16, "#395e56")
    centered("\u6d88\u8017\u7cbe\u529b  1", 337, 17, "#6c5141")
    draw.text((150, 398), "\u524d\u5f80\u5386\u7ec3", font=font(19), fill="#f8e8b5", anchor="mm",
              stroke_width=1, stroke_fill="#1d4a42")
    draw.text((330, 398), "\u8fd4\u56de\u6d1e\u5e9c", font=font(19), fill="#f8e8b5", anchor="mm",
              stroke_width=1, stroke_fill="#1d4a42")
    rgb565_image(background.convert("RGB"), destination)


def export_actor(name: str, source: Path) -> None:
    destination = OUTPUT / "actors" / name
    destination.mkdir(parents=True, exist_ok=True)
    metadata: dict[str, object] = {}
    for action in ACTION_NAMES:
        action_root = source / "actions" / action if (source / "actions").is_dir() else source / action
        animation = action_root / "animation.json"
        playback = action_root / "playback.json"
        if not animation.is_file() or not playback.is_file():
            raise ValueError(f"missing {action} metadata for {name}")
        canvas = json.loads(animation.read_text(encoding="utf-8"))["canvas"]
        directions = json.loads(playback.read_text(encoding="utf-8"))["directions"]
        direction_index = PLAYER_DIRECTIONS[action] if name in ("female", "male") else MONSTER_DIRECTIONS[action]
        direction = next((item for item in directions if item["index"] == direction_index),
                         directions[0])
        frames = []
        for index, frame in enumerate(direction["frames"]):
            image = Image.open(action_root / frame["file"]).convert("RGBA")
            normalized = Image.new("RGBA", (256, 256))
            normalized.alpha_composite(image, (frame["x"], frame["y"]))
            normalized = normalized.resize((FRAME_SIZE, FRAME_SIZE), Image.Resampling.LANCZOS)
            target = destination / action / f"frame-{index:03d}.argb8888"
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(normalized.tobytes("raw", "BGRA"))
            frames.append(target.relative_to(destination).as_posix())
        while len(frames) < 8:
            target = destination / action / f"frame-{len(frames):03d}.argb8888"
            shutil.copy2(destination / frames[-1], target)
            frames.append(target.relative_to(destination).as_posix())
        metadata[action] = {"frame_interval_ms": json.loads(playback.read_text(encoding="utf-8")).get("frame_interval_ms", 100), "frames": frames, "canvas": canvas}
    (destination / "actor.json").write_text(json.dumps(metadata, separators=(",", ":")), encoding="utf-8")


def main() -> None:
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
    (OUTPUT / "manifest.json").write_text(json.dumps({"id": 1, "name": "青岚灵墟", "monsters": ["willow_wraith", "peach_treant", "azure_dragon"]}, ensure_ascii=False), encoding="utf-8")


if __name__ == "__main__":
    main()
