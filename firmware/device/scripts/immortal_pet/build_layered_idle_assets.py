#!/usr/bin/env python3
"""Build an SD-card package for runtime character/weapon layer composition."""
from __future__ import annotations

import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
SOURCE = ROOT / "docs/images/raw"
OUTPUT = ROOT / "docs/images/sdcard/immortal_pet/layered_idle"
DIRS = {"stand": {5, 6}, "walk": {0, 4}}
FAMILY_NAMES = {
    "all_女火": "female_fire",
    "all_女金": "female_metal",
    "all_男火": "male_fire",
    "all_男金": "male_metal",
}


def read(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def export_action(
    source: Path,
    destination: Path,
    action: str,
    reference: dict | None = None,
) -> dict | None:
    action_root = source / "actions" / action
    playback = action_root / "playback.json"
    animation = action_root / "animation.json"
    if not playback.is_file() or not animation.is_file():
        return None

    data = read(playback)
    canvas = read(animation).get("canvas", {})
    all_directions = data.get("directions", [])
    directions = [
        item for item in all_directions if item.get("index") in DIRS[action]
    ]
    if len(directions) != 2 and reference is not None and len(all_directions) == 1:
        serial = {
            frame.get("source_frame"): frame
            for frame in all_directions[0].get("frames", [])
        }
        directions = []
        for item in reference["directions"]:
            frames = [
                serial.get(frame.get("source_frame"))
                for frame in item["frames"]
            ]
            if any(frame is None for frame in frames):
                return None
            directions.append({"index": item["index"], "frames": frames})
    if len(directions) != 2:
        return None

    exported_directions = []
    for direction in directions:
        exported_frames = []
        for frame in direction["frames"]:
            relative = Path(frame["file"])
            original = action_root / relative
            source_x = frame.get("source_x")
            source_y = frame.get("source_y")
            if (
                not original.is_file()
                or not isinstance(source_x, int)
                or not isinstance(source_y, int)
            ):
                return None
            target = destination / action / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(original, target)
            exported_frames.append(
                {
                    "file": frame["file"],
                    "x": source_x,
                    "y": source_y,
                    "source_frame": frame.get("source_frame"),
                }
            )
        exported_directions.append(
            {"index": direction["index"], "frames": exported_frames}
        )

    return {
        "canvas": canvas,
        "frame_interval_ms": data.get("frame_interval_ms", 100),
        "directions": exported_directions,
    }


def export_asset(
    source: Path,
    destination: Path,
    reference: dict | None = None,
) -> dict | None:
    result = {
        action: export_action(
            source,
            destination,
            action,
            reference.get(action) if reference else None,
        )
        for action in DIRS
    }
    if any(value is None for value in result.values()):
        return None
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "actor.json").write_text(
        json.dumps(result, ensure_ascii=False, separators=(",", ":")),
        encoding="utf-8",
    )
    return result


def compatible(body: dict, weapon: dict) -> bool:
    for action in DIRS:
        body_directions = {
            item["index"]: item["frames"]
            for item in body[action]["directions"]
        }
        weapon_directions = {
            item["index"]: item["frames"]
            for item in weapon[action]["directions"]
        }
        if body_directions.keys() != weapon_directions.keys():
            return False
        if any(
            len(body_directions[index]) != len(weapon_directions[index])
            for index in body_directions
        ):
            return False
    return True


def base_model_for_suit(suit: str) -> str | None:
    if len(suit) != 6 or not suit.isdigit() or not suit.startswith("8"):
        return None
    return f"0{suit[1]}0{suit[4:6]}"


def main() -> None:
    if OUTPUT.exists():
        shutil.rmtree(OUTPUT)
    OUTPUT.mkdir(parents=True)
    entries = []

    for family_dir in sorted(SOURCE.glob("all_*")):
        character_root = family_dir / "characters" / family_dir.name
        if not character_root.is_dir():
            continue
        family = FAMILY_NAMES.get(family_dir.name, family_dir.name)
        base_configs = {}
        weapon_configs = {}

        for base in sorted((character_root / "base").glob("*")):
            if not base.is_dir():
                continue
            body_config = export_asset(
                base,
                OUTPUT / family / "bodies" / base.name,
            )
            if body_config is None:
                continue
            base_configs[base.name] = body_config

            model_weapons = []
            for weapon in sorted(
                (character_root / "weapons" / base.name).glob("*")
            ):
                if not weapon.is_dir():
                    continue
                weapon_config = export_asset(
                    weapon,
                    OUTPUT / family / "weapons" / base.name / weapon.name,
                    body_config,
                )
                if weapon_config is not None:
                    model_weapons.append((weapon.name, weapon_config))
            weapon_configs[base.name] = model_weapons

            entries.extend(
                {
                    "body": f"{family}/bodies/{base.name}",
                    "weapon": (
                        f"{family}/weapons/{base.name}/{weapon_name}"
                    ),
                }
                for weapon_name, weapon_config in model_weapons
                if compatible(body_config, weapon_config)
            )

        without_weapon = character_root / "suits" / "without_weapon"
        for suit in sorted(without_weapon.glob("*")):
            base_model = base_model_for_suit(suit.name)
            if not suit.is_dir() or base_model not in base_configs:
                continue
            body_config = export_asset(
                suit,
                OUTPUT / family / "bodies" / suit.name,
            )
            if body_config is None:
                continue
            entries.extend(
                {
                    "body": f"{family}/bodies/{suit.name}",
                    "weapon": (
                        f"{family}/weapons/{base_model}/{weapon_name}"
                    ),
                }
                for weapon_name, weapon_config in weapon_configs.get(
                    base_model, []
                )
                if compatible(body_config, weapon_config)
            )

        with_weapon = character_root / "suits" / "with_weapon"
        for suit in sorted(with_weapon.glob("*")):
            if not suit.is_dir():
                continue
            body_config = export_asset(
                suit,
                OUTPUT / family / "bodies" / suit.name,
            )
            if body_config is not None:
                entries.append(
                    {
                        "body": f"{family}/bodies/{suit.name}",
                        "weapon": None,
                    }
                )

    (OUTPUT / "catalog.json").write_text(
        json.dumps(
            {"entries": entries},
            ensure_ascii=False,
            separators=(",", ":"),
        ),
        encoding="utf-8",
    )
    print(f"Built {len(entries)} runtime layer combinations")


if __name__ == "__main__":
    main()
