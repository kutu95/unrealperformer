"""Phase 6 step 6 — World Partition + exhibit placement finalize.

WARNING: Never run performer destroy/spawn via UnrealEditor-Cmd. A 2026-06-28 Cmd attempt
destroyed BP_Kristofer and crashed on quit in HairStrandsCore (MetaHuman groom bindings).

Confirms full inert performer stack (steps 2–5), BP_Godfrey_Performer on Godfrey_World,
GM_Godfrey_Exhibit, and exhibit sanity checks. Level swap/save is EDITOR ONLY.

  Open Godfrey_World, run wp.Editor.LoadAllCells in the console, then
  Tools > Execute Python Script (this file)

Headless (BP audit + read-only verify — no spawn/save):
  UnrealEditor-Cmd.exe "D:/UE Projects/MetaHuman_Baseline_Test/UnrealPerformer.uproject"
    /Game/Godfrey_World
    -ExecutePythonScript="D:/UE Projects/MetaHuman_Baseline_Test/Scripts/setup_phase6_step6_exhibit_finalize.py"
    -unattended -nop4 -nosplash -log
"""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import unreal

import godfrey_blueprint_wiring as wiring  # noqa: E402
import godfrey_exhibit_guard  # noqa: E402
import godfrey_performer_shell as shell  # noqa: E402
import verify_godfrey_exhibit as exhibit_verify  # noqa: E402

REPORT = "Phase6Step6ExhibitFinalize.txt"
GM_ASSET_PATH = "/Game/Godfrey/GM_Godfrey_Exhibit"
LEVEL_PATH = "/Game/Godfrey_World"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[Phase6Step6] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    header = "RESULT: PASS\n" if ok else "RESULT: FAIL\n"
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


def is_headless() -> bool:
    cmd = unreal.SystemLibrary.get_command_line().lower()
    return "-unattended" in cmd or "unrealeditor-cmd" in cmd


