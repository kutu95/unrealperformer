"""Phase 6 step 1 — Godfrey performer shell (Kristofer meshes, no bridge).

1. Duplicate BP_Kristofer -> /Game/MetaHumans/Godfrey/BP_Godfrey_Performer
2. Verify mesh components only (no GodfreyPerformerAnimationBridge / ACE / speech)
3. Replace BP_Kristofer in Godfrey_World with BP_Godfrey_Performer (same transform)

EDITOR ONLY — do not run headless (World Partition).

  Tools > Execute Python Script (Godfrey_World open, wp.Editor.LoadAllCells run first)
"""
from __future__ import annotations

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import unreal

import godfrey_exhibit_guard  # noqa: E402
import godfrey_performer_shell as shell  # noqa: E402

REPORT = "Phase6Step1GodfreyShell.txt"
_lines: list[str] = []


def log(msg: str) -> None:
    _lines.append(msg)
    unreal.log(f"[Phase6Shell] {msg}")


def write_report(ok: bool) -> None:
    path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_saved_dir() + REPORT
    )
    header = "RESULT: PASS\n" if ok else "RESULT: FAIL\n"
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header + "\n".join(_lines) + "\n")
    log(f"Report: {path}")


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


def main() -> None:
    godfrey_exhibit_guard.reject_headless_spawn()
    godfrey_exhibit_guard.require_loaded(
        (shell.LEGACY_PERFORMER_LABEL, "Exhibit_Floor")
    )

    bp = shell.ensure_godfrey_performer_blueprint(replace_existing=False)
    shell.assert_shell_valid(bp)
    audit = shell.audit_shell_blueprint(bp)
    log(f"Shell asset: {shell.GODFREY_PERFORMER_BP}")
    log(f"Meshes: {', '.join(audit['mesh_found'])}")

    swap_performer_in_level(bp)

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("Saved dirty packages — Ctrl+S in editor if prompted")

    # Post-swap loaded check
    godfrey_exhibit_guard.require_loaded(
        (shell.LEVEL_PERFORMER_LABEL, "Exhibit_Floor")
    )
    write_report(True)
    log("Phase 6 step 1 complete — zoom-test Kristofer shirt 30+ seconds before step 2")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[Phase6Shell] {exc}")
        write_report(False)
        sys.exit(1)
