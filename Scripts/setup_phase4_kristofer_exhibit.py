"""Phase 4: simple exhibit setup for BP_Kristofer on Godfrey_World.

NON-DESTRUCTIVE: does not remove existing lights. Adds floor + daylight fill only.

Run headless:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_Test/UnrealPerformer.uproject"
    /Game/Godfrey_World
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_Test/Scripts/setup_phase4_kristofer_exhibit.py"
    -unattended -nop4 -nosplash -log
"""
from __future__ import annotations

import unreal

REPORT = "Phase4KristoferExhibit.txt"
FLOOR_MESH = "/Engine/BasicShapes/Plane.Plane"
FLOOR_MAT = "/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"
FLOOR_Z = 1.0
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[Phase4Exhibit] {msg}")


def actors() -> unreal.EditorActorSubsystem:
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def find_kristofer():
    for actor in actors().get_all_level_actors():
        label = actor.get_actor_label()
        if "Kristofer" in label and "Light" not in label and "PostProcess" not in label:
            return actor
    return None


def _configure_floor_mesh(comp: unreal.StaticMeshComponent) -> None:
    comp.set_mobility(unreal.ComponentMobility.STATIONARY)
    comp.set_cast_shadow(False)
    comp.set_editor_property("affect_dynamic_indirect_lighting", False)
    comp.set_editor_property("affect_distance_field_lighting", False)
    comp.set_editor_property("visible_in_ray_tracing", False)
    mat = unreal.load_asset(FLOOR_MAT)
    if mat:
        comp.set_material(0, mat)


def ensure_floor(center: unreal.Vector) -> None:
    loaded = [
        a for a in actors().get_all_level_actors() if a.get_actor_label() == "Exhibit_Floor"
    ]
    if loaded:
        actor = loaded[0]
        comp = actor.get_component_by_class(unreal.StaticMeshComponent)
        if comp:
            actor.set_actor_location(
                unreal.Vector(center.x, center.y, FLOOR_Z), False, True
            )
            _configure_floor_mesh(comp)
        log("Exhibit_Floor already exists (reconfigured)")
        if len(loaded) > 1:
            unreal.log_warning(
                "[Phase4Exhibit] Multiple Exhibit_Floor actors loaded — "
                "run remove_duplicate_exhibit_floor.py from editor"
            )
        return

    mesh = unreal.load_asset(FLOOR_MESH)
    if not mesh:
        return

    actor = actors().spawn_actor_from_class(
        unreal.StaticMeshActor,
        unreal.Vector(center.x, center.y, FLOOR_Z),
        unreal.Rotator(0.0, 0.0, 0.0),
    )
    actor.set_actor_label("Exhibit_Floor")
    sm = actor.get_component_by_class(unreal.StaticMeshComponent)
    if sm:
        sm.set_static_mesh(mesh)
        actor.set_actor_scale3d(unreal.Vector(50.0, 50.0, 1.0))
        _configure_floor_mesh(sm)
    log(f"Spawned Exhibit_Floor at z={FLOOR_Z}")


def main() -> None:
    kristofer = find_kristofer()
    if not kristofer:
        raise RuntimeError("BP_Kristofer not found")

    center = kristofer.get_actor_location()
    if center.z < 50.0:
        center = unreal.Vector(center.x, center.y, 100.0)
        kristofer.set_actor_location(center, False, True)

    ensure_floor(center)

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("Phase 4 floor only — run restore_godfrey_world_lighting.py for lighting")
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines))


if __name__ == "__main__":
    main()
