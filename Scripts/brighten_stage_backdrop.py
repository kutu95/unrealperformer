"""Brighten Redgate background meshes — does NOT move or delete any actors.

WARNING: Do NOT run headless on Godfrey_World — use editor-only with all exhibit
cells loaded first.

Applies MI_Stage_Backdrop (unlit + emissive) to Stage_SkySphere and Stage_Backdrop.
Adds Stage_BackdropFill rect light near the sky sphere when backdrop plane is absent.

Run with Godfrey_World open: Tools > Execute Python Script (EDITOR ONLY).
"""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import godfrey_exhibit_guard  # noqa: E402

import unreal

REPORT = "BrightenStageBackdrop.txt"
PARENT_MAT = "/Game/Godfrey/Backgrounds/M_Redgate_Background.M_Redgate_Background"
MIC_PATH = "/Game/Godfrey/Backgrounds/MI_Stage_Backdrop"
TARGET_LABELS = ("Stage_SkySphere", "Stage_Backdrop")
BACKDROP_LIGHT_LABEL = "Stage_BackdropFill"
EMISSIVE_BOOST = 24.0
RECT_INTENSITY = 120000.0
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[BrightenBackdrop] {msg}")


def warn(msg: str) -> None:
    _lines.append(f"WARN: {msg}")
    unreal.log_warning(f"[BrightenBackdrop] {msg}")


def actors() -> unreal.EditorActorSubsystem:
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def try_set(obj, names: tuple[str, ...], value) -> str | None:
    for name in names:
        try:
            obj.set_editor_property(name, value)
            return name
        except Exception:
            continue
    return None


def get_or_create_mic() -> unreal.MaterialInstanceConstant:
    if unreal.EditorAssetLibrary.does_asset_exist(MIC_PATH):
        mic = unreal.EditorAssetLibrary.load_asset(MIC_PATH)
        log(f"Loaded existing {MIC_PATH}")
    else:
        parent = unreal.load_asset(PARENT_MAT)
        if not parent:
            raise RuntimeError(f"Missing parent material {PARENT_MAT}")

        factory = unreal.MaterialInstanceConstantFactoryNew()
        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        mic = asset_tools.create_asset(
            "MI_Stage_Backdrop",
            "/Game/Godfrey/Backgrounds",
            unreal.MaterialInstanceConstant,
            factory,
        )
        mic.set_editor_property("parent", parent)
        log(f"Created {MIC_PATH}")

    overrides = unreal.MaterialInstanceBasePropertyOverrides()
    for prop, val in (
        (("b_override_shading_model", "override_shading_model"), True),
        (("shading_model",), unreal.MaterialShadingModel.MSM_UNLIT),
        (("b_override_two_sided", "override_two_sided"), True),
        (("two_sided",), True),
    ):
        hit = try_set(overrides, prop, val)
        if hit:
            log(f"override {hit} = {val}")

    mic.set_editor_property("base_property_overrides", overrides)

    for scalar_name, value in (
        ("Emissive", EMISSIVE_BOOST),
        ("EmissiveStrength", EMISSIVE_BOOST),
        ("Brightness", EMISSIVE_BOOST),
        ("Exposure", 2.0),
        ("Multiply", EMISSIVE_BOOST),
    ):
        try:
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                mic, scalar_name, value
            )
            log(f"Scalar {scalar_name} = {value}")
        except Exception:
            pass

    bright = unreal.LinearColor(1.0, 1.0, 1.0, 1.0)
    for vector_name in ("EmissiveColor", "Emissive", "BaseColor", "Color"):
        try:
            unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
                mic, vector_name, bright
            )
            log(f"Vector {vector_name} = white")
        except Exception:
            pass

    unreal.EditorAssetLibrary.save_loaded_assets([mic])
    return mic


def load_cells() -> None:
    world = unreal.EditorLevelLibrary.get_editor_world()
    if world:
        unreal.SystemLibrary.execute_console_command(world, "wp.Editor.LoadAllCells")
        log("Loaded all World Partition cells")


