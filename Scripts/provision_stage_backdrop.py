"""DEPRECATED — DO NOT RUN. Caused WP cell splits. See Docs/UE56_EditorScriptingGuidelines.md."""
from __future__ import annotations

import json
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import brighten_stage_backdrop  # noqa: E402
import restore_stage_backdrop  # noqa: E402

import unreal

REPORT = "ProvisionStageBackdrop.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[ProvisionBackdrop] {msg}")


def actors() -> unreal.EditorActorSubsystem:
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def load_cells() -> None:
    world = unreal.EditorLevelLibrary.get_editor_world()
    if world:
        unreal.SystemLibrary.execute_console_command(world, "wp.Editor.LoadAllCells")
        log("wp.Editor.LoadAllCells")


def package_has_actor(label: str, class_hint: bytes) -> list[str]:
    root = os.path.join(
        unreal.Paths.project_content_dir(), "__ExternalActors__", "Godfrey_World"
    )
    hits: list[str] = []
    needle = label.encode("ascii")
    if not os.path.isdir(root):
        return hits
    for dirpath, _, filenames in os.walk(root):
        for name in filenames:
            if not name.endswith(".uasset"):
                continue
            path = os.path.join(dirpath, name)
            with open(path, "rb") as handle:
                data = handle.read()
            if needle in data and class_hint in data:
                rel = path.replace(unreal.Paths.project_content_dir(), "Content/").replace("\\", "/")
                hits.append(rel)
    return hits


def missing_stage_labels() -> list[str]:
    loaded = {a.get_actor_label() for a in actors().get_all_level_actors()}
    need = ("Stage_Backdrop", "Stage_BackdropFill")
    return [label for label in need if label not in loaded]


def self_check() -> bool:
    load_cells()
    missing = missing_stage_labels()
    if missing:
        log(f"SELF-CHECK FAIL: not loaded after save: {', '.join(missing)}")
        return False
    backdrop_pkgs = package_has_actor("Stage_Backdrop", b"StaticMeshActor")
    fill_pkgs = package_has_actor("Stage_BackdropFill", b"RectLight")
    if len(backdrop_pkgs) != 1:
        log(f"SELF-CHECK FAIL: expected 1 Stage_Backdrop package, found {len(backdrop_pkgs)}: {backdrop_pkgs}")
        return False
    if len(fill_pkgs) != 1:
        log(f"SELF-CHECK FAIL: expected 1 Stage_BackdropFill package, found {len(fill_pkgs)}: {fill_pkgs}")
        return False
    for actor in actors().get_all_level_actors():
        if actor.get_actor_label() == "Stage_Backdrop":
            loc = actor.get_actor_location()
            log(f"SELF-CHECK PASS: Stage_Backdrop @ ({loc.x:.1f},{loc.y:.1f},{loc.z:.1f}) pkg={backdrop_pkgs[0]}")
            break
    log("SELF-CHECK PASS: Stage_Backdrop + Stage_BackdropFill loaded, one package each")
    return True


def write_report() -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines) + "\n")
    log(f"Report: {path}")


def main() -> None:
    raise RuntimeError(
        "provision_stage_backdrop.py is deprecated — place backdrop via editor UI. "
        "See Docs/UE56_EditorScriptingGuidelines.md"
    )
    missing_before = missing_stage_labels()
    if missing_before:
        log(f"Missing before provision: {', '.join(missing_before)}")
    else:
        log("Stage actors already present — refreshing material/fill light only")

    restore_stage_backdrop.load_exhibit_cells()
    cfg = restore_stage_backdrop.read_transform_config()
    restore_stage_backdrop.restore_backdrop(cfg)

    # Brighten without editor-only guard (same session, actors loaded).
    mic = brighten_stage_backdrop.get_or_create_mic()
    backdrop = restore_stage_backdrop.find_backdrop()
    if backdrop:
        brighten_stage_backdrop.apply_material(backdrop, mic)
        brighten_stage_backdrop.ensure_backdrop_fill_light(backdrop)
    else:
        kristofer = restore_stage_backdrop.find_kristofer()
        brighten_stage_backdrop.ensure_backdrop_fill_light(kristofer)

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("save_dirty_packages OK")

    if not self_check():
        raise RuntimeError("Stage backdrop provision failed self-check — see Saved/ProvisionStageBackdrop.txt")
    write_report()


if __name__ == "__main__":
    main()
