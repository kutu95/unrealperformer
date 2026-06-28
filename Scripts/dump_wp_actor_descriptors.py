"""Dump World Partition actor descriptors (including unloaded) for transform recovery."""
from __future__ import annotations

import unreal

REPORT = "WPActorDescriptors.txt"
TARGETS = ("Stage_Backdrop", "Kristofer", "Exhibit_Floor")
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[WPDump] {msg}")


def load_cells() -> None:
    world = unreal.EditorLevelLibrary.get_editor_world()
    if not world:
        raise RuntimeError("No editor world")
    unreal.SystemLibrary.execute_console_command(world, "wp.Editor.LoadAllCells")
    log("wp.Editor.LoadAllCells")


def dump_loaded_actors() -> None:
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in eas.get_all_level_actors():
        label = actor.get_actor_label()
        if not any(t in label for t in TARGETS):
            continue
        loc = actor.get_actor_location()
        rot = actor.get_actor_rotation()
        scale = actor.get_actor_scale3d()
        log(
            f"LOADED {label}: loc=({loc.x:.3f},{loc.y:.3f},{loc.z:.3f}) "
            f"rot=({rot.pitch:.3f},{rot.yaw:.3f},{rot.roll:.3f}) "
            f"scale=({scale.x:.3f},{scale.y:.3f},{scale.z:.3f})"
        )


def dump_descriptors() -> None:
    world = unreal.EditorLevelLibrary.get_editor_world()
    wp = world.get_world_partition()
    if not wp:
        log("No WorldPartition on level")
        return

    try:
        descs = wp.get_actor_descriptions()
        log(f"get_actor_descriptions count={len(descs)}")
        for desc in descs:
            label = str(desc.get_actor_label()) if hasattr(desc, "get_actor_label") else "?"
            name = str(desc.get_actor_name()) if hasattr(desc, "get_actor_name") else "?"
            if not any(t in label or t in name for t in TARGETS + ("Backdrop", "StaticMesh")):
                continue
            loc = desc.get_editor_property("location") if hasattr(desc, "get_editor_property") else None
            rot = desc.get_editor_property("rotation") if hasattr(desc, "get_editor_property") else None
            log(f"DESC label={label} name={name} loc={loc} rot={rot}")
    except Exception as exc:
        log(f"get_actor_descriptions failed: {exc}")

    try:
        lib = unreal.WorldPartitionEditorBlueprintLibrary
        if hasattr(lib, "get_all_actor_descriptions"):
            descs2 = lib.get_all_actor_descriptions(world)
            log(f"BP lib descriptors count={len(descs2)}")
    except Exception as exc:
        log(f"BP lib failed: {exc}")


def main() -> None:
    load_cells()
    dump_loaded_actors()
    dump_descriptors()
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines))
    log(f"Report: {path}")


if __name__ == "__main__":
    main()
