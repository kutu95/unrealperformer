"""Load Godfrey_World exhibit region so Kristofer / floor / lights are not (Unloaded).

Run with Godfrey_World open:
  Tools > Execute Python Script > this file

Or headless:
  UnrealEditor-Cmd.exe ... /Game/Godfrey_World -ExecutePythonScript=".../fix_godfrey_world_partition_load.py"
"""
from __future__ import annotations

import unreal

EXHIBIT = unreal.Vector(540.0, 0.0, 100.0)
RADIUS = 5000.0
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[WPLoad] {msg}")


def actors() -> unreal.EditorActorSubsystem:
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def load_exhibit_region() -> None:
    world = unreal.EditorLevelLibrary.get_editor_world()
    if not world:
        raise RuntimeError("No editor world — open Godfrey_World first")

    for cmd in (
        "wp.Editor.LoadAllCells",
        "wp.Editor.LoadRegion",
    ):
        unreal.SystemLibrary.execute_console_command(world, cmd)
        log(f"Console: {cmd}")

    try:
        les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        les.set_viewport_camera_location(EXHIBIT)
        log(f"Viewport moved to exhibit {EXHIBIT}")
    except Exception as exc:
        log(f"Viewport move skipped: {exc}")


def report_loaded_actors() -> None:
    labels = []
    for actor in actors().get_all_level_actors():
        labels.append(f"{actor.get_actor_label()} ({actor.get_class().get_name()})")
    labels.sort()
    for line in labels:
        log(line)
    log(f"Total loaded actors: {len(labels)}")


def main() -> None:
    load_exhibit_region()
    report_loaded_actors()
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + "GodfreyWorldWPLoad.txt"
    )
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines))
    log(f"Report: {path}")


if __name__ == "__main__":
    main()
