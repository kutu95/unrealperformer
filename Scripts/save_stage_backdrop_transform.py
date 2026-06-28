"""Write Stage_Backdrop transform to Config/StageBackdropTransform.json.

Run from editor with Godfrey_World open after positioning the backdrop:
  Tools > Execute Python Script > save_stage_backdrop_transform.py
"""
from __future__ import annotations

import json
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import godfrey_exhibit_guard  # noqa: E402

import unreal

BACKDROP_CONFIG = "Config/StageBackdropTransform.json"
LAYOUT_CONFIG = "Config/ExhibitLayout.json"
KEY_LABELS = (
    "Stage_Backdrop",
    "BP_Kristofer",
    "Exhibit_CineCamera",
    "Exhibit_Floor",
    "Exhibit_Fill_Key",
    "Exhibit_Fill_Rim",
    "Stage_BackdropFill",
    "Kristofer_PostProcess",
)
MAX_CELL_OFFSET = 250.0


def load_cells() -> None:
    world = unreal.EditorLevelLibrary.get_editor_world()
    if world:
        unreal.SystemLibrary.execute_console_command(world, "wp.Editor.LoadAllCells")


def actors() -> unreal.EditorActorSubsystem:
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def find_kristofer() -> unreal.Actor | None:
    for actor in actors().get_all_level_actors():
        label = actor.get_actor_label()
        if "Kristofer" in label and "Light" not in label and "PostProcess" not in label:
            return actor
    return None


def find_backdrop() -> unreal.Actor:
    for actor in actors().get_all_level_actors():
        if actor.get_actor_label() == "Stage_Backdrop":
            return actor
    raise RuntimeError(
        "Stage_Backdrop not found. Load Godfrey_World, run wp.Editor.LoadAllCells, "
        "then run restore_and_brighten_backdrop.py first."
    )


def actor_record(actor: unreal.Actor) -> dict:
    loc = actor.get_actor_location()
    rot = actor.get_actor_rotation()
    scale = actor.get_actor_scale3d()
    record = {
        "label": actor.get_actor_label(),
        "class": actor.get_class().get_name(),
        "location": {"x": loc.x, "y": loc.y, "z": loc.z},
        "rotation": {"pitch": rot.pitch, "yaw": rot.yaw, "roll": rot.roll},
        "scale": {"x": scale.x, "y": scale.y, "z": scale.z},
    }
    comp = actor.get_component_by_class(unreal.StaticMeshComponent)
    if comp:
        mat = comp.get_material(0)
        if mat:
            record["material"] = mat.get_path_name()
    return record


def write_json(relative_path: str, data: dict) -> None:
    path = os.path.join(unreal.Paths.project_dir(), relative_path.replace("/", os.sep))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2)
        handle.write("\n")
    unreal.log(f"[SaveExhibit] Wrote {relative_path}")


def main() -> None:
    godfrey_exhibit_guard.reject_headless_spawn()
    godfrey_exhibit_guard.require_loaded(
        ("BP_Kristofer", "Exhibit_Floor", "Stage_Backdrop")
    )
    load_cells()

    backdrop = find_backdrop()
    kristofer = find_kristofer()
    loc = backdrop.get_actor_location()
    rot = backdrop.get_actor_rotation()
    scale = backdrop.get_actor_scale3d()
    comp = backdrop.get_component_by_class(unreal.StaticMeshComponent)
    mat_path = ""
    if comp:
        mat = comp.get_material(0)
        if mat:
            mat_path = mat.get_path_name()

    offset = {"x": loc.x, "y": loc.y, "z": loc.z}
    if kristofer:
        k_loc = kristofer.get_actor_location()
        offset = {
            "x": loc.x - k_loc.x,
            "y": loc.y - k_loc.y,
            "z": loc.z - k_loc.z,
        }

    write_json(
        BACKDROP_CONFIG,
        {
            "note": "Captured from editor. offset_from_kristofer keeps backdrop in Kristofer WP cell.",
            "location_mode": "offset_from_kristofer",
            "offset": offset,
            "rotation": {"pitch": rot.pitch, "yaw": rot.yaw, "roll": rot.roll},
            "scale": {"x": scale.x, "y": scale.y, "z": scale.z},
            "material": mat_path or "/Game/Godfrey/Backgrounds/MI_Stage_Backdrop.MI_Stage_Backdrop",
        },
    )

    layout: dict[str, dict] = {}
    for actor in actors().get_all_level_actors():
        label = actor.get_actor_label()
        if label in KEY_LABELS or "Kristofer" in label or "Exhibit_" in label or "Stage_" in label:
            layout[label] = actor_record(actor)

    write_json(
        LAYOUT_CONFIG,
        {"note": "Exhibit actor transforms captured from editor.", "actors": layout},
    )

    unreal.log(f"[SaveExhibit] Stage_Backdrop offset=({offset['x']},{offset['y']},{offset['z']})")
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    unreal.log("[SaveExhibit] save_dirty_packages OK")


if __name__ == "__main__":
    main()
