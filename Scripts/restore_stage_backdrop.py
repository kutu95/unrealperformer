"""Restore Stage_Backdrop beside Kristofer (same WP cell).

Reads Config/StageBackdropTransform.json. Uses offset_from_kristofer so the
backdrop is not saved into a distant unloaded WP cell.

EDITOR ONLY — Tools > Execute Python Script > restore_stage_backdrop.py
"""
from __future__ import annotations

import json
import math
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import godfrey_exhibit_guard  # noqa: E402

import unreal

REPORT = "RestoreStageBackdrop.txt"
CONFIG_REL = "Config/StageBackdropTransform.json"
PLANE_MESH = "/Engine/BasicShapes/Plane.Plane"
FALLBACK_MAT = "/Game/Godfrey/Backgrounds/M_Redgate_Background.M_Redgate_Background"
MAX_CELL_OFFSET = 250.0
DEFAULT_OFFSET = unreal.Vector(0.0, 0.0, 80.0)
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[RestoreBackdrop] {msg}")


def warn(msg: str) -> None:
    _lines.append(f"WARN: {msg}")
    unreal.log_warning(f"[RestoreBackdrop] {msg}")


def actors() -> unreal.EditorActorSubsystem:
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def load_exhibit_cells() -> None:
    world = unreal.EditorLevelLibrary.get_editor_world()
    if not world:
        raise RuntimeError("Open Godfrey_World before running")
    unreal.SystemLibrary.execute_console_command(world, "wp.Editor.LoadAllCells")
    log("Loaded all World Partition cells")


def read_transform_config() -> dict:
    config_path = os.path.join(unreal.Paths.project_dir(), CONFIG_REL.replace("/", os.sep))
    if not os.path.isfile(config_path):
        raise RuntimeError(f"Missing {CONFIG_REL}")
    with open(config_path, encoding="utf-8") as handle:
        data = json.load(handle)
    log(f"Loaded transform from {CONFIG_REL}")
    return data


def find_kristofer() -> unreal.Actor:
    for actor in actors().get_all_level_actors():
        label = actor.get_actor_label()
        if "Kristofer" in label and "Light" not in label and "PostProcess" not in label:
            return actor
    raise RuntimeError("BP_Kristofer not found — run wp.Editor.LoadAllCells first")


def find_backdrop() -> unreal.Actor | None:
    for actor in actors().get_all_level_actors():
        if actor.get_actor_label() == "Stage_Backdrop":
            return actor
    return None


def vec(data: dict) -> unreal.Vector:
    return unreal.Vector(float(data["x"]), float(data["y"]), float(data["z"]))


def rot(data: dict) -> unreal.Rotator:
    return unreal.Rotator(
        float(data.get("pitch", 0.0)),
        float(data.get("yaw", 0.0)),
        float(data.get("roll", 0.0)),
    )


def find_floor() -> unreal.Actor:
    for actor in actors().get_all_level_actors():
        if actor.get_actor_label() == "Exhibit_Floor":
            return actor
    raise RuntimeError("Exhibit_Floor not found — run wp.Editor.LoadAllCells first")


def resolve_location(cfg: dict, anchor: unreal.Vector) -> unreal.Vector:
    mode = cfg.get("location_mode", "offset_from_kristofer")
    if mode == "offset_from_kristofer":
        offset = vec(cfg.get("offset", {"x": DEFAULT_OFFSET.x, "y": DEFAULT_OFFSET.y, "z": DEFAULT_OFFSET.z}))
        loc = unreal.Vector(anchor.x + offset.x, anchor.y + offset.y, anchor.z + offset.z)
        dist = math.sqrt(offset.x ** 2 + offset.y ** 2 + offset.z ** 2)
        if dist > MAX_CELL_OFFSET:
            warn(f"Backdrop offset {dist:.0f} uu from anchor — may split WP cells")
        return loc

    loc = vec(cfg["location"])
    dist = math.sqrt((loc.x - anchor.x) ** 2 + (loc.y - anchor.y) ** 2 + (loc.z - anchor.z) ** 2)
    if dist > MAX_CELL_OFFSET:
        warn(
            f"Absolute location ({loc.x},{loc.y},{loc.z}) is {dist:.0f} uu from anchor — "
            f"using offset_from_kristofer instead"
        )
        return unreal.Vector(anchor.x + DEFAULT_OFFSET.x, anchor.y + DEFAULT_OFFSET.y, anchor.z + DEFAULT_OFFSET.z)
    return loc


