"""Identify duplicate Exhibit_Floor packages and remove the stale one (editor-only delete)."""
from __future__ import annotations

import os
import sys

import unreal

REPORT = "RemoveDuplicateFloor.txt"
KEEP_LOCATION = unreal.Vector(540.0, 0.0, 1.0)
LOCATION_EPS = 5.0
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[DedupeFloor] {msg}")


def actors() -> unreal.EditorActorSubsystem:
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def load_all_cells() -> None:
    world = unreal.EditorLevelLibrary.get_editor_world()
    if world:
        unreal.SystemLibrary.execute_console_command(world, "wp.Editor.LoadAllCells")


def find_floors() -> list[unreal.Actor]:
    floors: list[unreal.Actor] = []
    for actor in actors().get_all_level_actors():
        if actor.get_actor_label() == "Exhibit_Floor":
            floors.append(actor)
    return floors


def actor_path(actor: unreal.Actor) -> str:
    try:
        return actor.get_path_name()
    except Exception:
        return actor.get_name()


def near_exhibit(loc: unreal.Vector) -> bool:
    return (
        abs(loc.x - KEEP_LOCATION.x) <= LOCATION_EPS
        and abs(loc.y - KEEP_LOCATION.y) <= LOCATION_EPS
        and abs(loc.z - KEEP_LOCATION.z) <= 2.0
    )


def delete_actor(actor: unreal.Actor) -> None:
    label = actor.get_actor_label()
    path = actor_path(actor)
    loc = actor.get_actor_location()
    actors().destroy_actor(actor)
    log(f"Deleted duplicate {label} @ ({loc.x:.1f},{loc.y:.1f},{loc.z:.1f}) path={path}")


def main() -> None:
    load_all_cells()
    floors = find_floors()
    log(f"Loaded Exhibit_Floor count: {len(floors)}")

    for floor in floors:
        loc = floor.get_actor_location()
        log(f"  floor @ ({loc.x:.1f},{loc.y:.1f},{loc.z:.1f}) path={actor_path(floor)}")

    if len(floors) <= 1:
        log("Only one loaded floor — stale duplicate may be an unloaded descriptor only.")
        log("Saving level to reconcile WP descriptors after manual editor cleanup if needed.")

    keep = None
    stale: list[unreal.Actor] = []
    for floor in floors:
        loc = floor.get_actor_location()
        if near_exhibit(loc):
            if keep is None:
                keep = floor
            else:
                stale.append(floor)
        else:
            stale.append(floor)

    if keep is None and floors:
        keep = floors[0]
        stale = floors[1:]

    if keep:
        loc = keep.get_actor_location()
        log(f"Keeping floor @ ({loc.x:.1f},{loc.y:.1f},{loc.z:.1f})")

    for actor in stale:
        delete_actor(actor)

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("save_dirty_packages OK")

    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines) + "\n")
    log(f"Report: {path}")


if __name__ == "__main__":
    main()
