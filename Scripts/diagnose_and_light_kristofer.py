"""Diagnose BP_Kristofer visibility and add strong local lighting + fixed exposure.

Run headless:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_Test/UnrealPerformer.uproject"
    /Game/Godfrey_World
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_Test/Scripts/diagnose_and_light_kristofer.py"
    -unattended -nop4 -nosplash -log
"""
from __future__ import annotations

import math

import unreal

REPORT = "DiagnoseKristoferLighting.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[KristoferLight] {msg}")


def warn(msg: str) -> None:
    _lines.append(f"WARN: {msg}")
    unreal.log_warning(f"[KristoferLight] {msg}")


def actors() -> unreal.EditorActorSubsystem:
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def find_kristofer():
    matches = []
    for actor in actors().get_all_level_actors():
        label = actor.get_actor_label()
        cls = actor.get_class().get_name()
        path = actor.get_class().get_path_name()
        if "Kristofer" in label or "Kristofer" in cls or "Kristofer" in path:
            matches.append(actor)
    return matches


def mesh_material_report(actor) -> None:
    log(f"Actor: {actor.get_actor_label()} class={actor.get_class().get_name()}")
    log(f"  Location: {actor.get_actor_location()}")
    log(f"  HiddenInGame: {actor.is_hidden_ed()}")
    comps = actor.get_components_by_class(unreal.SkeletalMeshComponent)
    log(f"  SkeletalMeshComponents: {len(comps)}")
    for idx, comp in enumerate(comps[:8]):
        mesh = comp.get_skeletal_mesh_asset()
        mesh_name = mesh.get_name() if mesh else "None"
        mats = comp.get_materials()
        log(f"    [{idx}] mesh={mesh_name} materials={len(mats)} visible={comp.is_visible()}")
        for mid, mat in enumerate(mats[:3]):
            mat_name = mat.get_name() if mat else "None"
            log(f"         mat[{mid}]={mat_name}")


def ensure_post_process() -> unreal.PostProcessVolume:
    for actor in actors().get_all_level_actors():
        if isinstance(actor, unreal.PostProcessVolume):
            log(f"Updating PostProcessVolume: {actor.get_actor_label()}")
            configure_post_process(actor)
            return actor

    actor = actors().spawn_actor_from_class(
        unreal.PostProcessVolume,
        unreal.Vector(0.0, 0.0, 0.0),
        unreal.Rotator(0.0, 0.0, 0.0),
    )
    actor.set_actor_label("Kristofer_PostProcess")
    configure_post_process(actor)
    log("Spawned Kristofer_PostProcess")
    return actor


def configure_post_process(actor: unreal.PostProcessVolume) -> None:
    actor.set_editor_property("unbound", True)
    settings = actor.get_editor_property("settings")
    settings.set_editor_property("override_auto_exposure_method", True)
    settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
    settings.set_editor_property("override_auto_exposure_bias", True)
    settings.set_editor_property("auto_exposure_bias", 8.0)
    settings.set_editor_property("override_auto_exposure_min_brightness", True)
    settings.set_editor_property("auto_exposure_min_brightness", 0.5)
    settings.set_editor_property("override_auto_exposure_max_brightness", True)
    settings.set_editor_property("auto_exposure_max_brightness", 8.0)
    actor.set_editor_property("settings", settings)
    log("PostProcess: unbound, manual exposure bias 8")


def spawn_point_light(location: unreal.Vector, label: str, intensity: float) -> None:
    actor = actors().spawn_actor_from_class(
        unreal.PointLight,
        location,
        unreal.Rotator(0.0, 0.0, 0.0),
    )
    actor.set_actor_label(label)
    comp = actor.get_component_by_class(unreal.PointLightComponent)
    if comp:
        comp.set_mobility(unreal.ComponentMobility.MOVABLE)
        comp.set_intensity(intensity)
        comp.set_attenuation_radius(5000.0)
        comp.set_cast_shadows(True)
        comp.set_use_inverse_squared_falloff(False)
    log(f"PointLight {label} at {location} intensity={intensity}")


def aim_directional_at(target: unreal.Vector) -> None:
    for actor in actors().get_all_level_actors():
        if isinstance(actor, unreal.DirectionalLight):
            comp = actor.get_component_by_class(unreal.DirectionalLightComponent)
            if not comp:
                continue
            comp.set_mobility(unreal.ComponentMobility.MOVABLE)
            comp.set_intensity(10.0)
            actor.set_actor_rotation(unreal.Rotator(-45.0, 135.0, 0.0), False)
            log(f"DirectionalLight {actor.get_actor_label()} intensity 10, aimed at scene")


def light_ring_around(target: unreal.Vector) -> None:
    radius = 350.0
    height = 220.0
    intensity = 250000.0
    for i, angle in enumerate((0.0, 90.0, 180.0, 270.0)):
        rad = math.radians(angle)
        loc = unreal.Vector(
            target.x + math.cos(rad) * radius,
            target.y + math.sin(rad) * radius,
            target.z + height,
        )
        spawn_point_light(loc, f"Kristofer_KeyLight_{i}", intensity)

    spawn_point_light(
        unreal.Vector(target.x, target.y, target.z + 400.0),
        "Kristofer_FillTop",
        180000.0,
    )


def refresh_skylight() -> None:
    for actor in actors().get_all_level_actors():
        if isinstance(actor, unreal.SkyLight):
            comp = actor.get_component_by_class(unreal.SkyLightComponent)
            if not comp:
                continue
            comp.set_mobility(unreal.ComponentMobility.MOVABLE)
            comp.set_intensity(2.0)
            comp.set_real_time_capture(True)
            comp.recapture_sky()
            log(f"SkyLight {actor.get_actor_label()} intensity 2, recaptured")


def write_report() -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines))
    log(f"Report: {path}")


def raise_kristofer(actor) -> unreal.Vector:
    loc = actor.get_actor_location()
    if loc.z < 50.0:
        new_loc = unreal.Vector(loc.x, loc.y, 100.0)
        actor.set_actor_location(new_loc, False, True)
        log(f"Raised {actor.get_actor_label()} from z={loc.z} to z=100")
        return new_loc
    return loc


def main() -> None:
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    log(f"World: {world.get_name() if world else 'None'}")

    kristofers = find_kristofer()
    if not kristofers:
        warn("No Kristofer actor found in level")
        target = unreal.Vector(0.0, 0.0, 100.0)
    else:
        for actor in kristofers:
            mesh_material_report(actor)
        target = raise_kristofer(kristofers[0])

    ensure_post_process()
    aim_directional_at(target)
    refresh_skylight()
    light_ring_around(target)

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("save_dirty_packages OK")
    write_report()


if __name__ == "__main__":
    main()
