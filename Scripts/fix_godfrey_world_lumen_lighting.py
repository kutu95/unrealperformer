"""Ensure Godfrey_World has movable lighting for Lumen (Phase 1 renderer settings).

Phase 1 sets r.AllowStaticLighting=False. Open World template lights are often Static,
which leaves the scene black after that change.

Run headless:
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_Test/UnrealPerformer.uproject"
    /Game/Godfrey_World
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_Test/Scripts/fix_godfrey_world_lumen_lighting.py"
    -unattended -nop4 -nosplash -log
"""
from __future__ import annotations

import unreal

REPORT = "FixGodfreyWorldLumenLighting.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[LumenLighting] {msg}")


def warn(msg: str) -> None:
    _lines.append(f"WARN: {msg}")
    unreal.log_warning(f"[LumenLighting] {msg}")


def actor_subsystem() -> unreal.EditorActorSubsystem:
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def set_movable(component) -> None:
    if component:
        component.set_mobility(unreal.ComponentMobility.MOVABLE)


def configure_directional_light(actor) -> None:
    comp = actor.get_component_by_class(unreal.DirectionalLightComponent)
    if not comp:
        warn(f"No DirectionalLightComponent on {actor.get_actor_label()}")
        return
    set_movable(comp)
    comp.set_intensity(6.0)
    comp.set_cast_shadows(True)
    comp.set_temperature(6500.0)
    log(f"DirectionalLight: {actor.get_actor_label()} -> Movable, intensity 6")


def configure_sky_light(actor) -> None:
    comp = actor.get_component_by_class(unreal.SkyLightComponent)
    if not comp:
        warn(f"No SkyLightComponent on {actor.get_actor_label()}")
        return
    set_movable(comp)
    comp.set_intensity(1.0)
    comp.set_cast_shadows(True)
    comp.set_real_time_capture(True)
    comp.recapture_sky()
    log(f"SkyLight: {actor.get_actor_label()} -> Movable, RealTimeCapture ON")


def spawn_directional_light() -> unreal.DirectionalLight:
    actor = actor_subsystem().spawn_actor_from_class(
        unreal.DirectionalLight,
        unreal.Vector(0.0, 0.0, 500.0),
        unreal.Rotator(-45.0, 45.0, 0.0),
    )
    actor.set_actor_label("Lumen_DirectionalLight")
    configure_directional_light(actor)
    return actor


def spawn_sky_light() -> unreal.SkyLight:
    actor = actor_subsystem().spawn_actor_from_class(
        unreal.SkyLight,
        unreal.Vector(0.0, 0.0, 600.0),
        unreal.Rotator(0.0, 0.0, 0.0),
    )
    actor.set_actor_label("Lumen_SkyLight")
    configure_sky_light(actor)
    return actor


def spawn_sky_atmosphere() -> unreal.SkyAtmosphere:
    actor = actor_subsystem().spawn_actor_from_class(
        unreal.SkyAtmosphere,
        unreal.Vector(0.0, 0.0, 0.0),
        unreal.Rotator(0.0, 0.0, 0.0),
    )
    actor.set_actor_label("Lumen_SkyAtmosphere")
    log("Spawned SkyAtmosphere")
    return actor


def write_report() -> None:
    report_path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    with open(report_path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines))
    log(f"Report: {report_path}")


def main() -> None:
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if not world:
        raise RuntimeError("No editor world loaded")

    log(f"World: {world.get_name()}")

    directionals = []
    skylights = []
    atmospheres = []

    for actor in actor_subsystem().get_all_level_actors():
        if isinstance(actor, unreal.DirectionalLight):
            directionals.append(actor)
            configure_directional_light(actor)
        elif isinstance(actor, unreal.SkyLight):
            skylights.append(actor)
            configure_sky_light(actor)
        elif isinstance(actor, unreal.SkyAtmosphere):
            atmospheres.append(actor)

    if not directionals:
        spawn_directional_light()
    if not skylights:
        spawn_sky_light()
    if not atmospheres:
        spawn_sky_atmosphere()

    try:
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
        log("save_dirty_packages OK")
    except Exception as exc:
        warn(f"save_dirty_packages failed: {exc}")

    write_report()


if __name__ == "__main__":
    main()
