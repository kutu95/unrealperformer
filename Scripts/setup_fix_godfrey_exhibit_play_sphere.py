"""Create GM_Godfrey_Exhibit (Default Pawn = None) and assign on Godfrey_World.

Fixes PIE checkerboard DefaultPawn sphere when using Selected Viewport play.

Run with Godfrey_World open (or script loads map):
  Tools > Execute Python Script
"""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import unreal

GM_PACKAGE = "/Game/Godfrey"
GM_ASSET_PATH = "/Game/Godfrey/GM_Godfrey_Exhibit"
GM_ASSET_NAME = "GM_Godfrey_Exhibit"
LEVEL_PATH = "/Game/Godfrey_World"
REPORT = "FixGodfreyExhibitPlaySphere.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[FixPlaySphere] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    header = "RESULT: PASS\n" if ok else "RESULT: FAIL\n"
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def ensure_godfrey_package() -> None:
    if not unreal.EditorAssetLibrary.does_directory_exist(GM_PACKAGE):
        unreal.EditorAssetLibrary.make_directory(GM_PACKAGE)
        log(f"Created content folder {GM_PACKAGE}")


def ensure_gamemode_blueprint() -> unreal.Blueprint:
    ensure_godfrey_package()

    if unreal.EditorAssetLibrary.does_asset_exist(GM_ASSET_PATH):
        bp = unreal.load_asset(GM_ASSET_PATH)
        if bp:
            log(f"Using existing {GM_ASSET_PATH}")
            return bp
        raise RuntimeError(f"Could not load existing {GM_ASSET_PATH}")

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("ParentClass", unreal.GameModeBase)

    bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        GM_ASSET_NAME,
        GM_PACKAGE,
        unreal.Blueprint,
        factory,
    )
    if not bp:
        raise RuntimeError(f"Failed to create {GM_ASSET_PATH}")

    log(f"Created {GM_ASSET_PATH}")
    return bp


def set_default_pawn_none(bp: unreal.Blueprint) -> None:
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    generated = bp.generated_class()
    if not generated:
        raise RuntimeError("GM_Godfrey_Exhibit has no generated class after compile")

    cdo = unreal.get_default_object(generated)
    if not cdo:
        raise RuntimeError("Could not get GameMode CDO")

    for prop in ("default_pawn_class", "DefaultPawnClass"):
        try:
            cdo.set_editor_property(prop, None)
            log(f"Set CDO {prop} = None")
            break
        except Exception:
            continue
    else:
        raise RuntimeError("Could not set DefaultPawnClass on GM_Godfrey_Exhibit CDO")

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_loaded_assets([bp])


def assign_gamemode_on_level(gm_class) -> None:
    if not unreal.EditorAssetLibrary.does_asset_exist(LEVEL_PATH):
        raise RuntimeError(f"Missing level {LEVEL_PATH}")

    if unreal.EditorLevelLibrary.get_editor_world() is None:
        unreal.EditorLoadingAndSavingUtils.load_map(LEVEL_PATH)
        log(f"Loaded {LEVEL_PATH}")

    world = unreal.EditorLevelLibrary.get_editor_world()
    if not world:
        raise RuntimeError("No editor world after load")

    world_settings = world.get_world_settings()
    if not world_settings:
        raise RuntimeError("No WorldSettings on Godfrey_World")

    for prop in ("default_game_mode", "DefaultGameMode", "game_mode_override", "GameModeOverride"):
        try:
            world_settings.set_editor_property(prop, gm_class)
            log(f"WorldSettings.{prop} = {gm_class.get_name()}")
            break
        except Exception:
            continue
    else:
        raise RuntimeError("Could not set GameMode override on WorldSettings")

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("Saved dirty packages — Ctrl+S on Godfrey_World if prompted")


def verify(gm_class) -> None:
    cdo = unreal.get_default_object(gm_class)
    pawn = None
    for prop in ("default_pawn_class", "DefaultPawnClass"):
        try:
            pawn = cdo.get_editor_property(prop)
            break
        except Exception:
            pass
    if pawn is not None:
        raise RuntimeError(f"GM_Godfrey_Exhibit DefaultPawnClass should be None (got {pawn})")

    world = unreal.EditorLevelLibrary.get_editor_world()
    if world:
        ws = world.get_world_settings()
        assigned = None
        for prop in ("default_game_mode", "DefaultGameMode", "game_mode_override", "GameModeOverride"):
            try:
                assigned = ws.get_editor_property(prop)
                if assigned:
                    log(f"Level GameMode ({prop}): {assigned.get_name()}")
                    break
            except Exception:
                pass


def main() -> None:
    bp = ensure_gamemode_blueprint()
    set_default_pawn_none(bp)
    gm_class = bp.generated_class()
    if not gm_class:
        raise RuntimeError("No generated class on GM_Godfrey_Exhibit")

    assign_gamemode_on_level(gm_class)
    verify(gm_class)
    write_report(True)
    log("Play sphere fix applied — PIE should not spawn DefaultPawn sphere")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[FixPlaySphere] {exc}")
        write_report(False)
        sys.exit(1)
