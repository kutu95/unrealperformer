"""Diagnose BP_Kristofer visibility and apply modest Lumen lighting.

Emergency debug script — uses sane intensities (not the old 250000 point lights).
Prefer tuning Lumen_DirectionalLight + Lumen_SkyLight in editor (~10–50 range).

Run headless:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_Test/UnrealPerformer.uproject"
    /Game/Godfrey_World
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_Test/Scripts/diagnose_and_light_kristofer.py"
    -unattended -nop4 -nosplash -log
"""
from __future__ import annotations

import unreal

REPORT = "DiagnoseKristoferLighting.txt"
_lines: list[str] = []

# Modest defaults — exhibition daylight is Phase 5–6.
DIRECTIONAL_INTENSITY = 5.0
SKYLIGHT_INTENSITY = 1.0
POST_EXPOSURE_BIAS = 1.0


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
    comps = actor.get_components_by_class(unreal.SkeletalMeshComponent)
    log(f"  SkeletalMeshComponents: {len(comps)}")
    for idx, comp in enumerate(comps[:3]):
        mesh = comp.get_skeletal_mesh_asset()
        mesh_name = mesh.get_name() if mesh else "None"
        mats = comp.get_materials()
        log(f"    [{idx}] mesh={mesh_name} materials={len(mats)} visible={comp.is_visible()}")


def configure_post_process(actor: unreal.PostProcessVolume) -> None:
    actor.set_editor_property("unbound", True)
    settings = actor.get_editor_property("settings")
    settings.set_editor_property("override_auto_exposure_method", True)
    settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_BASIC)
    settings.set_editor_property("override_auto_exposure_bias", True)
    settings.set_editor_property("auto_exposure_bias", POST_EXPOSURE_BIAS)
    actor.set_editor_property("settings", settings)
    log(f"PostProcess: unbound, basic auto exposure bias {POST_EXPOSURE_BIAS}")


def ensure_post_process() -> None:
    for actor in actors().get_all_level_actors():
        if isinstance(actor, unreal.PostProcessVolume):
            log(f"Updating PostProcessVolume: {actor.get_actor_label()}")
            configure_post_process(actor)
            return

    actor = actors().spawn_actor_from_class(
        unreal.PostProcessVolume,
        unreal.Vector(0.0, 0.0, 0.0),
        unreal.Rotator(0.0, 0.0, 0.0),
    )
    actor.set_actor_label("Kristofer_PostProcess")
    configure_post_process(actor)
    log("Spawned Kristofer_PostProcess")


def configure_directional(actor: unreal.DirectionalLight) -> None:
    comp = actor.get_component_by_class(unreal.DirectionalLightComponent)
    if not comp:
        return
    comp.set_mobility(unreal.ComponentMobility.MOVABLE)
    comp.set_intensity(DIRECTIONAL_INTENSITY)
    comp.set_cast_shadows(True)
    comp.set_temperature(6500.0)
    actor.set_actor_rotation(unreal.Rotator(-45.0, 135.0, 0.0), False)
    log(f"DirectionalLight {actor.get_actor_label()} intensity {DIRECTIONAL_INTENSITY}")


def configure_skylight(actor: unreal.SkyLight) -> None:
    comp = actor.get_component_by_class(unreal.SkyLightComponent)
    if not comp:
        return
    comp.set_mobility(unreal.ComponentMobility.MOVABLE)
    comp.set_intensity(SKYLIGHT_INTENSITY)
    comp.set_real_time_capture(True)
    comp.recapture_sky()
    log(f"SkyLight {actor.get_actor_label()} intensity {SKYLIGHT_INTENSITY}")


def refresh_scene_lights() -> None:
    has_directional = False
    has_skylight = False
    for actor in actors().get_all_level_actors():
        if isinstance(actor, unreal.DirectionalLight):
            configure_directional(actor)
            has_directional = True
        elif isinstance(actor, unreal.SkyLight):
            configure_skylight(actor)
            has_skylight = True

    if not has_directional:
        warn("No DirectionalLight — run fix_godfrey_world_lumen_lighting.py first")
    if not has_skylight:
        warn("No SkyLight — run fix_godfrey_world_lumen_lighting.py first")


def raise_kristofer(actor) -> unreal.Vector:
    loc = actor.get_actor_location()
    if loc.z < 50.0:
        new_loc = unreal.Vector(loc.x, loc.y, 100.0)
        actor.set_actor_location(new_loc, False, True)
        log(f"Raised {actor.get_actor_label()} from z={loc.z} to z=100")
        return new_loc
    return loc


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

    kristofers = find_kristofer()
    if not kristofers:
        warn("No Kristofer actor found in level")
    else:
        for actor in kristofers:
            mesh_material_report(actor)
            raise_kristofer(actor)

    ensure_post_process()
    refresh_scene_lights()

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("save_dirty_packages OK")
    write_report()


if __name__ == "__main__":
    main()
