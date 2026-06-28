"""Phase 5: Redgate exhibit stage geometry on Godfrey_World (Kristofer only).

WARNING: Do NOT run headless — splits actors across unloaded WP cells.
Use editor-only with exhibit region loaded (see verify_godfrey_exhibit.py).

NON-DESTRUCTIVE: keeps Phase 4 lights, floor, and user-tuned settings.
Adds HDRI sky sphere, backdrop plane, rect key, and frames cine camera.
Does NOT copy Godfrey_Realtime_Stage WP external actors.

Prerequisite: Content/Godfrey/Backgrounds/ copied from Test_Live_Audio.

Run headless:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_Test/UnrealPerformer.uproject"
    /Game/Godfrey_World
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_Test/Scripts/setup_phase5_stage_geometry.py"
    -unattended -nop4 -nosplash -log
"""
from __future__ import annotations

import unreal

REPORT = "Phase5StageGeometry.txt"
HDR_MAT = "/Game/Godfrey/Backgrounds/M_Redgate_Background.M_Redgate_Background"
SKY_MESH = "/Engine/EngineSky/SM_SkySphere.SM_SkySphere"
PLANE_MESH = "/Engine/BasicShapes/Plane.Plane"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[Phase5Stage] {msg}")


def warn(msg: str) -> None:
    _lines.append(f"WARN: {msg}")
    unreal.log_warning(f"[Phase5Stage] {msg}")


def actors() -> unreal.EditorActorSubsystem:
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def find_kristofer() -> unreal.Actor | None:
    for actor in actors().get_all_level_actors():
        label = actor.get_actor_label()
        if "Kristofer" in label and "Light" not in label and "PostProcess" not in label:
            return actor
    return None


def find_by_label(label: str) -> unreal.Actor | None:
    for actor in actors().get_all_level_actors():
        if actor.get_actor_label() == label:
            return actor
    return None


def load_material() -> unreal.MaterialInterface | None:
    mat = unreal.load_asset(HDR_MAT)
    if not mat:
        warn(f"Missing {HDR_MAT} — copy Godfrey/Backgrounds from Test_Live_Audio first")
    return mat


def configure_skylight_capture() -> None:
    for actor in actors().get_all_level_actors():
        if not isinstance(actor, unreal.SkyLight):
            continue
        comp = actor.get_component_by_class(unreal.SkyLightComponent)
        if not comp:
            continue
        comp.set_mobility(unreal.ComponentMobility.MOVABLE)
        comp.set_intensity(1.2)
        comp.set_real_time_capture(True)
        comp.recapture_sky()
        log(f"SkyLight {actor.get_actor_label()}: real-time capture, intensity 1.2")


def ensure_sky_sphere(center: unreal.Vector, mat: unreal.MaterialInterface) -> None:
    label = "Stage_SkySphere"
    existing = find_by_label(label)
    if existing:
        comp = existing.get_component_by_class(unreal.StaticMeshComponent)
        if comp and mat:
            comp.set_material(0, mat)
        log(f"{label} already exists")
        return

    mesh = unreal.load_asset(SKY_MESH)
    if not mesh:
        warn(f"Could not load {SKY_MESH}")
        return

    actor = actors().spawn_actor_from_class(
        unreal.StaticMeshActor,
        unreal.Vector(center.x, center.y, center.z),
        unreal.Rotator(0.0, 0.0, 0.0),
    )
    actor.set_actor_label(label)
    comp = actor.get_component_by_class(unreal.StaticMeshComponent)
    if comp:
        comp.set_static_mesh(mesh)
        comp.set_mobility(unreal.ComponentMobility.STATIONARY)
        comp.set_cast_shadow(False)
        comp.set_editor_property("affect_dynamic_indirect_lighting", False)
        if mat:
            comp.set_material(0, mat)
        actor.set_actor_scale3d(unreal.Vector(400.0, 400.0, 400.0))
    log(f"Spawned {label} with Redgate material")