def find_backdrop() -> unreal.Actor | None:
    for actor in actors().get_all_level_actors():
        if actor.get_actor_label() == "Stage_Backdrop":
            return actor
    return None


def find_redgate_meshes() -> list[unreal.Actor]:
    found: list[unreal.Actor] = []
    seen: set[str] = set()
    parent = unreal.load_asset(PARENT_MAT)

    for actor in actors().get_all_level_actors():
        label = actor.get_actor_label()
        if label not in TARGET_LABELS and "Backdrop" not in label:
            continue
        if not isinstance(actor, unreal.StaticMeshActor):
            continue
        if label in seen:
            continue
        seen.add(label)
        found.append(actor)

    if found:
        return found

    for actor in actors().get_all_level_actors():
        if not isinstance(actor, unreal.StaticMeshActor):
            continue
        label = actor.get_actor_label()
        if label in ("Exhibit_Floor",):
            continue
        comp = actor.get_component_by_class(unreal.StaticMeshComponent)
        if not comp:
            continue
        mat = comp.get_material(0)
        if mat and parent and mat.get_path_name().startswith(PARENT_MAT.rsplit(".", 1)[0]):
            found.append(actor)
    return found


def apply_material(actor: unreal.Actor, mic: unreal.MaterialInstanceConstant) -> None:
    comp = actor.get_component_by_class(unreal.StaticMeshComponent)
    if not comp:
        return
    log(
        f"{actor.get_actor_label()} transform preserved at "
        f"{actor.get_actor_location()}"
    )
    comp.set_material(0, mic)
    log(f"Applied MI_Stage_Backdrop to {actor.get_actor_label()}")


def ensure_backdrop_fill_light(anchor: unreal.Actor) -> None:
    loc = anchor.get_actor_location()
    rot = anchor.get_actor_rotation()
    # Place rect light just in front of the backdrop plane, facing back into it.
    offset = unreal.MathLibrary.get_forward_vector(rot) * 40.0
    light_loc = unreal.Vector(loc.x + offset.x, loc.y + offset.y, loc.z + offset.z)
    light_rot = unreal.Rotator(rot.pitch, rot.yaw + 180.0, rot.roll)

    existing = None
    for actor in actors().get_all_level_actors():
        if actor.get_actor_label() == BACKDROP_LIGHT_LABEL:
            existing = actor
            break

    if existing:
        existing.set_actor_location(light_loc, False, True)
        existing.set_actor_rotation(light_rot, False)
        actor = existing
        log(f"Updated {BACKDROP_LIGHT_LABEL}")
    else:
        actor = actors().spawn_actor_from_class(unreal.RectLight, light_loc, light_rot)
        actor.set_actor_label(BACKDROP_LIGHT_LABEL)
        log(f"Spawned {BACKDROP_LIGHT_LABEL}")

    comp = actor.get_component_by_class(unreal.RectLightComponent)
    if not comp:
        return

    comp.set_mobility(unreal.ComponentMobility.MOVABLE)
    comp.set_intensity(RECT_INTENSITY)
    comp.set_source_width(600.0)
    comp.set_source_height(450.0)
    comp.set_attenuation_radius(6000.0)
    comp.set_cast_shadows(False)
    comp.set_temperature(6500.0)
    log(f"{BACKDROP_LIGHT_LABEL}: intensity={RECT_INTENSITY}")


def write_report() -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines))
    log(f"Report: {path}")


def main() -> None:
    godfrey_exhibit_guard.reject_headless_spawn()
    godfrey_exhibit_guard.require_loaded(("BP_Kristofer", "Exhibit_Floor", "Stage_Backdrop"))
    load_cells()
    mic = get_or_create_mic()
    meshes = find_redgate_meshes()
    if not meshes:
        raise RuntimeError("No Stage_SkySphere / Stage_Backdrop / Redgate mesh found")

    for actor in meshes:
        apply_material(actor, mic)

    anchor = find_backdrop() or meshes[0]
    ensure_backdrop_fill_light(anchor)

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("save_dirty_packages OK")
    write_report()


if __name__ == "__main__":
    main()