def load_material(cfg: dict) -> unreal.MaterialInterface:
    path = cfg.get("material", FALLBACK_MAT)
    mat = unreal.load_asset(path.split(".")[0]) if "." in path else unreal.load_asset(path)
    if mat:
        log(f"Material: {mat.get_path_name()}")
        return mat
    mat = unreal.load_asset(FALLBACK_MAT)
    if not mat:
        raise RuntimeError("No backdrop material found")
    warn(f"Using fallback {FALLBACK_MAT}")
    return mat


def apply_backdrop_settings(actor: unreal.Actor, mat: unreal.MaterialInterface, scale: unreal.Vector) -> None:
    comp = actor.get_component_by_class(unreal.StaticMeshComponent)
    if not comp:
        raise RuntimeError("Stage_Backdrop has no StaticMeshComponent")
    mesh = unreal.load_asset(PLANE_MESH)
    if mesh:
        comp.set_static_mesh(mesh)
    comp.set_mobility(unreal.ComponentMobility.STATIONARY)
    comp.set_cast_shadow(False)
    comp.set_editor_property("affect_dynamic_indirect_lighting", False)
    comp.set_editor_property("visible_in_ray_tracing", False)
    comp.set_material(0, mat)
    actor.set_actor_scale3d(scale)


def restore_backdrop(cfg: dict) -> None:
    mat = load_material(cfg)
    r = rot(cfg["rotation"])
    scale = vec(cfg.get("scale", {"x": 12.0, "y": 8.0, "z": 1.0}))

    kristofer = find_kristofer()
    floor = find_floor()
    k_loc = kristofer.get_actor_location()
    floor_loc = floor.get_actor_location()
    # Anchor to Exhibit_Floor cell (same loaded region as Kristofer exhibit).
    loc = resolve_location(cfg, floor_loc)
    log(f"Kristofer at ({k_loc.x:.3f},{k_loc.y:.3f},{k_loc.z:.3f})")
    log(f"Exhibit_Floor at ({floor_loc.x:.3f},{floor_loc.y:.3f},{floor_loc.z:.3f})")
    log(f"Target backdrop at ({loc.x:.3f},{loc.y:.3f},{loc.z:.3f})")

    existing = find_backdrop()
    if existing:
        actor = existing
        log("Updating existing Stage_Backdrop")
        actor.set_actor_location(loc, False, True)
        actor.set_actor_rotation(r, False)
    else:
        actor = actors().spawn_actor_from_class(unreal.StaticMeshActor, loc, r)
        actor.set_actor_label("Stage_Backdrop")
        log(f"Spawned Stage_Backdrop at ({loc.x:.3f},{loc.y:.3f},{loc.z:.3f})")

    apply_backdrop_settings(actor, mat, scale)

    # Keep backdrop in Exhibit_Floor's WP cell (large plane bounds otherwise spill into unloaded cells).
    try:
        actor.attach_to_actor(
            floor,
            "",
            unreal.AttachmentRule.KEEP_WORLD,
            unreal.AttachmentRule.KEEP_WORLD,
            unreal.AttachmentRule.KEEP_WORLD,
            False,
        )
        log("Attached Stage_Backdrop to Exhibit_Floor for WP cell co-location")
    except Exception as exc:
        warn(f"Could not attach backdrop to floor: {exc}")

    final_loc = actor.get_actor_location()
    final_rot = actor.get_actor_rotation()
    final_scale = actor.get_actor_scale3d()
    log(
        f"Stage_Backdrop: loc=({final_loc.x:.3f},{final_loc.y:.3f},{final_loc.z:.3f}) "
        f"rot=({final_rot.pitch:.3f},{final_rot.yaw:.3f},{final_rot.roll:.3f}) "
        f"scale=({final_scale.x:.3f},{final_scale.y:.3f},{final_scale.z:.3f})"
    )


def write_report() -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines))
    log(f"Report: {path}")


def main() -> None:
    godfrey_exhibit_guard.reject_headless_spawn()
    godfrey_exhibit_guard.require_loaded(("BP_Kristofer", "Exhibit_Floor"))
    load_exhibit_cells()
    cfg = read_transform_config()
    restore_backdrop(cfg)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("save_dirty_packages OK")
    write_report()


if __name__ == "__main__":
    main()