def audit_performer_blueprint() -> dict[str, object]:
    bp = unreal.load_asset(wiring.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {wiring.GODFREY_PERFORMER_BP}")
    audit = wiring.audit_step5_blueprint(bp)
    for key, value in audit.items():
        log(f"audit.{key} = {value}")
    return audit


def find_performer_actor():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    performer = None
    legacy = None
    for actor in eas.get_all_level_actors():
        label = actor.get_actor_label()
        if label == shell.LEVEL_PERFORMER_LABEL:
            performer = actor
        elif label == shell.LEGACY_PERFORMER_LABEL:
            legacy = actor
    return performer, legacy


def swap_performer_in_level(bp: unreal.Blueprint) -> None:
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    existing, legacy = find_performer_actor()

    if existing and not legacy:
        log(f"{shell.LEVEL_PERFORMER_LABEL} already in level — no swap needed")
        return

    source = legacy or existing
    if not source:
        raise RuntimeError(
            f"No {shell.LEGACY_PERFORMER_LABEL} or {shell.LEVEL_PERFORMER_LABEL} in level"
        )

    loc = source.get_actor_location()
    rot = source.get_actor_rotation()
    log(f"Captured transform from {source.get_actor_label()}: ({loc.x:.1f},{loc.y:.1f},{loc.z:.1f})")

    if legacy:
        eas.destroy_actor(legacy)
        log(f"Removed {shell.LEGACY_PERFORMER_LABEL}")

    if existing and existing != legacy:
        eas.destroy_actor(existing)
        log(f"Removed prior {shell.LEVEL_PERFORMER_LABEL} for clean respawn")

    generated = bp.generated_class() if hasattr(bp, "generated_class") else None
    if not generated:
        raise RuntimeError(f"No generated class for {shell.GODFREY_PERFORMER_BP}")

    actor = eas.spawn_actor_from_class(generated, loc, rot)
    if not actor:
        raise RuntimeError("spawn_actor_from_class failed")

    actor.set_actor_label(shell.LEVEL_PERFORMER_LABEL)
    actor.set_actor_hidden_in_game(False)
    log(f"Spawned {shell.LEVEL_PERFORMER_LABEL} at exhibit transform")


def check_preferred_performer_label() -> None:
    performer, legacy = find_performer_actor()
    if performer:
        log(f"Preferred performer label present: {shell.LEVEL_PERFORMER_LABEL}")
        return
    if legacy:
        raise RuntimeError(
            f"Level still has {shell.LEGACY_PERFORMER_LABEL} only — "
            "run this script from the editor (Tools > Execute Python Script) to swap."
        )
    raise RuntimeError("No performer actor in level")


def verify_gamemode_on_level() -> None:
    if not unreal.EditorAssetLibrary.does_asset_exist(GM_ASSET_PATH):
        raise RuntimeError(f"Missing {GM_ASSET_PATH} — run setup_fix_godfrey_exhibit_play_sphere.py")

    gm_bp = unreal.load_asset(GM_ASSET_PATH)
    gm_class = gm_bp.generated_class() if gm_bp and hasattr(gm_bp, "generated_class") else None
    if not gm_class:
        raise RuntimeError(f"No generated class for {GM_ASSET_PATH}")

    world = unreal.EditorLevelLibrary.get_editor_world()
    if not world:
        raise RuntimeError("No editor world — open Godfrey_World")

    ws = world.get_world_settings()
    if not ws:
        raise RuntimeError("No WorldSettings on Godfrey_World")

    assigned = None
    for prop in ("default_game_mode", "DefaultGameMode", "game_mode_override", "GameModeOverride"):
        try:
            assigned = ws.get_editor_property(prop)
            if assigned:
                log(f"Level GameMode ({prop}): {assigned.get_name()}")
                break
        except Exception:
            pass

    if not assigned:
        raise RuntimeError("Godfrey_World has no GameMode override — assign GM_Godfrey_Exhibit")

    if assigned.get_name() != gm_class.get_name():
        raise RuntimeError(
            f"GameMode mismatch: level has {assigned.get_name()}, expected {gm_class.get_name()}"
        )

    cdo = unreal.get_default_object(gm_class)
    pawn = None
    for prop in ("default_pawn_class", "DefaultPawnClass"):
        try:
            pawn = cdo.get_editor_property(prop)
            break
        except Exception:
            pass
    if pawn is not None:
        raise RuntimeError(f"GM_Godfrey_Exhibit DefaultPawnClass must be None (got {pawn})")
    log("GM_Godfrey_Exhibit DefaultPawnClass = None")


def run_exhibit_sanity_checks() -> None:
    exhibit_verify._lines.clear()
    exhibit_verify.load_all_cells()
    checks = (
        exhibit_verify.check_orphan_wp_packages(),
        exhibit_verify.check_single_exhibit_floor_package(),
        exhibit_verify.check_required_actors_loaded(),
        exhibit_verify.check_stage_actors_loaded(),
        exhibit_verify.check_stage_packages_on_disk(),
        exhibit_verify.check_no_duplicate_loaded_labels(),
        exhibit_verify.check_performer_placement(),
    )
    for line in exhibit_verify._lines:
        log(f"[ExhibitSanity] {line}")
    if not all(checks):
        raise RuntimeError("Exhibit sanity checks failed — see Saved/GodfreyExhibitSanity.txt")


def finalize_level_editor() -> None:
    godfrey_exhibit_guard.reject_headless_spawn()

    if unreal.EditorLevelLibrary.get_editor_world() is None:
        unreal.EditorLoadingAndSavingUtils.load_map(LEVEL_PATH)
        log(f"Loaded {LEVEL_PATH}")

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    loaded = {a.get_actor_label() for a in eas.get_all_level_actors()}
    if "Exhibit_Floor" not in loaded:
        raise RuntimeError("Exhibit_Floor not loaded — run wp.Editor.LoadAllCells and retry")
    if shell.LEVEL_PERFORMER_LABEL not in loaded and shell.LEGACY_PERFORMER_LABEL not in loaded:
        raise RuntimeError("No performer actor in level")

    bp = unreal.load_asset(shell.GODFREY_PERFORMER_BP)
    if not bp:
        raise RuntimeError(f"Missing {shell.GODFREY_PERFORMER_BP}")

    swap_performer_in_level(bp)
    check_preferred_performer_label()
    verify_gamemode_on_level()
    run_exhibit_sanity_checks()

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("Saved dirty packages — Ctrl+S on Godfrey_World if prompted")


def main() -> None:
    log(f"Mode: {'headless (read-only)' if is_headless() else 'editor'}")
    audit_performer_blueprint()

    if unreal.EditorLevelLibrary.get_editor_world() is None:
        unreal.EditorLoadingAndSavingUtils.load_map(LEVEL_PATH)
        log(f"Loaded {LEVEL_PATH}")

    if is_headless():
        log("Level swap/save skipped — run from editor to finalize WP placement")
        check_preferred_performer_label()
        verify_gamemode_on_level()
        run_exhibit_sanity_checks()
    else:
        finalize_level_editor()

    write_report(True)
    log("Phase 6 step 6 complete — zoom-test shirt 30+ s for final Phase 6 PASS gate")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[Phase6Step6] {exc}")
        write_report(False)
        sys.exit(1)
