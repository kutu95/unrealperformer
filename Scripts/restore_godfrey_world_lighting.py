"""Restore Godfrey_World lighting after Phase 4 removed tuned key lights.

Does NOT delete existing lights. Ensures sun/sky/fill + stable exposure for Kristofer.

Run headless:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_Test/UnrealPerformer.uproject"
    /Game/Godfrey_World
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_Test/Scripts/restore_godfrey_world_lighting.py"
    -unattended -nop4 -nosplash -log
"""
from __future__ import annotations

import math

import unreal

REPORT = "RestoreGodfreyWorldLighting.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[RestoreLight] {msg}")


def warn(msg: str) -> None:
    _lines.append(f"WARN: {msg}")
    unreal.log_warning(f"[RestoreLight] {msg}")


def actors() -> unreal.EditorActorSubsystem:
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def find_kristofer():
    for actor in actors().get_all_level_actors():
        label = actor.get_actor_label()
        if "Kristofer" in label and "Light" not in label and "PostProcess" not in label:
            return actor
    return None


def ensure_core_sky() -> None:
    has_dir = has_sky = has_atmo = False
    for actor in actors().get_all_level_actors():
        if isinstance(actor, unreal.DirectionalLight):
            has_dir = True
            comp = actor.get_component_by_class(unreal.DirectionalLightComponent)
            if comp:
                comp.set_mobility(unreal.ComponentMobility.MOVABLE)
                comp.set_intensity(6.0)
                comp.set_temperature(5800.0)
                comp.set_cast_shadows(True)
                actor.set_actor_rotation(unreal.Rotator(-42.0, 35.0, 0.0), False)
                log(f"Directional {actor.get_actor_label()} -> lux 6")
        elif isinstance(actor, unreal.SkyLight):
            has_sky = True
            comp = actor.get_component_by_class(unreal.SkyLightComponent)
            if comp:
                comp.set_mobility(unreal.ComponentMobility.MOVABLE)
                comp.set_intensity(1.5)
                comp.set_real_time_capture(True)
                comp.recapture_sky()
                log(f"SkyLight {actor.get_actor_label()} -> 1.5")
        elif isinstance(actor, unreal.SkyAtmosphere):
            has_atmo = True

    if not has_dir:
        actor = actors().spawn_actor_from_class(
            unreal.DirectionalLight,
            unreal.Vector(0.0, 0.0, 500.0),
            unreal.Rotator(-42.0, 35.0, 0.0),
        )
        actor.set_actor_label("Exhibit_Sun")
        comp = actor.get_component_by_class(unreal.DirectionalLightComponent)
        if comp:
            comp.set_mobility(unreal.ComponentMobility.MOVABLE)
            comp.set_intensity(6.0)
        log("Spawned Exhibit_Sun")

    if not has_sky:
        actor = actors().spawn_actor_from_class(
            unreal.SkyLight,
            unreal.Vector(0.0, 0.0, 600.0),
            unreal.Rotator(0.0, 0.0, 0.0),
        )
        actor.set_actor_label("Exhibit_SkyLight")
        comp = actor.get_component_by_class(unreal.SkyLightComponent)
        if comp:
            comp.set_mobility(unreal.ComponentMobility.MOVABLE)
            comp.set_intensity(1.5)
            comp.set_real_time_capture(True)
            comp.recapture_sky()
        log("Spawned Exhibit_SkyLight")

    if not has_atmo:
        actors().spawn_actor_from_class(
            unreal.SkyAtmosphere,
            unreal.Vector(0.0, 0.0, 0.0),
            unreal.Rotator(0.0, 0.0, 0.0),
        ).set_actor_label("Exhibit_SkyAtmosphere")
        log("Spawned Exhibit_SkyAtmosphere")


def ensure_fill_lights(center: unreal.Vector) -> None:
    existing = [
        a.get_actor_label()
        for a in actors().get_all_level_actors()
        if isinstance(a, unreal.PointLight) and "Exhibit_Fill" in a.get_actor_label()
    ]
    if len(existing) >= 2:
        log(f"Fill lights already present: {existing}")
        return

    specs = (
        ("Exhibit_Fill_Key", 45.0, unreal.Vector(200.0, -150.0, 180.0)),
        ("Exhibit_Fill_Rim", 35.0, unreal.Vector(-180.0, 120.0, 160.0)),
    )
    for label, intensity, offset in specs:
        if any(label in name for name in existing):
            continue
        loc = unreal.Vector(center.x + offset.x, center.y + offset.y, center.z + offset.z)
        actor = actors().spawn_actor_from_class(unreal.PointLight, loc, unreal.Rotator(0.0, 0.0, 0.0))
        actor.set_actor_label(label)
        comp = actor.get_component_by_class(unreal.PointLightComponent)
        if comp:
            comp.set_mobility(unreal.ComponentMobility.MOVABLE)
            comp.set_intensity(intensity)
            comp.set_attenuation_radius(3000.0)
            comp.set_use_inverse_squared_falloff(False)
            comp.set_cast_shadows(False)
        log(f"Spawned {label} intensity={intensity} at {loc}")


def configure_post_process() -> None:
    for actor in actors().get_all_level_actors():
        if not isinstance(actor, unreal.PostProcessVolume):
            continue
        actor.set_editor_property("unbound", True)
        settings = actor.get_editor_property("settings")
        settings.set_editor_property("override_auto_exposure_method", True)
        settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
        settings.set_editor_property("override_auto_exposure_bias", True)
        settings.set_editor_property("auto_exposure_bias", 2.5)
        actor.set_editor_property("settings", settings)
        log(f"PostProcess {actor.get_actor_label()}: manual exposure bias 2.5")


def stabilize_floor() -> None:
    floor_z = 1.0
    floor_mat = "/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"
    for actor in actors().get_all_level_actors():
        if actor.get_actor_label() != "Exhibit_Floor":
            continue
        loc = actor.get_actor_location()
        if abs(loc.z - floor_z) > 0.01:
            actor.set_actor_location(unreal.Vector(loc.x, loc.y, floor_z), False, True)
        comp = actor.get_component_by_class(unreal.StaticMeshComponent)
        if comp:
            comp.set_mobility(unreal.ComponentMobility.STATIONARY)
            comp.set_cast_shadow(False)
            comp.set_editor_property("affect_dynamic_indirect_lighting", False)
            comp.set_editor_property("affect_distance_field_lighting", False)
            comp.set_editor_property("visible_in_ray_tracing", False)
            mat = unreal.load_asset(floor_mat)
            if mat:
                comp.set_material(0, mat)
            log(f"Exhibit_Floor -> z={floor_z}, STATIONARY, low-GI material")


def write_report() -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines))
    log(f"Report: {path}")


def main() -> None:
    kristofer = find_kristofer()
    if not kristofer:
        raise RuntimeError("BP_Kristofer not found")

    center = kristofer.get_actor_location()
    if center.z < 50.0:
        center = unreal.Vector(center.x, center.y, 100.0)
        kristofer.set_actor_location(center, False, True)

    ensure_core_sky()
    ensure_fill_lights(center)
    configure_post_process()
    stabilize_floor()

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("save_dirty_packages OK")
    write_report()


if __name__ == "__main__":
    main()