def ensure_backdrop(center: unreal.Vector, mat: unreal.MaterialInterface) -> None:
    label = "Stage_Backdrop"
    loc = unreal.Vector(center.x, center.y - 450.0, center.z + 40.0)
    rot = unreal.Rotator(0.0, 0.0, -90.0)

    existing = find_by_label(label)
    if existing:
        comp = existing.get_component_by_class(unreal.StaticMeshComponent)
        if comp and mat:
            mic = unreal.EditorAssetLibrary.load_asset(
                "/Game/Godfrey/Backgrounds/MI_Stage_Backdrop"
            )
            comp.set_material(0, mic if mic else mat)
        log(f"{label} already exists (transform unchanged)")
        return

    mesh = unreal.load_asset(PLANE_MESH)
    if not mesh:
        return

    actor = actors().spawn_actor_from_class(unreal.StaticMeshActor, loc, rot)
    actor.set_actor_label(label)
    comp = actor.get_component_by_class(unreal.StaticMeshComponent)
    if comp:
        comp.set_static_mesh(mesh)
        comp.set_mobility(unreal.ComponentMobility.STATIONARY)
        comp.set_cast_shadow(False)
        comp.set_editor_property("affect_dynamic_indirect_lighting", False)
        comp.set_editor_property("visible_in_ray_tracing", False)
        if mat:
            comp.set_material(0, mat)
        actor.set_actor_scale3d(unreal.Vector(12.0, 8.0, 1.0))
    log(f"Spawned {label} at {loc}")


def ensure_rect_key(center: unreal.Vector) -> None:
    label = "Stage_RectKey"
    loc = unreal.Vector(center.x + 180.0, center.y + 220.0, center.z + 160.0)
    look_at = unreal.MathLibrary.find_look_at_rotation(loc, center)

    existing = find_by_label(label)
    if existing:
        existing.set_actor_location(loc, False, True)
        existing.set_actor_rotation(look_at, False)
        log(f"{label} already exists (repositioned)")
        return

    actor = actors().spawn_actor_from_class(unreal.RectLight, loc, look_at)
    actor.set_actor_label(label)
    comp = actor.get_component_by_class(unreal.RectLightComponent)
    if comp:
        comp.set_mobility(unreal.ComponentMobility.MOVABLE)
        comp.set_intensity(12000.0)
        comp.set_source_width(180.0)
        comp.set_source_height(120.0)
        comp.set_attenuation_radius(4000.0)
        comp.set_cast_shadows(True)
        comp.set_temperature(5600.0)
    log(f"Spawned {label} at {loc}")


def ensure_cine_camera(center: unreal.Vector) -> None:
    label = "Exhibit_CineCamera"
    cam_loc = unreal.Vector(center.x + 50.0, center.y + 500.0, center.z + 20.0)
    look_at = unreal.MathLibrary.find_look_at_rotation(
        cam_loc, unreal.Vector(center.x, center.y, center.z + 60.0)
    )

    actor = find_by_label(label)
    if not actor:
        actor = actors().spawn_actor_from_class(unreal.CineCameraActor, cam_loc, look_at)
        actor.set_actor_label(label)
        log(f"Spawned {label}")
    else:
        actor.set_actor_location(cam_loc, False, True)
        actor.set_actor_rotation(look_at, False)
        log(f"Updated {label}")

    cine = actor.get_cine_camera_component()
    if cine:
        cine.set_editor_property("current_focal_length", 35.0)
        cine.set_editor_property("current_aperture", 2.8)


def reject_contaminated_performers() -> None:
    bad_labels = ("BP_Gavin", "Gavin", "Erno", "Godfrey_Performer", "GodfreyApi")
    for actor in actors().get_all_level_actors():
        label = actor.get_actor_label()
        if any(token in label for token in bad_labels):
            warn(f"Contaminated actor still in level: {label} — remove manually before Phase 5 PASS")


def write_report() -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines))
    log(f"Report: {path}")


def main() -> None:
    mat = load_material()
    kristofer = find_kristofer()
    if not kristofer:
        raise RuntimeError("BP_Kristofer not found on Godfrey_World")

    center = kristofer.get_actor_location()
    log(f"Kristofer at {center}")

    reject_contaminated_performers()
    ensure_sky_sphere(center, mat)
    configure_skylight_capture()
    ensure_backdrop(center, mat)
    ensure_rect_key(center)
    ensure_cine_camera(center)

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("save_dirty_packages OK")
    write_report()


if __name__ == "__main__":
    main()
