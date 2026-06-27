"""Phase 4: simple exhibit setup for BP_Kristofer on Godfrey_World.

Adds a floor, soft daylight rig, removes emergency test point lights.
NOT Godfrey_Realtime_Stage — no exhibit coords, no stage geo, Kristofer only.

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
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[Phase4Exhibit] {msg}")


def warn(msg: str) -> None:
    _lines.append(f"WARN: {msg}")
    unreal.log_warning(f"[Phase4Exhibit] {msg}")


def actors() -> unreal.EditorActorSubsystem:
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def find_kristofer():
    for actor in actors().get_all_level_actors():
        label = actor.get_actor_label()
        if "Kristofer" in label and "Light" not in label and "PostProcess" not in label:
            return actor
    return None


def remove_emergency_lights() -> None:
    remove_labels = (
        "Kristofer_KeyLight_",
        "Kristofer_FillTop",
    )
    for actor in list(actors().get_all_level_actors()):
        label = actor.get_actor_label()
        if any(label.startswith(prefix) or label == prefix for prefix in remove_labels):
            actors().destroy_actor(actor)
            log(f"Removed emergency light: {label}")


def configure_daylight() -> None:
    for actor in actors().get_all_level_actors():
        if isinstance(actor, unreal.DirectionalLight):
            comp = actor.get_component_by_class(unreal.DirectionalLightComponent)
            if comp:
                comp.set_mobility(unreal.ComponentMobility.MOVABLE)
                comp.set_intensity(4.0)
                comp.set_temperature(5800.0)
                comp.set_cast_shadows(True)
                actor.set_actor_rotation(unreal.Rotator(-42.0, 35.0, 0.0), False)
                log(f"Daylight directional: {actor.get_actor_label()} lux=4")
        elif isinstance(actor, unreal.SkyLight):
            comp = actor.get_component_by_class(unreal.SkyLightComponent)
            if comp:
                comp.set_mobility(unreal.ComponentMobility.MOVABLE)
                comp.set_intensity(1.2)
                comp.set_real_time_capture(True)
                comp.recapture_sky()
                log(f"Daylight skylight: {actor.get_actor_label()} intensity=1.2")
        elif isinstance(actor, unreal.PostProcessVolume):
            settings = actor.get_editor_property("settings")
            settings.set_editor_property("override_auto_exposure_method", True)
            settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_BASIC)
            settings.set_editor_property("override_auto_exposure_bias", True)
            settings.set_editor_property("auto_exposure_bias", 0.0)
            actor.set_editor_property("settings", settings)
            actor.set_editor_property("unbound", True)
            log(f"PostProcess: basic auto exposure on {actor.get_actor_label()}")


def ensure_floor(center: unreal.Vector) -> None:
    for actor in actors().get_all_level_actors():
        if actor.get_actor_label() == "Exhibit_Floor":
            sm = actor.get_component_by_class(unreal.StaticMeshComponent)
            if sm:
                actor.set_actor_location(unreal.Vector(center.x, center.y, 0.0), False, True)
                log("Repositioned existing Exhibit_Floor to z=0")
            return

    mesh = unreal.load_asset(FLOOR_MESH)
    if not mesh:
        warn(f"Could not load floor mesh {FLOOR_MESH}")
        return

    floor_loc = unreal.Vector(center.x, center.y, 0.0)
    actor = actors().spawn_actor_from_class(
        unreal.StaticMeshActor,
        floor_loc,
        unreal.Rotator(0.0, 0.0, 0.0),
    )
    actor.set_actor_label("Exhibit_Floor")
    sm = actor.get_component_by_class(unreal.StaticMeshComponent)
    if sm:
        sm.set_static_mesh(mesh)
        sm.set_mobility(unreal.ComponentMobility.MOVABLE)
        sm.set_cast_shadow(True)
        actor.set_actor_scale3d(unreal.Vector(50.0, 50.0, 1.0))
    log(f"Spawned Exhibit_Floor at {floor_loc} scale 50m")


def ensure_exhibit_camera(target: unreal.Vector) -> None:
    for actor in actors().get_all_level_actors():
        if actor.get_actor_label() == "Exhibit_CineCamera":
            log("Exhibit_CineCamera already exists")
            return

    cam_loc = unreal.Vector(target.x + 250.0, target.y + 180.0, target.z + 30.0)
    actor = actors().spawn_actor_from_class(
        unreal.CineCameraActor,
        cam_loc,
        unreal.Rotator(-8.0, -145.0, 0.0),
    )
    actor.set_actor_label("Exhibit_CineCamera")
    log(f"Spawned Exhibit_CineCamera at {cam_loc}")


def write_report() -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines))
    log(f"Report: {path}")


def main() -> None:
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    log(f"World: {world.get_name() if world else 'None'}")

    kristofer = find_kristofer()
    if not kristofer:
        raise RuntimeError("BP_Kristofer not found in level.")

    center = kristofer.get_actor_location()
    stand_loc = unreal.Vector(center.x, center.y, 100.0)
    if abs(center.z - 100.0) > 5.0:
        kristofer.set_actor_location(stand_loc, False, True)
        log(f"Normalized Kristofer stand position to {stand_loc}")
    center = stand_loc

    remove_emergency_lights()
    configure_daylight()
    ensure_floor(center)
    ensure_exhibit_camera(center)

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("save_dirty_packages OK")
    write_report()


if __name__ == "__main__":
    main()
