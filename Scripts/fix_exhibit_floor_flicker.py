"""Stabilize Exhibit_Floor — stop grid/Lumen flicker when floor is visible.

Typical causes:
  - Plane coplanar with editor viewport grid at z=0 (z-fighting)
  - Default plane material + Lumen temporal noise on huge surface

Run headless:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_Test/UnrealPerformer.uproject"
    /Game/Godfrey_World
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_Test/Scripts/fix_exhibit_floor_flicker.py"
    -unattended -nop4 -nosplash -log
"""
from __future__ import annotations

import unreal

REPORT = "FixExhibitFloorFlicker.txt"
FLOOR_Z = 1.0
FLOOR_MAT = "/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[FixFloor] {msg}")


def actors() -> unreal.EditorActorSubsystem:
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def configure_floor(comp: unreal.StaticMeshComponent, actor: unreal.Actor) -> None:
    loc = actor.get_actor_location()
    if abs(loc.z - FLOOR_Z) > 0.01:
        actor.set_actor_location(unreal.Vector(loc.x, loc.y, FLOOR_Z), False, True)
        log(f"Raised floor to z={FLOOR_Z} (avoids grid z-fighting)")

    comp.set_mobility(unreal.ComponentMobility.STATIONARY)
    comp.set_cast_shadow(False)
    comp.set_editor_property("affect_dynamic_indirect_lighting", False)
    comp.set_editor_property("affect_distance_field_lighting", False)
    comp.set_editor_property("visible_in_ray_tracing", False)
    comp.set_editor_property("evaluate_world_position_offset", False)

    mat = unreal.load_asset(FLOOR_MAT)
    if mat:
        comp.set_material(0, mat)
        log(f"Material -> {FLOOR_MAT}")
    else:
        log("WARN: BasicShapeMaterial not found; keeping existing material")

    log(
        "Floor flags: STATIONARY, no cast shadow, "
        "no dynamic indirect / DF / RT (reduces Lumen flicker)"
    )


def main() -> None:
    floor = None
    for actor in actors().get_all_level_actors():
        if actor.get_actor_label() == "Exhibit_Floor":
            floor = actor
            break

    if not floor:
        raise RuntimeError("Exhibit_Floor not found")

    comp = floor.get_component_by_class(unreal.StaticMeshComponent)
    if not comp:
        raise RuntimeError("Exhibit_Floor has no StaticMeshComponent")

    configure_floor(comp, floor)

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("save_dirty_packages OK")

    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines))
    log(f"Report: {path}")


if __name__ == "__main__":
    main()
